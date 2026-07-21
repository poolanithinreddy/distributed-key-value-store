#pragma once

#include <atomic>
#include <memory>

#include "kv/config.hpp"
#include "kv/failure_detector.hpp"
#include "kv/http.hpp"
#include "kv/metrics.hpp"
#include "kv/peer_client.hpp"
#include "kv/replication.hpp"
#include "kv/storage_engine.hpp"
#include "kv/thread_pool.hpp"

namespace kv {

class Node {
 public:
  explicit Node(Config config);
  ~Node();
  void Start();
  void Stop();
  [[nodiscard]] bool Running() const { return running_.load(); }
  HttpResponse Handle(const HttpRequest& request);

 private:
  HttpResponse Error(int status, ErrorCode code, const std::string& message) const;
  void Log(const std::string& request_id, const std::string& operation, const std::string& key,
           const Result& result, std::uint64_t latency_us) const;
  Config config_;
  StorageEngine storage_;
  Metrics metrics_;
  HttpPeerClient peer_client_;
  ThreadPool replication_pool_;
  ReplicationCoordinator replication_;
  std::unique_ptr<FailureDetector> detector_;
  std::unique_ptr<HttpServer> server_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> request_sequence_{0};
};

}  // namespace kv
