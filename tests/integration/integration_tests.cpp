#include <gtest/gtest.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "kv/hash_ring.hpp"
#include "kv/http.hpp"
#include "kv/types.hpp"

namespace {
class Cluster : public testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_NE(executable, nullptr); }
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("kv-integration-" + std::to_string(kv::StableHash(kv::NowIso8601())));
    std::filesystem::create_directories(root);
    for (int i = 0; i < 3; ++i) WriteConfig(i);
    for (int i = 0; i < 3; ++i) Start(i);
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(WaitReady(i));
  }
  void TearDown() override {
    for (int i = 0; i < 3; ++i) Stop(i);
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
  void WriteConfig(int index) {
    std::ofstream out(root / ("node" + std::to_string(index) + ".json"));
    out << "{\"node_id\":\"node" << index
        << "\",\"listen_host\":\"127.0.0.1\",\"listen_port\":" << 19221 + index
        << ",\"data_dir\":\"" << (root / ("data" + std::to_string(index))).string()
        << "\",\"virtual_nodes\":64,\"replication_factor\":3,\"read_quorum\":2,\"write_quorum\":2,"
           "\"worker_threads\":6,\"queue_capacity\":128,\"request_timeout_ms\":200,\"health_"
           "interval_ms\":100,\"failure_threshold\":2,\"peers\":[";
    for (int peer = 0; peer < 3; ++peer) {
      if (peer != 0) out << ',';
      out << "{\"id\":\"node" << peer << "\",\"host\":\"127.0.0.1\",\"port\":" << 19221 + peer
          << '}';
    }
    out << "]}";
  }
  void Start(int index) {
    const auto config = (root / ("node" + std::to_string(index) + ".json")).string();
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
      execl(executable, executable, config.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    pids[index] = child;
  }
  void Stop(int index) {
    if (pids[index] <= 0) return;
    kill(pids[index], SIGTERM);
    for (int attempt = 0; attempt < 30; ++attempt) {
      if (waitpid(pids[index], nullptr, WNOHANG) == pids[index]) {
        pids[index] = 0;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(pids[index], SIGKILL);
    waitpid(pids[index], nullptr, 0);
    pids[index] = 0;
  }
  bool WaitReady(int index) {
    for (int attempt = 0; attempt < 60; ++attempt) {
      const auto response = Call(index, "GET", "/ready");
      if (response.transport_ok && response.status == 200) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }
  kv::HttpClientResponse Call(int index, const std::string& method, const std::string& target,
                              const std::string& body = "") {
    return kv::HttpCall("127.0.0.1", static_cast<std::uint16_t>(19221 + index), method, target,
                        body, std::chrono::milliseconds(800));
  }
  inline static const char* executable = nullptr;
  std::filesystem::path root;
  pid_t pids[3]{0, 0, 0};

 public:
  static void SetExecutable(const char* value) { executable = value; }
};

TEST_F(Cluster, QuorumLifecyclePersistenceAndRepair) {
  auto response = Call(0, "PUT", "/v1/kv/customer", "{\"value\":\"first\"}");
  ASSERT_EQ(response.status, 201) << response.body;
  response = Call(1, "GET", "/v1/kv/customer");
  ASSERT_EQ(response.status, 200);
  EXPECT_NE(response.body.find("first"), std::string::npos);
  response = Call(2, "PUT", "/v1/kv/customer", "{\"value\":\"updated\"}");
  ASSERT_EQ(response.status, 201);
  response = Call(0, "GET", "/v1/kv/customer");
  ASSERT_EQ(response.status, 200);
  EXPECT_NE(response.body.find("updated"), std::string::npos);

  Stop(2);
  response = Call(0, "PUT", "/v1/kv/during-failure", "{\"value\":\"available\"}");
  ASSERT_EQ(response.status, 201) << response.body;
  Stop(1);
  response = Call(0, "PUT", "/v1/kv/no-quorum", "{\"value\":\"rejected\"}");
  EXPECT_EQ(response.status, 503);
  Start(1);
  ASSERT_TRUE(WaitReady(1));
  response = Call(1, "GET", "/v1/kv/customer");
  ASSERT_EQ(response.status, 200);
  EXPECT_NE(response.body.find("updated"), std::string::npos);
  Start(2);
  ASSERT_TRUE(WaitReady(2));

  response = Call(0, "DELETE", "/v1/kv/customer");
  ASSERT_EQ(response.status, 200);
  response = Call(2, "GET", "/v1/kv/customer");
  EXPECT_EQ(response.status, 404);
  Stop(0);
  Start(0);
  ASSERT_TRUE(WaitReady(0));
  response = Call(0, "GET", "/internal/kv/customer");
  ASSERT_EQ(response.status, 200);
  const auto record = kv::RecordFromJson(response.body);
  ASSERT_TRUE(record);
  EXPECT_TRUE(record->tombstone);

  response = Call(2, "PUT", "/internal/kv/repair",
                  "{\"value\":\"stale\",\"version\":1,\"origin\":\"node0\",\"tombstone\":false}");
  ASSERT_EQ(response.status, 200);
  response = Call(0, "PUT", "/v1/kv/repair", "{\"value\":\"fresh\"}");
  ASSERT_EQ(response.status, 201);
  response = Call(1, "GET", "/v1/kv/repair");
  ASSERT_EQ(response.status, 200);
  EXPECT_NE(response.body.find("fresh"), std::string::npos);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  response = Call(2, "GET", "/internal/kv/repair");
  ASSERT_EQ(response.status, 200);
  EXPECT_NE(response.body.find("fresh"), std::string::npos);

  std::atomic<int> concurrent_failures{0};
  std::vector<std::thread> clients;
  for (int client = 0; client < 16; ++client)
    clients.emplace_back([&, client] {
      for (int item = 0; item < 10; ++item) {
        const auto key = "/v1/kv/concurrent-" + std::to_string(client) + '-' + std::to_string(item);
        auto concurrent = Call(client % 3, "PUT", key, "{\"value\":\"safe\"}");
        if (concurrent.status != 201) {
          ++concurrent_failures;
          continue;
        }
        concurrent = Call((client + 1) % 3, "GET", key);
        if (concurrent.status != 200 || concurrent.body.find("safe") == std::string::npos)
          ++concurrent_failures;
      }
    });
  for (auto& client : clients) client.join();
  EXPECT_EQ(concurrent_failures.load(), 0);
}
}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  if (argc < 2) return 2;
  Cluster::SetExecutable(argv[1]);
  return RUN_ALL_TESTS();
}
