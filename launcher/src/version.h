#pragma once

#include <string>

// CMake passes the tag a release was built from; a plain configure of an untagged tree
// keeps the historical 1.0.
#ifndef NLA_VERSION
#define NLA_VERSION "1.0"
#endif

// The commit the launcher was built from, so Settings can tell when the project folder's
// source is older than the launcher that builds it.
#ifndef NLA_SOURCE
#define NLA_SOURCE ""
#endif
#ifndef NLA_SOURCE_TIME
#define NLA_SOURCE_TIME 0
#endif

namespace nla {

inline std::wstring launcher_version() { return L"" NLA_VERSION; }
inline std::wstring launcher_source() { return L"" NLA_SOURCE; }
// When that commit was made, in seconds since the epoch: a checkout that never fetched
// it is judged by date instead.
inline long long launcher_source_time() { return NLA_SOURCE_TIME; }

// A build the workflow makes from a branch rather than a tag reports 0.0.0, so it must
// not compare itself with the published releases.
inline bool development_build() { return launcher_version() == L"0.0.0"; }

}  // namespace nla
