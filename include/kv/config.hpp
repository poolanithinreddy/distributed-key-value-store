#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "kv/types.hpp"

namespace kv {

struct Config {
  std::string node_id;
  std::string listen_host{"127.0.0.1"};
  std::uint16_t listen_port{0};
  std::string data_dir{"data"};
  std::vector<Peer> peers;
  std::size_t virtual_nodes{128};
  std::size_t replication_factor{3};
  std::size_t read_quorum{2};
  std::size_t write_quorum{2};
  std::size_t worker_threads{8};
  std::size_t queue_capacity{1024};
  std::chrono::milliseconds request_timeout{500};
  std::chrono::milliseconds health_interval{500};
  std::size_t failure_threshold{3};

  void Validate() const;
  static Config Load(const std::string& path);
};

}  // namespace kv
