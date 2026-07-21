#include "kv/replication.hpp"

#include <algorithm>
#include <future>

namespace kv {

ReplicationCoordinator::ReplicationCoordinator(std::string node_id, std::vector<Peer> peers,
                                               std::size_t virtual_nodes,
                                               std::size_t replication_factor,
                                               std::size_t read_quorum, std::size_t write_quorum,
                                               std::chrono::milliseconds timeout,
                                               StorageEngine& storage, ReplicaClient& client,
                                               ThreadPool& pool, Metrics& metrics)
    : node_id_(std::move(node_id)),
      peers_(std::move(peers)),
      ring_(virtual_nodes),
      replication_factor_(replication_factor),
      read_quorum_(read_quorum),
      write_quorum_(write_quorum),
      timeout_(timeout),
      storage_(storage),
      client_(client),
      pool_(pool),
      metrics_(metrics) {
  std::vector<std::string> ids;
  for (const auto& peer : peers_) ids.push_back(peer.id);
  ring_.SetNodes(ids);
  logical_clock_.store(storage_.MaxVersionCounter());
}

std::optional<Peer> ReplicationCoordinator::FindPeer(const std::string& id) const {
  const auto found =
      std::find_if(peers_.begin(), peers_.end(), [&](const Peer& peer) { return peer.id == id; });
  return found == peers_.end() ? std::nullopt : std::optional<Peer>(*found);
}

std::vector<std::string> ReplicationCoordinator::ReplicaIds(const std::string& key) const {
  return ring_.Replicas(key, replication_factor_);
}

bool ReplicationCoordinator::ApplyReplica(const std::string& key, const Record& record) {
  auto current = logical_clock_.load();
  while (current < record.version.counter &&
         !logical_clock_.compare_exchange_weak(current, record.version.counter)) {
  }
  storage_.PutIfNewer(key, record);
  return true;
}

Result ReplicationCoordinator::Put(const std::string& key, const std::string& value) {
  const auto current = Get(key);
  if (current.error == ErrorCode::kQuorumUnavailable) return current;
  const auto version = logical_clock_.fetch_add(1) + 1;
  return WriteRecord(key, Record{value, Version{version, node_id_}, false});
}

Result ReplicationCoordinator::Delete(const std::string& key) {
  const auto current = Get(key);
  if (current.error == ErrorCode::kQuorumUnavailable) return current;
  const auto version = logical_clock_.fetch_add(1) + 1;
  return WriteRecord(key, Record{"", Version{version, node_id_}, true});
}

Result ReplicationCoordinator::WriteRecord(const std::string& key, const Record& record) {
  std::vector<std::future<bool>> futures;
  for (const auto& id : ReplicaIds(key)) {
    const auto peer = FindPeer(id);
    futures.push_back(pool_.Submit([this, peer, key, record] {
      if (!peer) return false;
      return peer->id == node_id_ ? ApplyReplica(key, record)
                                  : client_.Write(*peer, key, record, timeout_);
    }));
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  std::size_t acknowledgements = 0;
  for (auto& future : futures) {
    if (future.wait_until(deadline) == std::future_status::ready) {
      try {
        if (future.get()) ++acknowledgements;
      } catch (const std::exception&) {
      }
    } else
      metrics_.ReplicaTimeout();
  }
  const bool success = acknowledgements >= write_quorum_;
  metrics_.Quorum(success);
  return {success ? ErrorCode::kNone : ErrorCode::kQuorumUnavailable,
          success ? std::optional<Record>(record) : std::nullopt, acknowledgements,
          success ? "write quorum reached" : "write quorum unavailable"};
}

Result ReplicationCoordinator::Get(const std::string& key) {
  struct ReadWithId {
    std::string id;
    ReplicaRead read;
  };
  std::vector<std::future<ReadWithId>> futures;
  for (const auto& id : ReplicaIds(key)) {
    const auto peer = FindPeer(id);
    futures.push_back(pool_.Submit([this, peer, id, key] {
      if (!peer) return ReadWithId{id, {}};
      if (peer->id == node_id_) return ReadWithId{id, {true, storage_.Get(key)}};
      return ReadWithId{id, client_.Read(*peer, key, timeout_)};
    }));
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  std::vector<ReadWithId> responses;
  for (auto& future : futures) {
    if (future.wait_until(deadline) == std::future_status::ready) {
      try {
        auto response = future.get();
        if (response.read.responded) responses.push_back(std::move(response));
      } catch (const std::exception&) {
      }
    } else
      metrics_.ReplicaTimeout();
  }
  if (responses.size() < read_quorum_) {
    metrics_.Quorum(false);
    return {ErrorCode::kQuorumUnavailable, std::nullopt, responses.size(),
            "read quorum unavailable"};
  }
  metrics_.Quorum(true);
  std::optional<Record> newest;
  for (const auto& response : responses)
    if (response.read.record)
      newest = newest ? std::optional<Record>(Newer(*newest, *response.read.record))
                      : response.read.record;
  if (!newest) return {ErrorCode::kNotFound, std::nullopt, responses.size(), "key not found"};
  auto clock = logical_clock_.load();
  while (clock < newest->version.counter &&
         !logical_clock_.compare_exchange_weak(clock, newest->version.counter)) {
  }
  for (const auto& response : responses) {
    if (!response.read.record || !(*response.read.record == *newest)) {
      const auto peer = FindPeer(response.id);
      if (peer) {
        metrics_.ReadRepair();
        try {
          static_cast<void>(pool_.Submit([this, peer, key, newest] {
            if (peer->id == node_id_)
              ApplyReplica(key, *newest);
            else
              static_cast<void>(client_.Write(*peer, key, *newest, timeout_));
          }));
        } catch (const std::exception&) {
        }
      }
    }
  }
  if (newest->tombstone) return {ErrorCode::kNotFound, newest, responses.size(), "key deleted"};
  return {ErrorCode::kNone, newest, responses.size(), "read quorum reached"};
}

}  // namespace kv
