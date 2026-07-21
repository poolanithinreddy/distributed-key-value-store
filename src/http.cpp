#include "kv/http.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "kv/types.hpp"

namespace kv {
namespace {
constexpr std::size_t kMaxRequest = 8U * 1024U * 1024U;

class SocketGuard {
 public:
  explicit SocketGuard(int descriptor) : descriptor_(descriptor) {}
  ~SocketGuard() {
    if (descriptor_ >= 0) ::close(descriptor_);
  }
  SocketGuard(const SocketGuard&) = delete;
  SocketGuard& operator=(const SocketGuard&) = delete;
  int get() const { return descriptor_; }

 private:
  int descriptor_;
};

bool SendAll(int socket, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto count = ::send(socket, data.data() + sent, data.size() - sent, 0);
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

std::string StatusText(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 408:
      return "Request Timeout";
    case 503:
      return "Service Unavailable";
    default:
      return "Internal Server Error";
  }
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::size_t ContentLength(const std::string& headers) {
  std::smatch match;
  if (!std::regex_search(headers, match, std::regex("[Cc]ontent-[Ll]ength:\\s*([0-9]+)"))) return 0;
  return static_cast<std::size_t>(std::stoull(match[1].str()));
}

int Connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout,
            std::string& error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const auto port_string = std::to_string(port);
  if (::getaddrinfo(host.c_str(), port_string.c_str(), &hints, &addresses) != 0) {
    error = "address resolution failed";
    return -1;
  }
  int result = -1;
  for (auto* address = addresses; address != nullptr && result < 0; address = address->ai_next) {
    const int socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket < 0) continue;
    const int flags = ::fcntl(socket, F_GETFL, 0);
    ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
    const int connected = ::connect(socket, address->ai_addr, address->ai_addrlen);
    if (connected == 0 || errno == EINPROGRESS) {
      fd_set writable;
      FD_ZERO(&writable);
      FD_SET(socket, &writable);
      timeval wait{static_cast<long>(timeout.count() / 1000),
                   static_cast<int>((timeout.count() % 1000) * 1000)};
      if (::select(socket + 1, nullptr, &writable, nullptr, &wait) > 0) {
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        ::getsockopt(socket, SOL_SOCKET, SO_ERROR, &socket_error, &length);
        if (socket_error == 0) {
          ::fcntl(socket, F_SETFL, flags);
          result = socket;
          continue;
        }
      }
    }
    ::close(socket);
  }
  ::freeaddrinfo(addresses);
  if (result < 0) error = "connection failed or timed out";
  return result;
}
}  // namespace

HttpServer::HttpServer(std::string host, std::uint16_t port, std::size_t threads,
                       std::size_t queue_capacity, HttpHandler handler)
    : host_(std::move(host)),
      port_(port),
      handler_(std::move(handler)),
      connection_pool_(std::max<std::size_t>(2, threads / 2), queue_capacity),
      public_pool_(threads, queue_capacity),
      internal_pool_(threads, queue_capacity) {}
HttpServer::~HttpServer() { Stop(); }

void HttpServer::Start() {
  if (running_.exchange(true)) return;
  const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
  listen_socket_.store(descriptor);
  if (descriptor < 0) {
    running_ = false;
    throw std::runtime_error("socket creation failed");
  }
  int reuse = 1;
  ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1 ||
      ::bind(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(descriptor, 128) != 0) {
    ::close(descriptor);
    listen_socket_.store(-1);
    running_ = false;
    throw std::runtime_error("cannot bind HTTP server to " + host_ + ':' + std::to_string(port_));
  }
  accept_thread_ = std::thread([this] { AcceptLoop(); });
}

void HttpServer::Stop() {
  if (!running_.exchange(false)) return;
  const int descriptor = listen_socket_.exchange(-1);
  if (descriptor >= 0) {
    ::shutdown(descriptor, SHUT_RDWR);
    ::close(descriptor);
  }
  if (accept_thread_.joinable()) accept_thread_.join();
  connection_pool_.Stop();
  public_pool_.Stop();
  internal_pool_.Stop();
}

void HttpServer::AcceptLoop() {
  while (running_.load()) {
    const int descriptor = listen_socket_.load();
    if (descriptor < 0) break;
    const int client = ::accept(descriptor, nullptr, nullptr);
    if (client < 0) {
      if (running_.load() && errno == EINTR) continue;
      break;
    }
    try {
      static_cast<void>(connection_pool_.Submit([this, client] { HandleClient(client); }));
    } catch (const std::exception&) {
      ::close(client);
    }
  }
}

void HttpServer::HandleClient(int socket) {
  auto owner = std::make_shared<SocketGuard>(socket);
  timeval timeout{5, 0};
  ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  std::string data;
  std::array<char, 4096> buffer{};
  std::size_t header_end = std::string::npos;
  while (data.size() < kMaxRequest && (header_end = data.find("\r\n\r\n")) == std::string::npos) {
    const auto count = ::recv(socket, buffer.data(), buffer.size(), 0);
    if (count <= 0) return;
    data.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (header_end == std::string::npos) return;
  const auto expected = ContentLength(data.substr(0, header_end));
  while (data.size() - header_end - 4 < expected && data.size() < kMaxRequest) {
    const auto count = ::recv(socket, buffer.data(), buffer.size(), 0);
    if (count <= 0) return;
    data.append(buffer.data(), static_cast<std::size_t>(count));
  }
  std::istringstream lines(data.substr(0, header_end));
  HttpRequest request;
  std::string line;
  if (!std::getline(lines, line)) return;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  std::istringstream first(line);
  std::string version;
  first >> request.method >> request.target >> version;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon != std::string::npos)
      request.headers[Lower(line.substr(0, colon))] = line.substr(colon + 1);
  }
  request.body = data.substr(header_end + 4, expected);
  const bool internal = request.target.rfind("/internal/", 0) == 0 || request.target == "/health" ||
                        request.target == "/ready";
  auto task = [this, owner, request = std::move(request)]() mutable {
    ProcessRequest(owner->get(), std::move(request));
  };
  try {
    if (internal)
      static_cast<void>(internal_pool_.Submit(std::move(task)));
    else
      static_cast<void>(public_pool_.Submit(std::move(task)));
  } catch (const std::exception&) {
  }
}

void HttpServer::ProcessRequest(int socket, HttpRequest request) {
  HttpResponse response;
  try {
    response = handler_(request);
  } catch (const std::exception& error) {
    response = {500, "application/json",
                "{\"error\":\"internal_error\",\"message\":\"" + JsonEscape(error.what()) + "\"}"};
  }
  const std::string wire = "HTTP/1.1 " + std::to_string(response.status) + ' ' +
                           StatusText(response.status) +
                           "\r\nContent-Type: " + response.content_type +
                           "\r\nContent-Length: " + std::to_string(response.body.size()) +
                           "\r\nConnection: close\r\n\r\n" + response.body;
  static_cast<void>(SendAll(socket, wire));
}

HttpClientResponse HttpCall(const std::string& host, std::uint16_t port, const std::string& method,
                            const std::string& target, const std::string& body,
                            std::chrono::milliseconds timeout) {
  HttpClientResponse response;
  std::string error;
  const int descriptor = Connect(host, port, timeout, error);
  if (descriptor < 0) {
    response.error = error;
    return response;
  }
  SocketGuard socket(descriptor);
  timeval wait{static_cast<long>(timeout.count() / 1000),
               static_cast<int>((timeout.count() % 1000) * 1000)};
  ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
  ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
  const std::string request =
      method + ' ' + target + " HTTP/1.1\r\nHost: " + host +
      "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\nConnection: close\r\n\r\n" + body;
  if (!SendAll(descriptor, request)) {
    response.error = "send failed";
    return response;
  }
  std::string data;
  std::array<char, 4096> buffer{};
  while (data.size() < kMaxRequest) {
    const auto count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
    if (count == 0) break;
    if (count < 0) {
      response.error = "receive timed out or failed";
      return response;
    }
    data.append(buffer.data(), static_cast<std::size_t>(count));
  }
  const auto header_end = data.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    response.error = "invalid response";
    return response;
  }
  std::istringstream first(data.substr(0, data.find("\r\n")));
  std::string version;
  first >> version >> response.status;
  response.body = data.substr(header_end + 4);
  response.transport_ok = response.status > 0;
  return response;
}

std::string UrlEncode(const std::string& value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (const char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out << static_cast<char>(c);
    else
      out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
  }
  return out.str();
}
std::string UrlDecode(const std::string& value) {
  std::string out;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      try {
        out.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)));
        i += 2;
      } catch (const std::exception&) {
        out.push_back(value[i]);
      }
    } else if (value[i] == '+')
      out.push_back(' ');
    else
      out.push_back(value[i]);
  }
  return out;
}

}  // namespace kv
