#include "kv/storage_engine.hpp"

#include <mutex>

namespace kv {

StorageEngine::StorageEngine(const std::string& wal_path) : wal_(wal_path) {
  recovered_records_ = wal_.Replay([this](const std::string& key, const Record& record) {
    const auto found = records_.find(key);
    if (found == records_.end() || &Newer(found->second, record) == &record) records_[key] = record;
    auto maximum = max_version_counter_.load();
    while (maximum < record.version.counter &&
           !max_version_counter_.compare_exchange_weak(maximum, record.version.counter)) {
    }
  });
}

bool StorageEngine::PutIfNewer(const std::string& key, const Record& record) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  const auto found = records_.find(key);
  if (found != records_.end() && &Newer(found->second, record) == &found->second) return false;
  wal_.Append(key, record);
  records_[key] = record;
  auto maximum = max_version_counter_.load();
  while (maximum < record.version.counter &&
         !max_version_counter_.compare_exchange_weak(maximum, record.version.counter)) {
  }
  wal_writes_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

std::optional<Record> StorageEngine::Get(const std::string& key) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  const auto found = records_.find(key);
  return found == records_.end() ? std::nullopt : std::optional<Record>(found->second);
}

std::size_t StorageEngine::Size() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::size_t count = 0;
  for (const auto& item : records_)
    if (!item.second.tombstone) ++count;
  return count;
}

}  // namespace kv
