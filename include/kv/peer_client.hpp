#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "kv/types.hpp"

namespace kv {

struct ReplicaRead {
  bool responded{false};
  std::optional<Record> record;
};

class ReplicaClient {
 public:
  virtual ~ReplicaClient() = default;
  virtual bool Write(const Peer& peer, const std::string& key, const Record& record,
                     std::chrono::milliseconds timeout) = 0;
  virtual ReplicaRead Read(const Peer& peer, const std::string& key,
                           std::chrono::milliseconds timeout) = 0;
  virtual bool Healthy(const Peer& peer, std::chrono::milliseconds timeout) = 0;
};

class HttpPeerClient final : public ReplicaClient {
 public:
  bool Write(const Peer& peer, const std::string& key, const Record& record,
             std::chrono::milliseconds timeout) override;
  ReplicaRead Read(const Peer& peer, const std::string& key,
                   std::chrono::milliseconds timeout) override;
  bool Healthy(const Peer& peer, std::chrono::milliseconds timeout) override;
};

}  // namespace kv
