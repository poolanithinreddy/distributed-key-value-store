#pragma once

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "kv/types.hpp"
#include "kv/wal.hpp"

namespace kv {

class StorageEngine {
 public:
  explicit StorageEngine(const std::string& wal_path);
  bool PutIfNewer(const std::string& key, const Record& record);
  [[nodiscard]] std::optional<Record> Get(const std::string& key) const;
  [[nodiscard]] std::size_t Size() const;
  [[nodiscard]] std::size_t RecoveredRecords() const { return recovered_records_; }
  [[nodiscard]] std::uint64_t WalWrites() const { return wal_writes_.load(); }
  [[nodiscard]] std::uint64_t MaxVersionCounter() const { return max_version_counter_.load(); }

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, Record> records_;
  WriteAheadLog wal_;
  std::size_t recovered_records_{0};
  std::atomic<std::uint64_t> wal_writes_{0};
  std::atomic<std::uint64_t> max_version_counter_{0};
};

}  // namespace kv
