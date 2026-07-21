#include "kv/types.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace kv {

bool operator==(const Version& lhs, const Version& rhs) {
  return lhs.counter == rhs.counter && lhs.origin == rhs.origin;
}

bool operator<(const Version& lhs, const Version& rhs) {
  return lhs.counter != rhs.counter ? lhs.counter < rhs.counter : lhs.origin < rhs.origin;
}

bool operator==(const Record& lhs, const Record& rhs) {
  return lhs.value == rhs.value && lhs.version == rhs.version && lhs.tombstone == rhs.tombstone;
}

const Record& Newer(const Record& lhs, const Record& rhs) {
  if (lhs.version < rhs.version) return rhs;
  if (rhs.version < lhs.version) return lhs;
  if (lhs.tombstone != rhs.tombstone) return lhs.tombstone ? lhs : rhs;
  return lhs.value < rhs.value ? rhs : lhs;
}

std::string JsonEscape(const std::string& input) {
  std::ostringstream out;
  for (const char raw : input) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (c < 0x20U) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

std::string ErrorName(ErrorCode error) {
  switch (error) {
    case ErrorCode::kNone:
      return "none";
    case ErrorCode::kNotFound:
      return "not_found";
    case ErrorCode::kBadRequest:
      return "bad_request";
    case ErrorCode::kQuorumUnavailable:
      return "quorum_unavailable";
    case ErrorCode::kTimeout:
      return "timeout";
    case ErrorCode::kIo:
      return "io_error";
    case ErrorCode::kInternal:
      return "internal_error";
  }
  return "unknown";
}

std::string RecordToJson(const Record& record) {
  return "{\"value\":\"" + JsonEscape(record.value) +
         "\",\"version\":" + std::to_string(record.version.counter) + ",\"origin\":\"" +
         JsonEscape(record.version.origin) +
         "\",\"tombstone\":" + (record.tombstone ? "true" : "false") + "}";
}

namespace {
std::optional<std::string> Match(const std::string& text, const std::string& pattern) {
  std::smatch match;
  if (!std::regex_search(text, match, std::regex(pattern))) return std::nullopt;
  return match[1].str();
}

std::string Unescape(std::string value) {
  std::string out;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out.push_back(value[i]);
      continue;
    }
    const char next = value[++i];
    if (next == 'n')
      out.push_back('\n');
    else if (next == 'r')
      out.push_back('\r');
    else if (next == 't')
      out.push_back('\t');
    else
      out.push_back(next);
  }
  return out;
}
}  // namespace

std::optional<Record> RecordFromJson(const std::string& json) {
  const auto value = Match(json, "\\\"value\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
  const auto counter = Match(json, "\\\"version\\\"\\s*:\\s*([0-9]+)");
  const auto origin = Match(json, "\\\"origin\\\"\\s*:\\s*\\\"((?:\\\\.|[^\\\"])*)\\\"");
  const auto tombstone = Match(json, "\\\"tombstone\\\"\\s*:\\s*(true|false)");
  if (!value || !counter || !origin || !tombstone) return std::nullopt;
  try {
    return Record{Unescape(*value), Version{std::stoull(*counter), Unescape(*origin)},
                  *tombstone == "true"};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string NowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
      << millis.count() << 'Z';
  return out.str();
}

}  // namespace kv
