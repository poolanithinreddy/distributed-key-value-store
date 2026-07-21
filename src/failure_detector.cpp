#include "kv/failure_detector.hpp"

#include <stdexcept>

namespace kv {

std::string HealthName(HealthState state) {
  if (state == HealthState::kHealthy) return "healthy";
  if (state == HealthState::kSuspect) return "suspect";
  return "unavailable";
}

FailureDetector::FailureDetector(std::string self, std::vector<Peer> peers,
                                 std::chrono::milliseconds interval, std::size_t threshold,
                                 Probe probe)
    : self_(std::move(self)),
      peers_(std::move(peers)),
      interval_(interval),
      threshold_(threshold),
      probe_(std::move(probe)) {
  if (threshold_ == 0) throw std::invalid_argument("failure threshold must be positive");
  for (const auto& peer : peers_) states_[peer.id] = {HealthState::kHealthy, 0};
}

FailureDetector::~FailureDetector() { Stop(); }
void FailureDetector::Start() {
  if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
}
void FailureDetector::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void FailureDetector::Observe(const std::string& id, bool success) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& entry = states_[id];
  if (success) {
    entry = {HealthState::kHealthy, 0};
    return;
  }
  ++entry.second;
  entry.first = entry.second >= threshold_ ? HealthState::kUnavailable : HealthState::kSuspect;
}

HealthState FailureDetector::State(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = states_.find(id);
  return found == states_.end() ? HealthState::kUnavailable : found->second.first;
}

std::tuple<std::size_t, std::size_t, std::size_t> FailureDetector::Counts() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::size_t healthy = 0, suspect = 0, unavailable = 0;
  for (const auto& item : states_) {
    if (item.second.first == HealthState::kHealthy)
      ++healthy;
    else if (item.second.first == HealthState::kSuspect)
      ++suspect;
    else
      ++unavailable;
  }
  return {healthy, suspect, unavailable};
}

void FailureDetector::Run() {
  while (true) {
    for (const auto& peer : peers_)
      if (peer.id != self_) Observe(peer.id, probe_(peer));
    std::unique_lock<std::mutex> lock(mutex_);
    if (wake_.wait_for(lock, interval_, [this] { return stopping_; })) return;
  }
}

}  // namespace kv
