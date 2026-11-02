// Text: turning UTF-8 into what the window draws and back again, and the small string work
// the launcher does on the way. Nothing here touches a file or a process.

#include "text.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwctype>

namespace nla {

wstring widen(std::string_view utf8) {
    wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    auto next = [&](uint32_t& cp) -> bool {
        if (i >= utf8.size()) return false;
        auto byte = static_cast<unsigned char>(utf8[i]);
        int extra = 0;
        if (byte < 0x80) {
            cp = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            cp = byte & 0x1F;
            extra = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            cp = byte & 0x0F;
            extra = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            cp = byte & 0x07;
            extra = 3;
        } else {
            cp = 0xFFFD;
            ++i;
            return true;
        }
        ++i;
        for (int n = 0; n < extra; ++n, ++i) {
            if (i >= utf8.size() || (static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) {
                cp = 0xFFFD;
                return true;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        }
        return true;
    };
    uint32_t cp = 0;
    while (next(cp)) {
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string narrow(wstring_view text) {
    std::string out;
    out.reserve(text.size());
    auto emit = [&out](uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        emit(cp);
    }
    return out;
}

std::vector<uint32_t> codepoints(wstring_view text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
            uint32_t low = static_cast<uint32_t>(text[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        out.push_back(cp);
    }
    return out;
}

wstring format(const wchar_t* fmt, ...) {
    wchar_t buffer[4096];
    va_list args;
    va_start(args, fmt);
    int count = vswprintf(buffer, std::size(buffer) - 1, fmt, args);
    va_end(args);
    if (count < 0) count = static_cast<int>(std::size(buffer)) - 1;
    buffer[count] = L'\0';
    return wstring(buffer, static_cast<size_t>(count));
}

wstring trim(wstring text) {
    auto space = [](wchar_t c) { return iswspace(static_cast<wint_t>(c)) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), space));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), space).base(), text.end());
    return text;
}

std::vector<wstring> split(wstring_view text, wchar_t separator) {
    std::vector<wstring> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(separator, start);
        if (end == wstring_view::npos) end = text.size();
        if (end > start) parts.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

wstring join(const std::vector<wstring>& parts, wstring_view separator) {
    wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(separator);
        out.append(parts[i]);
    }
    return out;
}

wstring to_lower(wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(static_cast<wint_t>(c))); });
    return text;
}

bool ends_with_ci(wstring_view text, wstring_view suffix) {
    if (suffix.size() > text.size()) return false;
    return to_lower(wstring(text.substr(text.size() - suffix.size()))) ==
           to_lower(wstring(suffix));
}
}  // namespace nla
