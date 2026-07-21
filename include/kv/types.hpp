#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kv {

struct Version {
  std::uint64_t counter{0};
  std::string origin;
};

bool operator==(const Version& lhs, const Version& rhs);
bool operator<(const Version& lhs, const Version& rhs);

struct Record {
  std::string value;
  Version version;
  bool tombstone{false};
};

bool operator==(const Record& lhs, const Record& rhs);
const Record& Newer(const Record& lhs, const Record& rhs);

enum class ErrorCode {
  kNone,
  kNotFound,
  kBadRequest,
  kQuorumUnavailable,
  kTimeout,
  kIo,
  kInternal
};

struct Result {
  ErrorCode error{ErrorCode::kNone};
  std::optional<Record> record;
  std::size_t acknowledgements{0};
  std::string message;

  [[nodiscard]] bool ok() const { return error == ErrorCode::kNone; }
};

struct Peer {
  std::string id;
  std::string host;
  std::uint16_t port{0};
};

std::string JsonEscape(const std::string& input);
std::string ErrorName(ErrorCode error);
std::string RecordToJson(const Record& record);
std::optional<Record> RecordFromJson(const std::string& json);
std::string NowIso8601();

}  // namespace kv
