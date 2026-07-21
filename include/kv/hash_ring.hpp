#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kv {

std::uint64_t StableHash(const std::string& value);

class HashRing {
 public:
  explicit HashRing(std::size_t virtual_nodes = 128);
  void SetNodes(const std::vector<std::string>& node_ids);
  [[nodiscard]] std::vector<std::string> Replicas(const std::string& key, std::size_t count) const;
  [[nodiscard]] std::string Owner(const std::string& key) const;
  [[nodiscard]] std::size_t PointCount() const { return ring_.size(); }

 private:
  std::size_t virtual_nodes_;
  std::map<std::uint64_t, std::string> ring_;
};

}  // namespace kv
