#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "kv/metrics.hpp"
#include "kv/replication.hpp"

namespace {
class FakeClient final : public kv::ReplicaClient {
 public:
  bool Write(const kv::Peer& peer, const std::string& key, const kv::Record& record,
             std::chrono::milliseconds) override {
    if (delay.count() > 0) std::this_thread::sleep_for(delay);
    std::lock_guard<std::mutex> lock(mutex);
    if (failed.count(peer.id)) return false;
    data[peer.id][key] = record;
    return true;
  }
  kv::ReplicaRead Read(const kv::Peer& peer, const std::string& key,
                       std::chrono::milliseconds) override {
    if (delay.count() > 0) std::this_thread::sleep_for(delay);
    std::lock_guard<std::mutex> lock(mutex);
    if (failed.count(peer.id)) return {};
    const auto found = data[peer.id].find(key);
    return {true,
            found == data[peer.id].end() ? std::nullopt : std::optional<kv::Record>(found->second)};
  }
  bool Healthy(const kv::Peer& peer, std::chrono::milliseconds) override {
    return failed.count(peer.id) == 0;
  }
  std::set<std::string> failed;
  std::chrono::milliseconds delay{0};
  std::map<std::string, std::map<std::string, kv::Record>> data;
  std::mutex mutex;
};

class CoordinatorTest : public testing::Test {
 protected:
  CoordinatorTest()
      : path(std::filesystem::temp_directory_path() /
             ("kv-failure-" + std::to_string(kv::StableHash(kv::NowIso8601())))),
        storage((path / "store.wal").string()),
        pool(6, 64),
        coordinator("a", {{"a", "x", 1}, {"b", "x", 2}, {"c", "x", 3}}, 64, 3, 2, 2,
                    std::chrono::milliseconds(40), storage, client, pool, metrics) {
    std::filesystem::create_directories(path);
  }
  ~CoordinatorTest() override {
    pool.Stop();
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  std::filesystem::path path;
  kv::StorageEngine storage;
  FakeClient client;
  kv::ThreadPool pool;
  kv::Metrics metrics;
  kv::ReplicationCoordinator coordinator;
};
}  // namespace

TEST_F(CoordinatorTest, OneReplicaFailureStillReachesQuorum) {
  client.failed.insert("c");
  const auto result = coordinator.Put("key", "value");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.acknowledgements, 2U);
}

TEST_F(CoordinatorTest, QuorumLossIsExplicit) {
  client.failed.insert("b");
  client.failed.insert("c");
  const auto result = coordinator.Put("key", "value");
  EXPECT_EQ(result.error, kv::ErrorCode::kQuorumUnavailable);
  EXPECT_EQ(result.acknowledgements, 1U);
}

TEST_F(CoordinatorTest, DelayedReplicasTimeOut) {
  client.delay = std::chrono::milliseconds(100);
  const auto result = coordinator.Put("key", "value");
  EXPECT_EQ(result.error, kv::ErrorCode::kQuorumUnavailable);
}

TEST_F(CoordinatorTest, StaleReplicaIsReadRepaired) {
  ASSERT_TRUE(coordinator.Put("key", "fresh").ok());
  {
    std::lock_guard<std::mutex> lock(client.mutex);
    client.data["b"]["key"] = {"stale", {0, "b"}, false};
  }
  const auto result = coordinator.Get("key");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.record->value, "fresh");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::lock_guard<std::mutex> lock(client.mutex);
  EXPECT_EQ(client.data["b"]["key"].value, "fresh");
}

TEST_F(CoordinatorTest, TombstoneWinsAndRepairsStaleValue) {
  ASSERT_TRUE(coordinator.Put("key", "value").ok());
  ASSERT_TRUE(coordinator.Delete("key").ok());
  {
    std::lock_guard<std::mutex> lock(client.mutex);
    client.data["c"]["key"] = {"stale", {1, "a"}, false};
  }
  const auto result = coordinator.Get("key");
  EXPECT_EQ(result.error, kv::ErrorCode::kNotFound);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::lock_guard<std::mutex> lock(client.mutex);
  EXPECT_TRUE(client.data["c"]["key"].tombstone);
}
