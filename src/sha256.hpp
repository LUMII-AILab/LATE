#pragma once

#include <array>
#include <sstream>
#include <iomanip>


class SHA256 {

    // constants for SHA-256
    static constexpr std::array<uint32_t, 64> k = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };

    // utility functions for SHA-256
    inline uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    inline uint32_t choose(uint32_t e, uint32_t f, uint32_t g) {
        return (e & f) ^ (~e & g);
    }

    inline uint32_t majority(uint32_t a, uint32_t b, uint32_t c) {
        return (a & b) ^ (a & c) ^ (b & c);
    }

    inline uint32_t sig0(uint32_t x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }

    inline uint32_t sig1(uint32_t x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }

    inline uint32_t SIG0(uint32_t x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }

    inline uint32_t SIG1(uint32_t x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }

public:
    SHA256() {
        reset();
    }

    void update(const uint8_t* data, size_t length) {
        const uint8_t* current = data;
        while (length > 0) {
            size_t space = 64 - m_dataLength;
            size_t copyLength = (length < space) ? length : space;
            std::memcpy(m_data + m_dataLength, current, copyLength);
            m_dataLength += copyLength;
            current += copyLength;
            length -= copyLength;
            if (m_dataLength == 64) {
                transform();
                m_dataLength = 0;
                m_bitLength += 512;
            }
        }
    }

    void update(const std::string& data) {
        update(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
    }

    std::string final() {
        uint64_t bitLength = m_bitLength + m_dataLength * 8;
        m_data[m_dataLength++] = 0x80;
        if (m_dataLength > 56) {
            while (m_dataLength < 64) {
                m_data[m_dataLength++] = 0x00;
            }
            transform();
            m_dataLength = 0;
        }
        while (m_dataLength < 56) {
            m_data[m_dataLength++] = 0x00;
        }
        for (int i = 7; i >= 0; --i) {
            m_data[m_dataLength++] = (bitLength >> (i * 8)) & 0xFF;
        }
        transform();
        std::ostringstream result;
        for (int i = 0; i < 8; ++i) {
            result << std::hex << std::setw(8) << std::setfill('0') << m_state[i];
        }
        reset();
        return result.str();
    }

private:
    void reset() {
        m_dataLength = 0;
        m_bitLength = 0;
        m_state[0] = 0x6a09e667;
        m_state[1] = 0xbb67ae85;
        m_state[2] = 0x3c6ef372;
        m_state[3] = 0xa54ff53a;
        m_state[4] = 0x510e527f;
        m_state[5] = 0x9b05688c;
        m_state[6] = 0x1f83d9ab;
        m_state[7] = 0x5be0cd19;
    }

    void transform() {
        uint32_t m[64];
        for (int i = 0; i < 16; ++i) {
            m[i] = (m_data[i * 4] << 24) | (m_data[i * 4 + 1] << 16) | (m_data[i * 4 + 2] << 8) | (m_data[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
        }
        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t T1 = h + SIG1(e) + choose(e, f, g) + k[i] + m[i];
            uint32_t T2 = SIG0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    uint8_t m_data[64];
    uint32_t m_dataLength;
    uint64_t m_bitLength;
    uint32_t m_state[8];
};


/*

// usage example

#include <iostream>

int main() {
    // your three private ASCII-based keys
    std::string key1 = "your_first_key";
    std::string key2 = "your_second_key";
    std::string key3 = "your_third_key";

    // concatenate the keys
    std::string concatenatedKeys = key1 + key2 + key3;

    // compute the SHA-256 hash of the concatenated keys
    SHA256 sha256;
    sha256.update(concatenatedKeys);
    std::string publicToken = sha256.final();

    // output the public token
    std::cout << "Public Token: " << publicToken << std::endl;

    return 0;
}

*/
