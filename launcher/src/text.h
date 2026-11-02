#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nla {

using std::wstring;
using std::wstring_view;

// Text is UTF-16 on Windows and UTF-32 elsewhere; these convert to and from UTF-8.
wstring widen(std::string_view utf8);
std::string narrow(wstring_view text);
std::vector<uint32_t> codepoints(wstring_view text);

wstring format(const wchar_t* fmt, ...);
wstring trim(wstring text);
std::vector<wstring> split(wstring_view text, wchar_t separator);
wstring join(const std::vector<wstring>& parts, wstring_view separator);
wstring to_lower(wstring text);
bool ends_with_ci(wstring_view text, wstring_view suffix);

}  // namespace nla
