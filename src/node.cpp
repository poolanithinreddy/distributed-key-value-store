#include "kv/node.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>

#include "kv/hash_ring.hpp"

namespace kv {
namespace {
std::mutex log_mutex;
std::optional<std::string> ValueFromBody(const std::string& body) {
  std::smatch match;
  if (!std::regex_search(body, match,
                         std::regex("\\\"value\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"")))
    return std::nullopt;
  const std::string wrapped =
      "{\"value\":\"" + match[1].str() + "\",\"version\":0,\"origin\":\"x\",\"tombstone\":false}";
  const auto parsed = RecordFromJson(wrapped);
  return parsed ? std::optional<std::string>(parsed->value) : std::nullopt;
}
std::string Join(const std::vector<std::string>& values) {
  std::string out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) out += ',';
    out += values[i];
  }
  return out;
}
}  // namespace

Node::Node(Config config)
    : config_(std::move(config)),
      storage_((std::filesystem::path(config_.data_dir) / "store.wal").string()),
      replication_pool_(config_.worker_threads, config_.queue_capacity),
      replication_(config_.node_id, config_.peers, config_.virtual_nodes,
                   config_.replication_factor, config_.read_quorum, config_.write_quorum,
                   config_.request_timeout, storage_, peer_client_, replication_pool_, metrics_) {
  detector_ = std::make_unique<FailureDetector>(
      config_.node_id, config_.peers, config_.health_interval, config_.failure_threshold,
      [this](const Peer& peer) { return peer_client_.Healthy(peer, config_.request_timeout); });
  server_ = std::make_unique<HttpServer>(
      config_.listen_host, config_.listen_port, config_.worker_threads, config_.queue_capacity,
      [this](const HttpRequest& request) { return Handle(request); });
}
Node::~Node() { Stop(); }

void Node::Start() {
  if (running_.exchange(true)) return;
  try {
    server_->Start();
    detector_->Start();
  } catch (...) {
    running_ = false;
    throw;
  }
  std::ostringstream line;
  line << "{\"timestamp\":\"" << NowIso8601() << "\",\"node_id\":\"" << JsonEscape(config_.node_id)
       << "\",\"event\":\"started\",\"port\":" << config_.listen_port
       << ",\"recovered_records\":" << storage_.RecoveredRecords() << "}\n";
  std::lock_guard<std::mutex> lock(log_mutex);
  std::cerr << line.str();
}
void Node::Stop() {
  if (!running_.exchange(false)) return;
  detector_->Stop();
  server_->Stop();
  replication_pool_.Stop();
}

HttpResponse Node::Error(int status, ErrorCode code, const std::string& message) const {
  return {status, "application/json",
          "{\"error\":\"" + ErrorName(code) + "\",\"message\":\"" + JsonEscape(message) + "\"}"};
}

void Node::Log(const std::string& request_id, const std::string& operation, const std::string& key,
               const Result& result, std::uint64_t latency_us) const {
  std::ostringstream line;
  line << "{\"timestamp\":\"" << NowIso8601() << "\",\"node_id\":\"" << JsonEscape(config_.node_id)
       << "\",\"request_id\":\"" << request_id << "\",\"operation\":\"" << operation
       << "\",\"key_hash\":\"" << std::hex << StableHash(key) << std::dec << "\",\"replicas\":\""
       << Join(replication_.ReplicaIds(key)) << "\",\"result\":\"" << ErrorName(result.error)
       << "\",\"acknowledgements\":" << result.acknowledgements << ",\"latency_us\":" << latency_us
       << "}\n";
  std::lock_guard<std::mutex> lock(log_mutex);
  std::cerr << line.str();
}

HttpResponse Node::Handle(const HttpRequest& request) {
  if (request.target == "/health") return {200, "application/json", "{\"status\":\"healthy\"}"};
  if (request.target == "/ready")
    return running_.load() ? HttpResponse{200, "application/json", "{\"status\":\"ready\"}"}
                           : HttpResponse{503, "application/json", "{\"status\":\"not_ready\"}"};
  if (request.target == "/metrics") {
    const auto [healthy, suspect, unavailable] = detector_->Counts();
    return {200, "text/plain; version=0.0.4",
            metrics_.Prometheus(storage_.Size(), storage_.WalWrites(), storage_.RecoveredRecords(),
                                healthy, suspect, unavailable)};
  }
  if (request.target == "/v1/stats")
    return {200, "application/json", metrics_.Json(storage_.Size())};
  if (request.target == "/v1/cluster") {
    std::ostringstream body;
    body << "{\"node_id\":\"" << JsonEscape(config_.node_id)
         << "\",\"N\":" << config_.replication_factor << ",\"R\":" << config_.read_quorum
         << ",\"W\":" << config_.write_quorum << ",\"nodes\":[";
    for (std::size_t i = 0; i < config_.peers.size(); ++i) {
      if (i != 0) body << ',';
      const auto& peer = config_.peers[i];
      body << "{\"id\":\"" << JsonEscape(peer.id) << "\",\"host\":\"" << JsonEscape(peer.host)
           << "\",\"port\":" << peer.port << ",\"state\":\""
           << HealthName(detector_->State(peer.id)) << "\"}";
    }
    body << "]}";
    return {200, "application/json", body.str()};
  }

  constexpr const char* internal_prefix = "/internal/kv/";
  if (request.target.rfind(internal_prefix, 0) == 0) {
    const auto key =
        UrlDecode(request.target.substr(std::char_traits<char>::length(internal_prefix)));
    if (key.empty()) return Error(400, ErrorCode::kBadRequest, "key cannot be empty");
    if (request.method == "PUT") {
      const auto record = RecordFromJson(request.body);
      if (!record) return Error(400, ErrorCode::kBadRequest, "invalid replica record");
      replication_.ApplyReplica(key, *record);
      return {200, "application/json", "{\"status\":\"applied\"}"};
    }
    if (request.method == "GET") {
      const auto record = storage_.Get(key);
      return record ? HttpResponse{200, "application/json", RecordToJson(*record)}
                    : Error(404, ErrorCode::kNotFound, "key not found");
    }
    return Error(405, ErrorCode::kBadRequest, "method not allowed");
  }

  constexpr const char* public_prefix = "/v1/kv/";
  if (request.target.rfind(public_prefix, 0) != 0)
    return Error(404, ErrorCode::kNotFound, "route not found");
  const auto key = UrlDecode(request.target.substr(std::char_traits<char>::length(public_prefix)));
  if (key.empty() || key.size() > 1024)
    return Error(400, ErrorCode::kBadRequest, "key length must be 1..1024 bytes");
  Metrics::InFlight in_flight(metrics_);
  const auto started = std::chrono::steady_clock::now();
  Result result;
  Operation operation;
  std::string operation_name;
  if (request.method == "PUT") {
    operation = Operation::kPut;
    operation_name = "PUT";
    const auto value = ValueFromBody(request.body);
    if (!value || value->size() > 4U * 1024U * 1024U)
      return Error(400, ErrorCode::kBadRequest, "JSON value string required (maximum 4 MiB)");
    result = replication_.Put(key, *value);
  } else if (request.method == "GET") {
    operation = Operation::kGet;
    operation_name = "GET";
    result = replication_.Get(key);
  } else if (request.method == "DELETE") {
    operation = Operation::kDelete;
    operation_name = "DELETE";
    result = replication_.Delete(key);
  } else
    return Error(405, ErrorCode::kBadRequest, "method not allowed");
  const auto elapsed =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
  metrics_.Request(operation, result.ok() || result.error == ErrorCode::kNotFound, elapsed);
  const auto request_id =
      config_.node_id + '-' + std::to_string(request_sequence_.fetch_add(1) + 1);
  Log(request_id, operation_name, key, result, elapsed);
  if (result.error == ErrorCode::kQuorumUnavailable)
    return Error(503, result.error, result.message);
  if (result.error == ErrorCode::kNotFound) return Error(404, result.error, result.message);
  if (!result.ok()) return Error(500, result.error, result.message);
  if (request.method == "GET")
    return {200, "application/json",
            "{\"key\":\"" + JsonEscape(key) + "\",\"value\":\"" + JsonEscape(result.record->value) +
                "\",\"version\":" + std::to_string(result.record->version.counter) +
                ",\"origin\":\"" + JsonEscape(result.record->version.origin) + "\"}"};
  return {request.method == "PUT" ? 201 : 200, "application/json",
          "{\"status\":\"ok\",\"acknowledgements\":" + std::to_string(result.acknowledgements) +
              ",\"version\":" + std::to_string(result.record->version.counter) + "}"};
}

}  // namespace kv
