#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

#include "kv/thread_pool.hpp"

namespace kv {

struct HttpRequest {
  std::string method;
  std::string target;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status{200};
  std::string content_type{"application/json"};
  std::string body;
};

struct HttpClientResponse {
  bool transport_ok{false};
  int status{0};
  std::string body;
  std::string error;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
 public:
  HttpServer(std::string host, std::uint16_t port, std::size_t threads, std::size_t queue_capacity,
             HttpHandler handler);
  ~HttpServer();
  void Start();
  void Stop();
  [[nodiscard]] bool Running() const { return running_.load(); }

 private:
  void AcceptLoop();
  void HandleClient(int socket);
  std::string host_;
  std::uint16_t port_;
  HttpHandler handler_;
  ThreadPool pool_;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  int listen_socket_{-1};
};

HttpClientResponse HttpCall(const std::string& host, std::uint16_t port, const std::string& method,
                            const std::string& target, const std::string& body,
                            std::chrono::milliseconds timeout);
std::string UrlEncode(const std::string& value);
std::string UrlDecode(const std::string& value);

}  // namespace kv
