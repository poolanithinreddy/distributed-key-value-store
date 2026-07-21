#include "kv/metrics.hpp"

#include <algorithm>
#include <sstream>

namespace kv {

void Metrics::Request(Operation operation, bool success, std::uint64_t latency_us) {
  const auto index = static_cast<std::size_t>(operation);
  requests_[index].fetch_add(1);
  (success ? successes_[index] : failures_[index]).fetch_add(1);
  latency_us_.fetch_add(latency_us);
  auto maximum = max_latency_us_.load();
  while (maximum < latency_us && !max_latency_us_.compare_exchange_weak(maximum, latency_us)) {
  }
}

void Metrics::Quorum(bool success) { (success ? quorum_success_ : quorum_failure_).fetch_add(1); }

std::string Metrics::Prometheus(std::size_t keys, std::uint64_t wal_writes, std::size_t recovered,
                                std::size_t healthy, std::size_t suspect,
                                std::size_t unavailable) const {
  std::ostringstream out;
  static constexpr std::array<const char*, 3> names{"get", "put", "delete"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    out << "kv_requests_total{operation=\"" << names[i] << "\"} " << requests_[i].load() << '\n';
    out << "kv_requests_success_total{operation=\"" << names[i] << "\"} " << successes_[i].load()
        << '\n';
    out << "kv_requests_failure_total{operation=\"" << names[i] << "\"} " << failures_[i].load()
        << '\n';
  }
  out << "kv_request_latency_microseconds_sum " << latency_us_.load() << '\n'
      << "kv_request_latency_microseconds_max " << max_latency_us_.load() << '\n'
      << "kv_quorum_success_total " << quorum_success_.load() << '\n'
      << "kv_quorum_failure_total " << quorum_failure_.load() << '\n'
      << "kv_replica_timeouts_total " << replica_timeouts_.load() << '\n'
      << "kv_read_repairs_total " << read_repairs_.load() << '\n'
      << "kv_in_flight_requests " << in_flight_.load() << '\n'
      << "kv_keys " << keys << '\n'
      << "kv_wal_writes_total " << wal_writes << '\n'
      << "kv_wal_recovered_records " << recovered << '\n'
      << "kv_nodes{state=\"healthy\"} " << healthy << '\n'
      << "kv_nodes{state=\"suspect\"} " << suspect << '\n'
      << "kv_nodes{state=\"unavailable\"} " << unavailable << '\n';
  return out.str();
}

std::string Metrics::Json(std::size_t keys) const {
  return "{\"requests\":" +
         std::to_string(requests_[0].load() + requests_[1].load() + requests_[2].load()) +
         ",\"quorum_success\":" + std::to_string(quorum_success_.load()) +
         ",\"quorum_failure\":" + std::to_string(quorum_failure_.load()) +
         ",\"read_repairs\":" + std::to_string(read_repairs_.load()) +
         ",\"replica_timeouts\":" + std::to_string(replica_timeouts_.load()) +
         ",\"keys\":" + std::to_string(keys) + "}";
}

}  // namespace kv
