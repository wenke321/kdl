#pragma once

// ---------------------------------------------------------------------------
// Small, dependency-free string helpers (UTF-8 <-> UTF-16, trimming, ...).
// All std::string values in the project are UTF-8 encoded.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "core/Types.h"

namespace kdl {

// UTF-8 <-> UTF-16 (Windows wide char) conversions.
std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);

std::string toLower(std::string s);
std::string toUpper(std::string s);
std::string trim(const std::string& s);

bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
bool iEquals(const std::string& a, const std::string& b);

// Split a string on a single delimiter character.
std::vector<std::string> split(const std::string& s, char delimiter);

// Split on CRLF line boundaries, ignoring empty trailing lines.
std::vector<std::string> splitLines(const std::string& s);

// Replace every occurrence of `from` with `to`.
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// Percent decoding ("%20" -> " "). '+' is preserved (it is a valid path char).
std::string urlDecode(const std::string& s);

// Strict unsigned integer parsing; returns false on any malformed input.
bool parseU64(const std::string& s, u64& out);
// Parse a decimal integer with optional sign.
bool parseInt(const std::string& s, long long& out);

} // namespace kdl
