#include "kv/hash_ring.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace kv {

std::uint64_t StableHash(const std::string& value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  // MurmurHash3's 64-bit finalizer avalanches FNV-1a's otherwise correlated
  // neighboring inputs (for example, virtual-node suffixes).
  hash ^= hash >> 33U;
  hash *= 0xff51afd7ed558ccdULL;
  hash ^= hash >> 33U;
  hash *= 0xc4ceb9fe1a85ec53ULL;
  hash ^= hash >> 33U;
  return hash;
}

HashRing::HashRing(std::size_t virtual_nodes) : virtual_nodes_(virtual_nodes) {
  if (virtual_nodes == 0) throw std::invalid_argument("virtual_nodes must be positive");
}

void HashRing::SetNodes(const std::vector<std::string>& node_ids) {
  ring_.clear();
  std::vector<std::string> nodes = node_ids;
  std::sort(nodes.begin(), nodes.end());
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  for (const auto& node : nodes) {
    if (node.empty()) throw std::invalid_argument("node id cannot be empty");
    for (std::size_t index = 0; index < virtual_nodes_; ++index) {
      auto token = StableHash(node + "#" + std::to_string(index));
      while (ring_.count(token) != 0) ++token;
      ring_.emplace(token, node);
    }
  }
}

std::vector<std::string> HashRing::Replicas(const std::string& key, std::size_t count) const {
  std::vector<std::string> result;
  if (ring_.empty() || count == 0) return result;
  std::unordered_set<std::string> seen;
  auto cursor = ring_.lower_bound(StableHash(key));
  if (cursor == ring_.end()) cursor = ring_.begin();
  const auto start = cursor;
  do {
    if (seen.insert(cursor->second).second) result.push_back(cursor->second);
    if (result.size() == count) break;
    ++cursor;
    if (cursor == ring_.end()) cursor = ring_.begin();
  } while (cursor != start);
  return result;
}

std::string HashRing::Owner(const std::string& key) const {
  const auto replicas = Replicas(key, 1);
  if (replicas.empty()) throw std::runtime_error("hash ring has no nodes");
  return replicas.front();
}

}  // namespace kv
