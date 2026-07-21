#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "kv/config.hpp"
#include "kv/failure_detector.hpp"
#include "kv/hash_ring.hpp"
#include "kv/metrics.hpp"
#include "kv/storage_engine.hpp"
#include "kv/types.hpp"

namespace {
class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("kv-unit-" +
             std::to_string(kv::StableHash(kv::NowIso8601() + std::to_string(sequence_++))));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  std::filesystem::path path() const { return path_; }

 private:
  inline static std::atomic<std::uint64_t> sequence_{0};
  std::filesystem::path path_;
};
}  // namespace

TEST(Hashing, DeterministicKnownVector) {
  EXPECT_EQ(kv::StableHash("hello"), 0xe9c562c0fdb23244ULL);
  EXPECT_EQ(kv::StableHash("hello"), kv::StableHash("hello"));
  EXPECT_NE(kv::StableHash("hello"), kv::StableHash("world"));
}

TEST(HashRing, VirtualNodesReplicasAndDistribution) {
  kv::HashRing ring(256);
  ring.SetNodes({"c", "a", "b"});
  EXPECT_EQ(ring.PointCount(), 768U);
  const auto replicas = ring.Replicas("customer:42", 3);
  EXPECT_EQ(std::set<std::string>(replicas.begin(), replicas.end()).size(), 3U);
  std::map<std::string, int> counts;
  for (int i = 0; i < 10000; ++i) ++counts[ring.Owner("key-" + std::to_string(i))];
  for (const auto& count : counts) EXPECT_GT(count.second, 2000);
}

TEST(HashRing, AddingNodeMinimallyRemapsKeys) {
  kv::HashRing before(256), after(256);
  before.SetNodes({"a", "b", "c"});
  after.SetNodes({"a", "b", "c", "d"});
  int changed = 0;
  for (int i = 0; i < 10000; ++i)
    if (before.Owner(std::to_string(i)) != after.Owner(std::to_string(i))) ++changed;
  EXPECT_GT(changed, 1000);
  EXPECT_LT(changed, 4000);
}

TEST(Versioning, OrdersAndResolvesDeterministically) {
  kv::Record old{"old", {4, "z"}, false}, newer{"new", {5, "a"}, false};
  EXPECT_EQ(kv::Newer(old, newer), newer);
  kv::Record tie_a{"a", {5, "a"}, false}, tie_b{"b", {5, "b"}, false};
  EXPECT_EQ(kv::Newer(tie_a, tie_b), tie_b);
  kv::Record value{"value", {9, "n"}, false}, tombstone{"", {9, "n"}, true};
  EXPECT_TRUE(kv::Newer(value, tombstone).tombstone);
}

TEST(Storage, PutDeleteRecoverAndIgnoreStale) {
  TempDirectory temp;
  const auto wal = (temp.path() / "store.wal").string();
  {
    kv::StorageEngine storage(wal);
    EXPECT_TRUE(storage.PutIfNewer("key", {"one", {1, "a"}, false}));
    EXPECT_FALSE(storage.PutIfNewer("key", {"stale", {0, "z"}, false}));
    EXPECT_TRUE(storage.PutIfNewer("key", {"", {2, "a"}, true}));
    EXPECT_EQ(storage.Size(), 0U);
    EXPECT_TRUE(storage.Get("key")->tombstone);
  }
  kv::StorageEngine recovered(wal);
  ASSERT_TRUE(recovered.Get("key"));
  EXPECT_TRUE(recovered.Get("key")->tombstone);
  EXPECT_EQ(recovered.RecoveredRecords(), 2U);
}

TEST(Storage, ConcurrentAccessIsSafe) {
  TempDirectory temp;
  kv::StorageEngine storage((temp.path() / "store.wal").string());
  std::vector<std::thread> threads;
  for (std::uint64_t thread = 0; thread < 8; ++thread)
    threads.emplace_back([&, thread] {
      for (std::uint64_t i = 0; i < 100; ++i)
        storage.PutIfNewer("key-" + std::to_string(i),
                           {std::to_string(thread), {thread + 1, std::to_string(thread)}, false});
    });
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(storage.Size(), 100U);
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(storage.Get("key-" + std::to_string(i))->version.counter, 8U);
}

TEST(Wal, TruncatedAndCorruptTailAreIgnored) {
  TempDirectory temp;
  const auto path = temp.path() / "store.wal";
  {
    kv::StorageEngine storage(path.string());
    storage.PutIfNewer("safe", {"value", {1, "a"}, false});
  }
  {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << "KVW";
  }
  kv::StorageEngine truncated(path.string());
  ASSERT_TRUE(truncated.Get("safe"));
  EXPECT_EQ(truncated.RecoveredRecords(), 1U);
  {
    std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
    output.seekp(16);
    output.put('X');
  }
  kv::StorageEngine corrupt(path.string());
  EXPECT_FALSE(corrupt.Get("safe"));
  EXPECT_EQ(corrupt.RecoveredRecords(), 0U);
}

TEST(FailureDetector, StateTransitionsAndRecovery) {
  kv::FailureDetector detector("a", {{"a", "x", 1}, {"b", "x", 2}}, std::chrono::seconds(1), 2,
                               [](const kv::Peer&) { return true; });
  detector.Observe("b", false);
  EXPECT_EQ(detector.State("b"), kv::HealthState::kSuspect);
  detector.Observe("b", false);
  EXPECT_EQ(detector.State("b"), kv::HealthState::kUnavailable);
  detector.Observe("b", true);
  EXPECT_EQ(detector.State("b"), kv::HealthState::kHealthy);
}

TEST(Metrics, AccountsOperationsAndQuorums) {
  kv::Metrics metrics;
  metrics.Request(kv::Operation::kPut, true, 12);
  metrics.Quorum(true);
  metrics.ReadRepair();
  const auto text = metrics.Prometheus(1, 2, 3, 1, 0, 0);
  EXPECT_NE(text.find("kv_requests_total{operation=\"put\"} 1"), std::string::npos);
  EXPECT_NE(text.find("kv_read_repairs_total 1"), std::string::npos);
}

TEST(Config, ValidatesQuorumOverlapAndMembership) {
  kv::Config config;
  config.node_id = "a";
  config.listen_port = 1;
  config.peers = {{"a", "x", 1}, {"b", "x", 2}, {"c", "x", 3}};
  EXPECT_NO_THROW(config.Validate());
  config.read_quorum = 1;
  EXPECT_THROW(config.Validate(), std::invalid_argument);
}
