#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace kv {

enum class Operation : std::size_t { kGet = 0, kPut = 1, kDelete = 2 };

class Metrics {
 public:
  class InFlight {
   public:
    explicit InFlight(Metrics& metrics) : metrics_(metrics) { metrics_.in_flight_.fetch_add(1); }
    ~InFlight() { metrics_.in_flight_.fetch_sub(1); }

   private:
    Metrics& metrics_;
  };

  void Request(Operation operation, bool success, std::uint64_t latency_us);
  void Quorum(bool success);
  void ReplicaTimeout() { replica_timeouts_.fetch_add(1); }
  void ReadRepair() { read_repairs_.fetch_add(1); }
  [[nodiscard]] std::string Prometheus(std::size_t keys, std::uint64_t wal_writes,
                                       std::size_t recovered, std::size_t healthy,
                                       std::size_t suspect, std::size_t unavailable) const;
  [[nodiscard]] std::string Json(std::size_t keys) const;

 private:
  friend class InFlight;
  std::array<std::atomic<std::uint64_t>, 3> requests_{};
  std::array<std::atomic<std::uint64_t>, 3> successes_{};
  std::array<std::atomic<std::uint64_t>, 3> failures_{};
  std::atomic<std::uint64_t> latency_us_{0};
  std::atomic<std::uint64_t> max_latency_us_{0};
  std::atomic<std::uint64_t> quorum_success_{0};
  std::atomic<std::uint64_t> quorum_failure_{0};
  std::atomic<std::uint64_t> replica_timeouts_{0};
  std::atomic<std::uint64_t> read_repairs_{0};
  std::atomic<std::uint64_t> in_flight_{0};
};

}  // namespace kv
