#include <chrono>
#include <iostream>
#include <string>

#include "kv/http.hpp"
#include "kv/types.hpp"

int main(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "usage: kv_cli <host> <port> <put|get|delete> <key> [value]\n";
    return 2;
  }
  const std::string operation = argv[3];
  std::string method;
  std::string body;
  if (operation == "put" && argc == 6) {
    method = "PUT";
    body = "{\"value\":\"" + kv::JsonEscape(argv[5]) + "\"}";
  } else if (operation == "get")
    method = "GET";
  else if (operation == "delete")
    method = "DELETE";
  else {
    std::cerr << "invalid operation or arguments\n";
    return 2;
  }
  const auto response =
      kv::HttpCall(argv[1], static_cast<std::uint16_t>(std::stoul(argv[2])), method,
                   "/v1/kv/" + kv::UrlEncode(argv[4]), body, std::chrono::milliseconds(2000));
  if (!response.transport_ok) {
    std::cerr << response.error << '\n';
    return 1;
  }
  std::cout << response.body << '\n';
  return response.status >= 200 && response.status < 300 ? 0 : 1;
}
