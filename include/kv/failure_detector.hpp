#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "kv/types.hpp"

namespace kv {

enum class HealthState { kHealthy, kSuspect, kUnavailable };
std::string HealthName(HealthState state);

class FailureDetector {
 public:
  using Probe = std::function<bool(const Peer&)>;
  FailureDetector(std::string self, std::vector<Peer> peers, std::chrono::milliseconds interval,
                  std::size_t threshold, Probe probe);
  ~FailureDetector();
  void Start();
  void Stop();
  void Observe(const std::string& id, bool success);
  [[nodiscard]] HealthState State(const std::string& id) const;
  [[nodiscard]] std::tuple<std::size_t, std::size_t, std::size_t> Counts() const;

 private:
  void Run();
  std::string self_;
  std::vector<Peer> peers_;
  std::chrono::milliseconds interval_;
  std::size_t threshold_;
  Probe probe_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::map<std::string, std::pair<HealthState, std::size_t>> states_;
  std::thread thread_;
  bool stopping_{false};
};

}  // namespace kv
