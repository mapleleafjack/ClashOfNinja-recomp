#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace nla {

namespace fs = std::filesystem;

struct DiscStatus {
    bool extracted = false;
    bool verified = false;
    std::wstring sha1;
    std::wstring expected;
};

struct ProjectStatus {
    DiscStatus disc;
    bool built = false;
};

class Project {
  public:
    explicit Project(fs::path root);

    bool valid() const;

    fs::path root() const { return root_; }
    fs::path naruto() const;
    fs::path disc() const;
    fs::path dol() const;
    fs::path manifest() const;
    fs::path build_dir() const;
    fs::path binary() const;
    fs::path controls() const;
    fs::path saves() const;
    fs::path extract_script() const;
    fs::path cmake_lists() const;

    std::wstring disc_id() const;
    std::wstring expected_sha1() const;
    DiscStatus disc_status() const;
    ProjectStatus status() const;

    static std::optional<fs::path> locate(const std::wstring& saved);

  private:
    fs::path root_;
};

}  // namespace nla
