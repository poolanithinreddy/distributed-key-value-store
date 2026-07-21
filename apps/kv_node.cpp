#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <thread>

#include "kv/config.hpp"
#include "kv/node.hpp"

namespace {
std::atomic<bool> keep_running{true};
void Signal(int) { keep_running = false; }
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: kv_node <config.json>\n";
    return 2;
  }
  try {
    std::signal(SIGINT, Signal);
    std::signal(SIGTERM, Signal);
    kv::Node node(kv::Config::Load(argv[1]));
    node.Start();
    while (keep_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    node.Stop();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "kv_node: " << error.what() << '\n';
    return 1;
  }
}
