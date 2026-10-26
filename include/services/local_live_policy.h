#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace local_live {
constexpr uint8_t PARTIAL_DRIVE_PASSES = 8;
constexpr uint8_t FULL_DRIVE_PASSES = 3;
inline bool validPartialPasses(int passes) { return passes == 6 || passes == 8 || passes == 10; }
// Conservative URL grammar: one origin, no embedded credentials or query in
// the poll endpoint. Image/ack paths may contain a query, never another host.
inline bool validPath(const char *path) {
  if (!path || path[0] != '/' || path[1] == '/' || strlen(path) > 512) return false;
  for (const char *p = path; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '\\' || *p == '#') return false;
  return true;
}
inline bool validEndpoint(const char *url) {
  if (!url || strlen(url) > 512) return false;
  const char *authority = nullptr;
  if (!strncmp(url, "http://", 7)) authority = url + 7;
  else if (!strncmp(url, "https://", 8)) authority = url + 8;
  else return false;
  const char *path = strchr(authority, '/');
  if (!path || path == authority || !validPath(path) || strchr(path, '?')) return false;
  for (const char *p = authority; p < path; ++p) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '-' || c == ':' || c == '[' || c == ']')) return false;
  }
  return true;
}
inline bool validToken(const char *token) {
  if (!token || strlen(token) > 256) return false;
  for (const char *p = token; *p; ++p)
    if ((unsigned char)*p < 33 || (unsigned char)*p > 126) return false;
  return true; // empty means no Authorization header
}
inline uint32_t bigEndian(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline bool validPng(const uint8_t *p, size_t size, uint32_t width, uint32_t height) {
  static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  return p && size >= 45 && size < 750000 && !memcmp(p, signature, 8) &&
    bigEndian(p + 8) == 13 && !memcmp(p + 12, "IHDR", 4) &&
    bigEndian(p + 16) == width && bigEndian(p + 20) == height &&
    p[24] == 1 && p[25] == 0 && p[26] == 0 && p[27] == 0 && p[28] == 0;
}
inline bool validId(const char *id) {
  if (!id || strlen(id) != 32) return false;
  for (int i = 0; i < 32; ++i)
    if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
  return true;
}
struct RowRange { int first; int last; };
inline RowRange changedRows(const uint8_t *current, const uint8_t *previous, size_t pitch, int height) {
  RowRange result = {height, -1};
  for (int row = 0; row < height; ++row) {
    if (memcmp(current + row * pitch, previous + row * pitch, pitch)) {
      if (result.last < 0) result.first = row;
      result.last = row;
    }
  }
  return result;
}
struct RefreshPolicy {
  bool known = false;
  uint8_t partials = 0;
  uint32_t lastFull = 0;
  bool full(uint32_t now, bool requested = false) const {
    return !known || requested || partials >= 3 || uint32_t(now - lastFull) >= 10UL * 60 * 1000;
  }
  void displayed(uint32_t now, bool wasFull) {
    known = true;
    if (wasFull) { partials = 0; lastFull = now; }
    else ++partials;
  }
  void failed() { known = false; }
};
inline uint32_t retryDelay(unsigned failures) {
  return failures >= 5 ? 60000 : 2000UL << failures;
}
}
