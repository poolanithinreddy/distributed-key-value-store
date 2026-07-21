#include "kv/wal.hpp"

#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

#include "kv/hash_ring.hpp"

namespace kv {
namespace {
constexpr std::uint32_t kMagic = 0x4B565741U;
constexpr std::uint32_t kMaxField = 64U * 1024U * 1024U;

void Put32(std::vector<char>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<char>((value >> shift) & 0xffU));
}
void Put64(std::vector<char>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8)
    out.push_back(static_cast<char>((value >> shift) & 0xffU));
}
std::uint32_t Get32(const char* data) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[i])) << (i * 8);
  return value;
}
std::uint64_t Get64(const char* data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i])) << (i * 8);
  return value;
}
std::uint64_t Checksum(const std::vector<char>& body) {
  return StableHash(std::string(body.begin(), body.end()));
}
}  // namespace

WriteAheadLog::WriteAheadLog(std::string path) : path_(std::move(path)) {
  const std::filesystem::path file_path(path_);
  if (file_path.has_parent_path()) std::filesystem::create_directories(file_path.parent_path());
  output_.open(path_, std::ios::binary | std::ios::app);
  if (!output_) throw std::runtime_error("cannot open WAL: " + path_);
}

WriteAheadLog::~WriteAheadLog() { output_.close(); }

void WriteAheadLog::Append(const std::string& key, const Record& record) {
  if (key.size() > kMaxField || record.value.size() > kMaxField ||
      record.version.origin.size() > kMaxField)
    throw std::invalid_argument("WAL field too large");
  std::vector<char> body;
  body.reserve(32 + key.size() + record.value.size() + record.version.origin.size());
  Put32(body, static_cast<std::uint32_t>(key.size()));
  Put32(body, static_cast<std::uint32_t>(record.value.size()));
  Put32(body, static_cast<std::uint32_t>(record.version.origin.size()));
  Put64(body, record.version.counter);
  body.push_back(record.tombstone ? 1 : 0);
  body.insert(body.end(), key.begin(), key.end());
  body.insert(body.end(), record.value.begin(), record.value.end());
  body.insert(body.end(), record.version.origin.begin(), record.version.origin.end());
  std::vector<char> header;
  Put32(header, kMagic);
  Put32(header, static_cast<std::uint32_t>(body.size()));
  Put64(header, Checksum(body));
  std::lock_guard<std::mutex> lock(mutex_);
  output_.write(header.data(), static_cast<std::streamsize>(header.size()));
  output_.write(body.data(), static_cast<std::streamsize>(body.size()));
  output_.flush();
  if (!output_) throw std::runtime_error("WAL append failed: " + path_);
}

std::size_t WriteAheadLog::Replay(
    const std::function<void(const std::string&, const Record&)>& visitor) {
  std::lock_guard<std::mutex> lock(mutex_);
  output_.flush();
  std::ifstream input(path_, std::ios::binary);
  std::size_t replayed = 0;
  std::array<char, 16> header{};
  while (input.read(header.data(), static_cast<std::streamsize>(header.size()))) {
    if (Get32(header.data()) != kMagic) break;
    const auto length = Get32(header.data() + 4);
    if (length < 21U || length > (kMaxField * 3U + 21U)) break;
    std::vector<char> body(length);
    if (!input.read(body.data(), static_cast<std::streamsize>(body.size()))) break;
    if (Get64(header.data() + 8) != Checksum(body)) break;
    const auto key_len = Get32(body.data());
    const auto value_len = Get32(body.data() + 4);
    const auto origin_len = Get32(body.data() + 8);
    const std::uint64_t total = 21ULL + key_len + value_len + origin_len;
    if (total != body.size()) break;
    const auto counter = Get64(body.data() + 12);
    const bool tombstone = body[20] != 0;
    const char* cursor = body.data() + 21;
    std::string key(cursor, cursor + key_len);
    cursor += key_len;
    std::string value(cursor, cursor + value_len);
    cursor += value_len;
    std::string origin(cursor, cursor + origin_len);
    visitor(key, Record{std::move(value), Version{counter, std::move(origin)}, tombstone});
    ++replayed;
  }
  return replayed;
}

}  // namespace kv
