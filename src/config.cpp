#include "kv/config.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace kv {
namespace {
std::string ReadAll(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read config: " + path);
  std::ostringstream content;
  content << input.rdbuf();
  return content.str();
}

std::string StringField(const std::string& text, const std::string& name,
                        const std::string& fallback = "") {
  std::smatch match;
  const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
  return std::regex_search(text, match, pattern) ? match[1].str() : fallback;
}

std::uint64_t NumberField(const std::string& text, const std::string& name,
                          std::uint64_t fallback) {
  std::smatch match;
  const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*([0-9]+)");
  return std::regex_search(text, match, pattern) ? std::stoull(match[1].str()) : fallback;
}
}  // namespace

void Config::Validate() const {
  if (node_id.empty()) throw std::invalid_argument("node_id is required");
  if (listen_port == 0) throw std::invalid_argument("listen_port must be nonzero");
  if (peers.empty()) throw std::invalid_argument("at least one peer is required");
  std::unordered_set<std::string> ids;
  bool self_found = false;
  for (const auto& peer : peers) {
    if (peer.id.empty() || peer.host.empty() || peer.port == 0)
      throw std::invalid_argument("invalid peer");
    if (!ids.insert(peer.id).second) throw std::invalid_argument("duplicate peer id: " + peer.id);
    self_found = self_found || peer.id == node_id;
  }
  if (!self_found) throw std::invalid_argument("peers must contain this node");
  if (replication_factor == 0 || replication_factor > peers.size())
    throw std::invalid_argument("replication_factor outside cluster size");
  if (read_quorum == 0 || read_quorum > replication_factor || write_quorum == 0 ||
      write_quorum > replication_factor)
    throw std::invalid_argument("invalid quorum");
  if (read_quorum + write_quorum <= replication_factor)
    throw std::invalid_argument("R + W must be greater than N for overlap");
  if (virtual_nodes == 0 || worker_threads == 0 || queue_capacity == 0 || failure_threshold == 0)
    throw std::invalid_argument("positive sizing values required");
}

Config Config::Load(const std::string& path) {
  const auto text = ReadAll(path);
  Config config;
  config.node_id = StringField(text, "node_id");
  config.listen_host = StringField(text, "listen_host", config.listen_host);
  config.listen_port = static_cast<std::uint16_t>(NumberField(text, "listen_port", 0));
  config.data_dir = StringField(text, "data_dir", config.data_dir);
  config.virtual_nodes =
      static_cast<std::size_t>(NumberField(text, "virtual_nodes", config.virtual_nodes));
  config.replication_factor =
      static_cast<std::size_t>(NumberField(text, "replication_factor", config.replication_factor));
  config.read_quorum =
      static_cast<std::size_t>(NumberField(text, "read_quorum", config.read_quorum));
  config.write_quorum =
      static_cast<std::size_t>(NumberField(text, "write_quorum", config.write_quorum));
  config.worker_threads =
      static_cast<std::size_t>(NumberField(text, "worker_threads", config.worker_threads));
  config.queue_capacity =
      static_cast<std::size_t>(NumberField(text, "queue_capacity", config.queue_capacity));
  config.request_timeout = std::chrono::milliseconds(NumberField(text, "request_timeout_ms", 500));
  config.health_interval = std::chrono::milliseconds(NumberField(text, "health_interval_ms", 500));
  config.failure_threshold =
      static_cast<std::size_t>(NumberField(text, "failure_threshold", config.failure_threshold));
  const std::regex peer_pattern(
      R"peer(\{\s*"id"\s*:\s*"([^"]+)"\s*,\s*"host"\s*:\s*"([^"]+)"\s*,\s*"port"\s*:\s*([0-9]+)\s*\})peer");
  for (std::sregex_iterator it(text.begin(), text.end(), peer_pattern), end; it != end; ++it) {
    config.peers.push_back(Peer{(*it)[1].str(), (*it)[2].str(),
                                static_cast<std::uint16_t>(std::stoul((*it)[3].str()))});
  }
  config.Validate();
  return config;
}

}  // namespace kv
