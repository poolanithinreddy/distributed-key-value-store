#include "kv/peer_client.hpp"

#include "kv/http.hpp"

namespace kv {

bool HttpPeerClient::Write(const Peer& peer, const std::string& key, const Record& record,
                           std::chrono::milliseconds timeout) {
  const auto response = HttpCall(peer.host, peer.port, "PUT", "/internal/kv/" + UrlEncode(key),
                                 RecordToJson(record), timeout);
  return response.transport_ok && response.status == 200;
}

ReplicaRead HttpPeerClient::Read(const Peer& peer, const std::string& key,
                                 std::chrono::milliseconds timeout) {
  const auto response =
      HttpCall(peer.host, peer.port, "GET", "/internal/kv/" + UrlEncode(key), "", timeout);
  if (!response.transport_ok) return {};
  if (response.status == 404) return {true, std::nullopt};
  if (response.status != 200) return {};
  const auto record = RecordFromJson(response.body);
  return record ? ReplicaRead{true, record} : ReplicaRead{};
}

bool HttpPeerClient::Healthy(const Peer& peer, std::chrono::milliseconds timeout) {
  const auto response = HttpCall(peer.host, peer.port, "GET", "/health", "", timeout);
  return response.transport_ok && response.status == 200;
}

}  // namespace kv
