#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>

#include "kv/types.hpp"

namespace kv {

class WriteAheadLog {
 public:
  explicit WriteAheadLog(std::string path);
  ~WriteAheadLog();
  WriteAheadLog(const WriteAheadLog&) = delete;
  WriteAheadLog& operator=(const WriteAheadLog&) = delete;

  void Append(const std::string& key, const Record& record);
  std::size_t Replay(const std::function<void(const std::string&, const Record&)>& visitor);
  [[nodiscard]] const std::string& path() const { return path_; }

 private:
  std::string path_;
  std::ofstream output_;
  std::mutex mutex_;
};

}  // namespace kv
