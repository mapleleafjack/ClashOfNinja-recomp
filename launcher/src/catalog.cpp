#include "catalog.h"

#include "util.h"

#include <climits>
#include <cwchar>
#include <utility>

namespace nla {
namespace {

// A small JSON reader: the catalog is the launcher's own file, and one this size does not
// justify a dependency. It reads the whole of JSON's syntax and refuses what it cannot
// read rather than guessing, so a broken mods.json is reported instead of half-read.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    std::wstring text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::wstring, JsonValue>> fields;

    const JsonValue* find(const wchar_t* key) const {
        for (const auto& field : fields) {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }

    std::wstring string_or(const wchar_t* key, const std::wstring& fallback = {}) const {
        const JsonValue* value = find(key);
        return value && value->type == Type::String ? value->text : fallback;
    }

    std::vector<std::wstring> strings_of(const wchar_t* key) const {
        std::vector<std::wstring> values;
        const JsonValue* value = find(key);
        if (!value) return values;
        if (value->type == Type::String) {
            if (!value->text.empty()) values.push_back(value->text);
        } else if (value->type == Type::Array) {
            for (const auto& item : value->items) {
                if (item.type == Type::String && !item.text.empty()) values.push_back(item.text);
            }
        }
        return values;
    }
};

void append_codepoint(std::wstring* out, unsigned long code) {
#if WCHAR_MAX >= 0x10FFFF
    out->push_back(static_cast<wchar_t>(code));
#else
    if (code <= 0xFFFF) {
        out->push_back(static_cast<wchar_t>(code));
    } else {
        code -= 0x10000;
        out->push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
        out->push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
    }
#endif
}

struct JsonReader {
    explicit JsonReader(const std::wstring& source) : text(source) {}

    const std::wstring& text;
    std::size_t at = 0;
    std::wstring error;

    bool fail(const wchar_t* what) {
        if (error.empty()) error = format(L"%ls at character %zu", what, at + 1);
        return false;
    }

    void skip_space() {
        while (at < text.size() && (text[at] == L' ' || text[at] == L'\t' || text[at] == L'\n' ||
                                    text[at] == L'\r')) {
            ++at;
        }
    }

    bool literal(const wchar_t* word) {
        const std::size_t length = std::wcslen(word);
        if (text.compare(at, length, word) != 0) return false;
        at += length;
        return true;
    }

    bool hex4(unsigned long* value) {
        if (at + 4 > text.size()) return fail(L"a \\u escape is cut short");
        unsigned long result = 0;
        for (int i = 0; i < 4; ++i) {
            const wchar_t c = text[at++];
            result <<= 4;
            if (c >= L'0' && c <= L'9') result |= static_cast<unsigned long>(c - L'0');
            else if (c >= L'a' && c <= L'f') result |= static_cast<unsigned long>(c - L'a' + 10);
            else if (c >= L'A' && c <= L'F') result |= static_cast<unsigned long>(c - L'A' + 10);
            else return fail(L"a \\u escape holds something other than hex");
        }
        *value = result;
        return true;
    }

    bool parse_string(std::wstring* out) {
        if (at >= text.size() || text[at] != L'"') return fail(L"a string was expected");
        ++at;
        out->clear();
        while (at < text.size()) {
            const wchar_t c = text[at++];
            if (c == L'"') return true;
            if (c != L'\\') {
                out->push_back(c);
                continue;
            }
            if (at >= text.size()) return fail(L"a string ends with a backslash");
            const wchar_t escaped = text[at++];
            switch (escaped) {
                case L'"': out->push_back(L'"'); break;
                case L'\\': out->push_back(L'\\'); break;
                case L'/': out->push_back(L'/'); break;
                case L'b': out->push_back(L'\b'); break;
                case L'f': out->push_back(L'\f'); break;
                case L'n': out->push_back(L'\n'); break;
                case L'r': out->push_back(L'\r'); break;
                case L't': out->push_back(L'\t'); break;
                case L'u': {
                    unsigned long code = 0;
                    if (!hex4(&code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF && at + 1 < text.size() &&
                        text[at] == L'\\' && text[at + 1] == L'u') {
                        at += 2;
                        unsigned long low = 0;
                        if (!hex4(&low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    append_codepoint(out, code);
                    break;
                }
                default:
                    return fail(L"a string holds an escape JSON does not have");
            }
        }
        return fail(L"a string is not closed");
    }

    bool parse_number(std::wstring* out) {
        const std::size_t start = at;
        if (at < text.size() && (text[at] == L'-' || text[at] == L'+')) ++at;
        bool digits = false;
        while (at < text.size()) {
            const wchar_t c = text[at];
            if ((c >= L'0' && c <= L'9') || c == L'.' || c == L'e' || c == L'E' ||
                ((c == L'-' || c == L'+') && (text[at - 1] == L'e' || text[at - 1] == L'E'))) {
                digits = digits || (c >= L'0' && c <= L'9');
                ++at;
            } else {
                break;
            }
        }
        if (!digits) return fail(L"a number was expected");
        *out = text.substr(start, at - start);
        return true;
    }

    bool parse_value(JsonValue* out, int depth) {
        if (depth > 64) return fail(L"the JSON is nested too deeply");
        skip_space();
        if (at >= text.size()) return fail(L"the file ends before its JSON does");
        const wchar_t c = text[at];
        if (c == L'{') return parse_object(out, depth);
        if (c == L'[') return parse_array(out, depth);
        if (c == L'"') {
            out->type = JsonValue::Type::String;
            return parse_string(&out->text);
        }
        if (literal(L"true")) {
            out->type = JsonValue::Type::Bool;
            out->boolean = true;
            return true;
        }
        if (literal(L"false")) {
            out->type = JsonValue::Type::Bool;
            return true;
        }
        if (literal(L"null")) {
            out->type = JsonValue::Type::Null;
            return true;
        }
        out->type = JsonValue::Type::Number;
        return parse_number(&out->text);
    }

    bool parse_array(JsonValue* out, int depth) {
        out->type = JsonValue::Type::Array;
        ++at;  // [
        skip_space();
        if (at < text.size() && text[at] == L']') {
            ++at;
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!parse_value(&item, depth + 1)) return false;
            out->items.push_back(std::move(item));
            skip_space();
            if (at >= text.size()) return fail(L"an array is not closed");
            if (text[at] == L',') {
                ++at;
                continue;
            }
            if (text[at] == L']') {
                ++at;
                return true;
            }
            return fail(L"an array holds a value that is not followed by ',' or ']'");
        }
    }

    bool parse_object(JsonValue* out, int depth) {
        out->type = JsonValue::Type::Object;
        ++at;  // {
        skip_space();
        if (at < text.size() && text[at] == L'}') {
            ++at;
            return true;
        }
        for (;;) {
            skip_space();
            std::wstring key;
            if (!parse_string(&key)) return false;
            skip_space();
            if (at >= text.size() || text[at] != L':') return fail(L"an object key has no ':'");
            ++at;
            JsonValue value;
            if (!parse_value(&value, depth + 1)) return false;
            out->fields.emplace_back(std::move(key), std::move(value));
            skip_space();
            if (at >= text.size()) return fail(L"an object is not closed");
            if (text[at] == L',') {
                ++at;
                continue;
            }
            if (text[at] == L'}') {
                ++at;
                return true;
            }
            return fail(L"an object holds a key that is not followed by ',' or '}'");
        }
    }

    bool parse_document(JsonValue* out) {
        if (!parse_value(out, 0)) return false;
        skip_space();
        if (at != text.size()) return fail(L"the file holds more than one JSON value");
        return true;
    }
};

std::wstring strip_bom(const std::wstring& text) {
    if (text.size() >= 1 && text.front() == L'\uFEFF') return text.substr(1);
    return text;
}

}  // namespace

fs::path mod_catalog_path() {
    std::error_code ec;
    if (fs::path resources = bundle_resources_dir(); !resources.empty()) {
        fs::path candidate = resources / L"mods.json";
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    // Beside the executable is where a release puts it. A development build lives in
    // launcher/build, so the checkout above it is looked in too.
    for (fs::path candidate = fs::path(exe_dir()); !candidate.empty();
         candidate = candidate.parent_path()) {
        fs::path beside = candidate / L"mods.json";
        if (fs::is_regular_file(beside, ec)) return beside;
        fs::path launcher = candidate / L"launcher" / L"mods.json";
        if (fs::is_regular_file(launcher, ec)) return launcher;
        if (candidate.parent_path() == candidate) break;
    }
    return {};
}

std::vector<CatalogMod> parse_mod_catalog(const std::wstring& text, std::wstring* error) {
    std::vector<CatalogMod> mods;
    if (error) error->clear();
    JsonValue root;
    const std::wstring stripped = strip_bom(text);
    JsonReader reader(stripped);
    if (!reader.parse_document(&root)) {
        if (error) *error = L"mods.json: " + reader.error;
        return mods;
    }
    const JsonValue* list = root.find(L"mods");
    if (!list || list->type != JsonValue::Type::Array) {
        if (error) *error = L"mods.json: there is no \"mods\" list";
        return mods;
    }
    for (const auto& item : list->items) {
        if (item.type != JsonValue::Type::Object) continue;
        CatalogMod mod;
        mod.id = trim(item.string_or(L"id"));
        if (mod.id.empty()) continue;
        mod.name = item.string_or(L"name", mod.id);
        mod.version = item.string_or(L"version");
        mod.author = item.string_or(L"author");
        mod.description = item.string_or(L"description");
        mod.disc_ids = item.strings_of(L"disc_id");
        mod.patch = to_lower(item.string_or(L"kind")) == L"patch";
        mod.url = item.string_or(L"url");
        mod.folder = item.string_or(L"folder");
        if (const JsonValue* release = item.find(L"release");
            release && release->type == JsonValue::Type::Object) {
            mod.owner = release->string_or(L"owner");
            mod.repo = release->string_or(L"repo");
            mod.asset = release->string_or(L"asset");
            mod.file = release->string_or(L"file");
        }
        // A mod that names no build is not offered for one: a build's addresses are its
        // own, and the catalog is not a place to guess at them.
        if (mod.disc_ids.empty()) continue;
        mods.push_back(std::move(mod));
    }
    return mods;
}

std::vector<CatalogMod> load_mod_catalog(const fs::path& path, std::wstring* error) {
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = L"no mods.json beside the launcher";
        return {};
    }
    auto text = read_text_file(path);
    if (text.empty()) {
        if (error) *error = L"mods.json could not be read";
        return {};
    }
    return parse_mod_catalog(text, error);
}

bool catalog_supports(const CatalogMod& mod, const std::wstring& disc_id) {
    auto wanted = to_lower(disc_id);
    for (const auto& id : mod.disc_ids) {
        if (to_lower(id) == wanted) return true;
    }
    return false;
}

}  // namespace nla
