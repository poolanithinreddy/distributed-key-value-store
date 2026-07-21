#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "kv/hash_ring.hpp"
#include "kv/metrics.hpp"
#include "kv/peer_client.hpp"
#include "kv/storage_engine.hpp"
#include "kv/thread_pool.hpp"
#include "kv/types.hpp"

namespace kv {

class ReplicationCoordinator {
 public:
  ReplicationCoordinator(std::string node_id, std::vector<Peer> peers, std::size_t virtual_nodes,
                         std::size_t replication_factor, std::size_t read_quorum,
                         std::size_t write_quorum, std::chrono::milliseconds timeout,
                         StorageEngine& storage, ReplicaClient& client, ThreadPool& pool,
                         Metrics& metrics);
  Result Put(const std::string& key, const std::string& value);
  Result Delete(const std::string& key);
  Result Get(const std::string& key);
  bool ApplyReplica(const std::string& key, const Record& record);
  [[nodiscard]] std::vector<std::string> ReplicaIds(const std::string& key) const;

 private:
  Result WriteRecord(const std::string& key, const Record& record);
  std::optional<Peer> FindPeer(const std::string& id) const;
  std::string node_id_;
  std::vector<Peer> peers_;
  HashRing ring_;
  std::size_t replication_factor_;
  std::size_t read_quorum_;
  std::size_t write_quorum_;
  std::chrono::milliseconds timeout_;
  StorageEngine& storage_;
  ReplicaClient& client_;
  ThreadPool& pool_;
  Metrics& metrics_;
  std::atomic<std::uint64_t> logical_clock_{0};
};

}  // namespace kv
