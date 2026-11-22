#pragma once

// Plain C++, no ESPHome includes: built on the host by tests/cpp/test_sniffer_data and tests/cpp/test_scan_goal.
//
// Reads a request URI's query string in place, without the server's heap-allocated parameters
// (.specs/sniffer.md, "Concurrency and RAM").

#include <optional>
#include <string_view>

namespace esphome {
namespace sec_touch {

/// The value of the first `key=value` pair in `uri`'s query string, empty for `key=`; nullopt when no pair has that
/// key (a bare `key` without `=` counts as missing). No percent-decoding.
inline std::optional<std::string_view> query_value(std::string_view uri, std::string_view key) {
  const size_t query = uri.find('?');
  if (query == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view rest = uri.substr(query + 1);
  while (!rest.empty()) {
    const size_t end = rest.find('&');
    const std::string_view pair = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
    if (pair.size() > key.size() && pair.substr(0, key.size()) == key && pair[key.size()] == '=') {
      return pair.substr(key.size() + 1);
    }
  }
  return std::nullopt;
}

}  // namespace sec_touch
}  // namespace esphome
