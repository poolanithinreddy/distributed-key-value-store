#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "kv/http.hpp"
#include "kv/types.hpp"

namespace {
struct Results {
  std::uint64_t success{0};
  std::uint64_t errors{0};
  std::vector<std::uint64_t> latency_us;
};
std::uint64_t Percentile(const std::vector<std::uint64_t>& values, double percentile) {
  if (values.empty()) return 0;
  const auto index =
      static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 10) {
    std::cerr << "usage: kv_benchmark <host> <port> <workload:put|get|mixed> <clients> <warmup_s> "
                 "<duration_s> <keyspace> <value_bytes> <output.json>\n";
    return 2;
  }
  const std::string host = argv[1], workload = argv[3], output_path = argv[9];
  const auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
  const auto clients = static_cast<std::size_t>(std::stoull(argv[4]));
  const auto warmup = std::chrono::seconds(std::stoll(argv[5]));
  const auto duration = std::chrono::seconds(std::stoll(argv[6]));
  const auto keyspace = static_cast<std::size_t>(std::stoull(argv[7]));
  const auto value_bytes = static_cast<std::size_t>(std::stoull(argv[8]));
  if ((workload != "put" && workload != "get" && workload != "mixed") || clients == 0 ||
      keyspace == 0)
    return 2;
  const std::string value(value_bytes, 'x');
  const auto cluster =
      kv::HttpCall(host, port, "GET", "/v1/cluster", "", std::chrono::milliseconds(3000));
  if (!cluster.transport_ok || cluster.status != 200) {
    std::cerr << "cannot read cluster metadata\n";
    return 1;
  }
  if (workload != "put") {
    for (std::size_t i = 0; i < keyspace; ++i) {
      const auto response =
          kv::HttpCall(host, port, "PUT", "/v1/kv/bench-" + std::to_string(i),
                       "{\"value\":\"" + value + "\"}", std::chrono::milliseconds(3000));
      if (!response.transport_ok || response.status / 100 != 2) {
        std::cerr << "prefill failed at key " << i << '\n';
        return 1;
      }
    }
  }
  std::atomic<bool> measuring{false}, stopping{false};
  std::mutex results_mutex;
  Results results;
  auto worker = [&](std::size_t id) {
    std::mt19937_64 random(0xC0FFEEULL + id);
    std::uniform_int_distribution<std::size_t> keys(0, keyspace - 1);
    std::uniform_int_distribution<int> mix(0, 99);
    while (!stopping.load(std::memory_order_relaxed)) {
      const auto key = keys(random);
      const bool put = workload == "put" || (workload == "mixed" && mix(random) >= 80);
      const auto started = std::chrono::steady_clock::now();
      const auto response =
          kv::HttpCall(host, port, put ? "PUT" : "GET", "/v1/kv/bench-" + std::to_string(key),
                       put ? "{\"value\":\"" + value + "\"}" : "", std::chrono::milliseconds(3000));
      const auto elapsed =
          static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count());
      if (measuring.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(results_mutex);
        if (response.transport_ok && response.status / 100 == 2)
          ++results.success;
        else
          ++results.errors;
        results.latency_us.push_back(elapsed);
      }
    }
  };
  std::vector<std::thread> threads;
  threads.reserve(clients);
  for (std::size_t i = 0; i < clients; ++i) threads.emplace_back(worker, i);
  std::this_thread::sleep_for(warmup);
  measuring = true;
  const auto measured_start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(duration);
  measuring = false;
  stopping = true;
  for (auto& thread : threads) thread.join();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - measured_start).count();
  std::sort(results.latency_us.begin(), results.latency_us.end());
  const double throughput = static_cast<double>(results.success + results.errors) / seconds;
  std::ofstream output(output_path);
  utsname system{};
  uname(&system);
  const auto pages = sysconf(_SC_PHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGESIZE);
#if defined(__clang__)
  const std::string compiler = "Clang " + std::string(__clang_version__);
#elif defined(__GNUC__)
  const std::string compiler = "GCC " + std::string(__VERSION__);
#else
  const std::string compiler = "unknown";
#endif
  output << "{\n  \"date\": \"" << kv::NowIso8601() << "\",\n  \"git_commit\": \"" << KV_GIT_COMMIT
         << "\",\n  \"os\": \"" << kv::JsonEscape(system.sysname) << ' '
         << kv::JsonEscape(system.release) << ' ' << kv::JsonEscape(system.machine)
         << "\",\n  \"logical_cpus\": " << std::thread::hardware_concurrency()
         << ",\n  \"memory_bytes\": " << (pages > 0 && page_size > 0 ? pages * page_size : 0)
         << ",\n  \"compiler\": \"" << kv::JsonEscape(compiler)
#ifdef NDEBUG
         << "\",\n  \"build_type\": \"Release\""
#else
         << "\",\n  \"build_type\": \"Debug\""
#endif
         << ",\n  \"transport\": \"localhost native processes\",\n  \"cluster\": " << cluster.body
         << ",\n  \"workload\": \"" << workload << "\",\n  \"clients\": " << clients
         << ",\n  \"warmup_seconds\": " << warmup.count()
         << ",\n  \"measured_seconds\": " << seconds << ",\n  \"keyspace\": " << keyspace
         << ",\n  \"value_bytes\": " << value_bytes << ",\n  \"success\": " << results.success
         << ",\n  \"errors\": " << results.errors
         << ",\n  \"throughput_ops_per_second\": " << throughput
         << ",\n  \"latency_us\": {\"p50\": " << Percentile(results.latency_us, 0.50)
         << ", \"p95\": " << Percentile(results.latency_us, 0.95)
         << ", \"p99\": " << Percentile(results.latency_us, 0.99)
         << ", \"max\": " << (results.latency_us.empty() ? 0 : results.latency_us.back())
         << "}\n}\n";
  std::cout << "throughput=" << throughput << " ops/s p95=" << Percentile(results.latency_us, 0.95)
            << "us success=" << results.success << " errors=" << results.errors << '\n';
  // Transport and quorum failures are measured outcomes, not harness failures.
  return 0;
}
