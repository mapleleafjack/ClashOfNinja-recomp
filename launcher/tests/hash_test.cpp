// The two digests the launcher takes: SHA-1 of a disc image's main executable, which is how
// a project knows the disc it was built from, and SHA-256 of the tools archive, which is how
// a download is known to be the one a release published. Neither had a check of its own
// before this, and there was no SHA-256 in the tree at all on Linux or macOS.
//
// The vectors are the ones the algorithms are specified with, plus the lengths either side
// of a block boundary, where a padding mistake shows up and nowhere else. Run by ctest.

#include "tools_package.h"
#include "util.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// `repeat` bytes of `byte`, which is how the million-'a' vector is built without a megabyte
// of source.
std::string repeated(char byte, size_t count) { return std::string(count, byte); }

void sha256_vectors() {
    check(nla::sha256_hex("", 0) ==
              L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "an empty buffer hashes to the specified value");
    check(nla::sha256_hex("abc", 3) ==
              L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "so does \"abc\"");
    const char* two_blocks =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(nla::sha256_hex(two_blocks, std::strlen(two_blocks)) ==
              L"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "a message spanning two blocks");
    const std::string million = repeated('a', 1000000);
    check(nla::sha256_hex(million.data(), million.size()) ==
              L"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "a million of them");
}

// Where the length is padded is where a block-at-a-time hash goes wrong: 55 bytes leaves
// room for the 0x80 and the length in the same block, 56 pushes the length into a second
// one, and 63 and 64 are the far edge of that.
void the_block_boundary() {
    struct Case {
        size_t size;
        const wchar_t* digest;
        const char* what;
    };
    const Case cases[] = {
        {55, L"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
         "55 bytes fills one block exactly"},
        {56, L"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
         "56 bytes moves the length into a second block"},
        {63, L"7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",
         "63 bytes is the last that fits"},
        {64, L"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
         "64 bytes is a whole block with no message in the next"},
        {65, L"635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0",
         "and 65 spills one byte into it"},
    };
    for (const Case& one : cases) {
        const std::string bytes = repeated('a', one.size);
        check(nla::sha256_hex(bytes.data(), bytes.size()) == one.digest, one.what);
    }
}

// The disc image is mostly zeroes, so a byte that is not one has to change the answer.
void bytes_that_are_zero() {
    const std::string zeros(32, '\0');
    check(nla::sha256_hex(zeros.data(), zeros.size()) ==
              L"66687aadf862bd776c8fc18b8e9f8e20089714856ee233b3902a591d0d5f2925",
          "an embedded zero is hashed, not skipped");
}

// The one the project already relied on, with no check behind it until now.
void sha1_vectors() {
    check(nla::sha1_hex("", 0) == L"da39a3ee5e6b4b0d3255bfef95601890afd80709",
          "an empty buffer has a SHA-1");
    check(nla::sha1_hex("abc", 3) == L"a9993e364706816aba3e25717850c26c9cd0d89d",
          "and so does \"abc\"");
    const char* two_blocks =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(nla::sha1_hex(two_blocks, std::strlen(two_blocks)) ==
              L"84983e441c3bd26ebaae4aa1f95129e5e54670f1",
          "and one spanning two blocks");
    const std::string million = repeated('a', 1000000);
    check(nla::sha1_hex(million.data(), million.size()) ==
              L"34aa973cd4c4daa4f61eeb2bdbad27316534016f",
          "and a million of them");
}

fs::path write_file(const std::wstring& name, const std::string& bytes) {
    const fs::path root = nla::temp_dir() / L"nla-hash-test";
    std::error_code ec;
    fs::create_directories(root, ec);
    const fs::path file = root / name;
    nla::write_text_file(file, bytes);
    return file;
}

// A file hashed in one call and a buffer hashed in one call are the same bytes, so the two
// must agree - the archive check reads from disk, the tests above read from memory.
void hashing_a_file() {
    const std::string bytes = repeated('a', 70000);
    const fs::path file = write_file(L"whole.bin", bytes);
    check(nla::sha256_file(file) == nla::sha256_hex(bytes.data(), bytes.size()),
          "a file hashes to what its bytes do");
    check(nla::sha256_file(file).size() == 64, "and the digest is 64 characters");
    std::error_code ec;
    check(nla::sha256_file(file.parent_path() / L"no-such-file", nullptr).empty(),
          "a file that is not there has no digest");
    check(!fs::exists(file.parent_path() / L"no-such-file", ec),
          "and nothing was created looking for it");
}

// What the download check does with the answer: a matching archive is kept, and one that
// does not match is deleted so a second attempt cannot reuse it.
void checking_an_archive() {
    const std::string bytes = "the tools archive";
    const fs::path good = write_file(L"good.tar.xz", bytes);
    std::wstring error;
    check(nla::check_archive(good, nla::sha256_hex(bytes.data(), bytes.size()), &error),
          "an archive that matches is accepted");
    std::error_code ec;
    check(fs::exists(good, ec), "and is left where it was");

    const fs::path bad = write_file(L"bad.tar.xz", bytes);
    check(!nla::check_archive(bad, L"00", &error), "one that does not match is refused");
    check(!error.empty(), "with a reason");
    check(error.find(L"hashes to") != std::wstring::npos, "that says what it did hash to");
    check(!fs::exists(bad, ec), "and it is deleted, so a retry cannot reuse it");

    const fs::path missing = nla::temp_dir() / L"nla-hash-test" / L"absent.tar.xz";
    error.clear();
    check(!nla::check_archive(missing, L"00", &error),
          "an archive that is not there is refused");
    check(!error.empty(), "with a reason that is not a mismatch");
    fs::remove_all(nla::temp_dir() / L"nla-hash-test", ec);
}

int run() {
    sha256_vectors();
    the_block_boundary();
    bytes_that_are_zero();
    sha1_vectors();
    hashing_a_file();
    checking_an_archive();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
