// The two digests the launcher takes, and nothing else: SHA-1 of a disc image's main
// executable, and SHA-256 of the tools archive. Both are written out here rather than taken
// from a library or from a tool the player has to have, so the same bytes give the same
// answer on every machine.

#include "crypto.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace nla {

// -- SHA-1 -----------------------------------------------------------------

namespace {

struct Sha1 {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t length = 0;
    unsigned char block[64] = {};
    size_t used = 0;

    static uint32_t rol(uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void compress(const unsigned char* data) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    void update(const unsigned char* data, size_t size) {
        length += size;
        while (size > 0) {
            size_t take = std::min(size, sizeof(block) - used);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == sizeof(block)) {
                compress(block);
                used = 0;
            }
        }
    }

    wstring finish() {
        uint64_t bits = length * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (used != 56) update(&zero, 1);
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFF);
        update(tail, 8);

        wstring hex;
        hex.reserve(40);
        for (uint32_t word : state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                unsigned value = (word >> shift) & 0xF;
                hex.push_back(static_cast<wchar_t>(value < 10 ? L'0' + value
                                                              : L'a' + (value - 10)));
            }
        }
        return hex;
    }
};

}  // namespace

wstring sha1_hex(const void* data, size_t size) {
    Sha1 hash;
    hash.update(static_cast<const unsigned char*>(data), size);
    return hash.finish();
}

wstring sha1_file(const fs::path& path, wstring* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = L"cannot read " + path.wstring();
        return {};
    }
    Sha1 hash;
    std::vector<char> buffer(1 << 16);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = file.gcount();
        if (got > 0) hash.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                                 static_cast<size_t>(got));
    }
    return hash.finish();
}

// -- SHA-256 ---------------------------------------------------------------

namespace {

struct Sha256 {
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint64_t length = 0;
    unsigned char block[64] = {};
    size_t used = 0;

    static uint32_t ror(uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void compress(const unsigned char* data) {
        static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(data[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    void update(const unsigned char* data, size_t size) {
        length += size;
        while (size > 0) {
            size_t take = std::min(size, sizeof(block) - used);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == sizeof(block)) {
                compress(block);
                used = 0;
            }
        }
    }

    wstring finish() {
        const uint64_t bits = length * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (used != 56) update(&zero, 1);
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFF);
        update(tail, 8);

        wstring hex;
        hex.reserve(64);
        for (uint32_t word : state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                unsigned value = (word >> shift) & 0xF;
                hex.push_back(static_cast<wchar_t>(value < 10 ? L'0' + value
                                                              : L'a' + (value - 10)));
            }
        }
        return hex;
    }
};

}  // namespace

wstring sha256_hex(const void* data, size_t size) {
    Sha256 hash;
    hash.update(static_cast<const unsigned char*>(data), size);
    return hash.finish();
}

wstring sha256_file(const fs::path& path, wstring* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = L"cannot read " + path.wstring();
        return {};
    }
    Sha256 hash;
    std::vector<char> buffer(1 << 16);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = file.gcount();
        if (got > 0) hash.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                                 static_cast<size_t>(got));
    }
    return hash.finish();
}
}  // namespace nla
