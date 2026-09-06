#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace WebBridge
{

class Sha1
{
public:
    Sha1()
    {
        reset();
    }

    void update(const uint8_t* data, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
        {
            buffer[bufferSize++] = data[i];
            if (bufferSize == 64)
            {
                processBlock(buffer);
                totalBits += 512;
                bufferSize = 0;
            }
        }
    }

    void update(const std::string& str)
    {
        update(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    std::vector<uint8_t> finalize()
    {
        totalBits += bufferSize * 8;
        buffer[bufferSize++] = 0x80;

        if (bufferSize > 56)
        {
            while (bufferSize < 64)
                buffer[bufferSize++] = 0;
            processBlock(buffer);
            bufferSize = 0;
        }

        while (bufferSize < 56)
            buffer[bufferSize++] = 0;

        for (int i = 7; i >= 0; --i)
            buffer[bufferSize++] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);

        processBlock(buffer);

        std::vector<uint8_t> digest(20);
        for (int i = 0; i < 5; ++i)
        {
            digest[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
        }

        reset();
        return digest;
    }

    static std::vector<uint8_t> compute(const std::string& input)
    {
        Sha1 sha;
        sha.update(input);
        return sha.finalize();
    }

private:
    void reset()
    {
        state[0] = 0x67452301;
        state[1] = 0xEFCDAB89;
        state[2] = 0x98BADCFE;
        state[3] = 0x10325476;
        state[4] = 0xC3D2E1F0;
        totalBits = 0;
        bufferSize = 0;
    }

    static inline uint32_t rol(uint32_t value, size_t bits)
    {
        return (value << bits) | (value >> (32 - bits));
    }

    void processBlock(const uint8_t* block)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

        for (int i = 0; i < 80; ++i)
        {
            uint32_t f = 0, k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
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

    uint32_t state[5];
    uint64_t totalBits;
    uint8_t buffer[64];
    size_t bufferSize;
};

} // namespace WebBridge
