#include "fastcdc.hpp"
#include "crypto_util.hpp"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cctype>
#include <set>

namespace fs = std::filesystem;

namespace cas {

namespace {

static const uint32_t GEAR_TABLE[256] = {
    0x86a3b239, 0x509f1d0b, 0xd6c268a7, 0x1d365f57, 0x76b297b3, 0x07f4336f, 0x553e18fb, 0x24e2ecf9,
    0xc58c730e, 0x759c5d07, 0x82f42a6c, 0xb802dc45, 0x3d3d63b2, 0x3c965b78, 0x6e9f2910, 0x9815049b,
    0x1e367839, 0xa31e7f34, 0x6ff052f5, 0xfa355a29, 0xd0f5f7f8, 0x48df9a88, 0x2863cb64, 0x8479e0a2,
    0x86f059ff, 0x75ea4a6c, 0x30fb0362, 0xd88a44ec, 0x367f00f0, 0x582496a7, 0x9330cf2b, 0x6e5a610e,
    0xb9c6c4f3, 0x37856461, 0x904323e2, 0x477fb477, 0x19a0d845, 0x6c6b3017, 0x5f67e411, 0x70d510bb,
    0xd7d7cb91, 0x5ec4d5c1, 0x86c23b20, 0x3ac4d1d8, 0x1c9842a2, 0x31b7fa25, 0x9a3f2a1b, 0xa1d5292c,
    0x6e2c31e4, 0x136a0f6b, 0xd1a9a834, 0x6543be2e, 0x56a4fb6f, 0x217d8481, 0x7a2d4807, 0x2280d2d3,
    0x8e833f92, 0x6a2c2702, 0xb696e1ec, 0x2101e149, 0x9953259a, 0x301ffb07, 0x8d27fb46, 0x32e92c68,
    0x7d6f59c8, 0x5c7f8a7e, 0x7892b1a8, 0x1e32dc34, 0x49f2b3e8, 0x25a66a1e, 0xa72d2361, 0x7a0d4c82,
    0x318d197e, 0x3c5520e6, 0x82f42a6c, 0xb802dc45, 0x973a4b64, 0x3c965b78, 0x6e9f2910, 0x9815049b,
    0x1e367839, 0xa31e7f34, 0x6ff052f5, 0xfa355a29, 0xd0f5f7f8, 0x48df9a88, 0x2863cb64, 0x8479e0a2,
    0x86f059ff, 0x75ea4a6c, 0x30fb0362, 0xd88a44ec, 0x367f00f0, 0x582496a7, 0x9330cf2b, 0x6e5a610e,
    0xb9c6c4f3, 0x37856461, 0x904323e2, 0x477fb477, 0x19a0d845, 0x6c6b3017, 0x5f67e411, 0x70d510bb,
    0xd7d7cb91, 0x5ec4d5c1, 0x86c23b20, 0x3ac4d1d8, 0x1c9842a2, 0x31b7fa25, 0x9a3f2a1b, 0xa1d5292c,
    0x6e2c31e4, 0x136a0f6b, 0xd1a9a834, 0x6543be2e, 0x56a4fb6f, 0x217d8481, 0x7a2d4807, 0x2280d2d3,
    0x8e833f92, 0x6a2c2702, 0xb696e1ec, 0x2101e149, 0x9953259a, 0x301ffb07, 0x8d27fb46, 0x32e92c68,
    0xd5c3a4f8, 0x26c7e2b1, 0x49f2b3e8, 0x25a66a1e, 0xa72d2361, 0x7a0d4c82, 0x318d197e, 0x3c5520e6,
    0x82f42a6c, 0xb802dc45, 0x973a4b64, 0x3c965b78, 0x6e9f2910, 0x9815049b, 0x1e367839, 0xa31e7f34,
    0x6ff052f5, 0xfa355a29, 0xd0f5f7f8, 0x48df9a88, 0x2863cb64, 0x8479e0a2, 0x86f059ff, 0x75ea4a6c,
    0x30fb0362, 0xd88a44ec, 0x367f00f0, 0x582496a7, 0x9330cf2b, 0x6e5a610e, 0xb9c6c4f3, 0x37856461,
    0x904323e2, 0x477fb477, 0x19a0d845, 0x6c6b3017, 0x5f67e411, 0x70d510bb, 0xd7d7cb91, 0x5ec4d5c1,
    0x86c23b20, 0x3ac4d1d8, 0x1c9842a2, 0x31b7fa25, 0x9a3f2a1b, 0xa1d5292c, 0x6e2c31e4, 0x136a0f6b,
    0xd1a9a834, 0x6543be2e, 0x56a4fb6f, 0x217d8481, 0x7a2d4807, 0x2280d2d3, 0x8e833f92, 0x6a2c2702,
    0xb696e1ec, 0x2101e149, 0x9953259a, 0x301ffb07, 0x8d27fb46, 0x32e92c68, 0x7d6f59c8, 0x5c7f8a7e,
    0x7892b1a8, 0x1e32dc34, 0x49f2b3e8, 0x25a66a1e, 0xa72d2361, 0x7a0d4c82, 0x318d197e, 0x3c5520e6,
    0x82f42a6c, 0xb802dc45, 0x973a4b64, 0x3c965b78, 0x6e9f2910, 0x9815049b, 0x1e367839, 0xa31e7f34,
    0x6ff052f5, 0xfa355a29, 0xd0f5f7f8, 0x48df9a88, 0x2863cb64, 0x8479e0a2, 0x86f059ff, 0x75ea4a6c,
    0x30fb0362, 0xd88a44ec, 0x367f00f0, 0x582496a7, 0x9330cf2b, 0x6e5a610e, 0xb9c6c4f3, 0x37856461,
    0x904323e2, 0x477fb477, 0x19a0d845, 0x6c6b3017, 0x5f67e411, 0x70d510bb, 0xd7d7cb91, 0x5ec4d5c1,
    0x86c23b20, 0x3ac4d1d8, 0x1c9842a2, 0x31b7fa25, 0x9a3f2a1b, 0xa1d5292c, 0x6e2c31e4, 0x136a0f6b,
    0xd1a9a834, 0x6543be2e, 0x56a4fb6f, 0x217d8481, 0x7a2d4807, 0x2280d2d3, 0x8e833f92, 0x6a2c2702,
    0xb696e1ec, 0x2101e149, 0x9953259a, 0x301ffb07, 0x8d27fb46, 0x32e92c68, 0x7d6f59c8, 0x5c7f8a7e
};

} // anonymous namespace

FastCDC::FastCDC(uint32_t minSize, uint32_t avgSize, uint32_t maxSize)
    : m_minSize(minSize), m_avgSize(avgSize), m_maxSize(maxSize) {
    int bits = static_cast<int>(std::round(std::log2(static_cast<double>(m_avgSize))));
    m_maskS = (1u << (bits + 1)) - 1u;
    m_maskL = (1u << (bits - 1)) - 1u;
}

std::vector<ChunkInfo> FastCDC::chunkBuffer(const uint8_t* buffer, size_t length, uint64_t baseOffset, bool withPayload) const {
    std::vector<ChunkInfo> chunks;
    if (!buffer || length == 0) return chunks;

    size_t chunkStart = 0;

    while (chunkStart < length) {
        size_t remaining = length - chunkStart;

        // If remaining bytes <= minSize, this is the final chunk
        if (remaining <= m_minSize) {
            const uint8_t* slicePtr = buffer + chunkStart;
            size_t sliceLen = remaining;
            std::string hash = CryptoUtil::sha256Hex(slicePtr, sliceLen);

            ChunkInfo ci;
            ci.hash = hash;
            ci.rawSize = static_cast<uint32_t>(sliceLen);
            ci.offset = baseOffset + chunkStart;
            if (withPayload) {
                ci.data.assign(slicePtr, slicePtr + sliceLen);
            }
            chunks.push_back(std::move(ci));
            break;
        }

        // Normalization phase:
        size_t offset = chunkStart + m_minSize;
        size_t targetBoundary = chunkStart + m_avgSize;
        size_t maxBoundary = std::min(chunkStart + static_cast<size_t>(m_maxSize), length);

        uint32_t fingerprint = 0;
        bool cutFound = false;

        // Stage 1: between minSize and avgSize (stricter maskS)
        size_t stage1Limit = std::min(targetBoundary, maxBoundary);
        while (offset < stage1Limit) {
            uint8_t b = buffer[offset];
            fingerprint = (fingerprint << 1) + GEAR_TABLE[b];
            if ((fingerprint & m_maskS) == 0) {
                offset++;
                cutFound = true;
                break;
            }
            offset++;
        }

        // Stage 2: between avgSize and maxSize (looser maskL) if cut not found
        if (!cutFound) {
            while (offset < maxBoundary) {
                uint8_t b = buffer[offset];
                fingerprint = (fingerprint << 1) + GEAR_TABLE[b];
                if ((fingerprint & m_maskL) == 0) {
                    offset++;
                    cutFound = true;
                    break;
                }
                offset++;
            }
        }

        // Cut at maxBoundary if no boundary matched
        size_t chunkEnd = offset;
        size_t sliceLen = chunkEnd - chunkStart;
        const uint8_t* slicePtr = buffer + chunkStart;

        std::string hash = CryptoUtil::sha256Hex(slicePtr, sliceLen);

        ChunkInfo ci;
        ci.hash = hash;
        ci.rawSize = static_cast<uint32_t>(sliceLen);
        ci.offset = baseOffset + chunkStart;
        if (withPayload) {
            ci.data.assign(slicePtr, slicePtr + sliceLen);
        }
        chunks.push_back(std::move(ci));

        chunkStart = chunkEnd;
    }

    return chunks;
}

std::vector<ChunkInfo> FastCDC::chunkBuffer(const std::vector<uint8_t>& buffer, uint64_t baseOffset, bool withPayload) const {
    return chunkBuffer(buffer.data(), buffer.size(), baseOffset, withPayload);
}

std::vector<ChunkInfo> FastCDC::chunkFile(const std::string& filePath, bool withPayload) const {
    fs::path u8p = fs::u8path(filePath);
    std::ifstream file(u8p, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for chunking: " + filePath);
    }

    std::vector<ChunkInfo> chunks;
    const size_t BUFFER_CAPACITY = 4 * 1024 * 1024; // Constant 4 MB streaming window
    std::vector<uint8_t> buffer(BUFFER_CAPACITY);

    size_t validDataLen = 0;
    uint64_t currentFileOffset = 0;

    while (file.good() || validDataLen > 0) {
        if (file.good() && validDataLen < BUFFER_CAPACITY) {
            file.read(reinterpret_cast<char*>(buffer.data() + validDataLen), BUFFER_CAPACITY - validDataLen);
            validDataLen += static_cast<size_t>(file.gcount());
        }

        if (validDataLen == 0) break;

        bool isEof = !file.good();

        if (isEof) {
            // Reached EOF: chunk whatever remains in the buffer
            auto tailChunks = chunkBuffer(buffer.data(), validDataLen, currentFileOffset, withPayload);
            for (auto& tc : tailChunks) {
                tc.sourcePath = filePath;
            }
            chunks.insert(chunks.end(), std::make_move_iterator(tailChunks.begin()), std::make_move_iterator(tailChunks.end()));
            break;
        }

        // We can safely cut chunks as long as (chunkStart + m_maxSize <= validDataLen).
        size_t chunkStart = 0;
        bool madeCut = false;

        while (chunkStart + m_maxSize <= validDataLen) {
            size_t offset = chunkStart + m_minSize;
            size_t targetBoundary = chunkStart + m_avgSize;
            size_t maxBoundary = chunkStart + m_maxSize;

            uint32_t fingerprint = 0;
            bool cutFound = false;

            size_t stage1Limit = targetBoundary;
            while (offset < stage1Limit) {
                uint8_t b = buffer[offset];
                fingerprint = (fingerprint << 1) + GEAR_TABLE[b];
                if ((fingerprint & m_maskS) == 0) {
                    offset++;
                    cutFound = true;
                    break;
                }
                offset++;
            }

            if (!cutFound) {
                while (offset < maxBoundary) {
                    uint8_t b = buffer[offset];
                    fingerprint = (fingerprint << 1) + GEAR_TABLE[b];
                    if ((fingerprint & m_maskL) == 0) {
                        offset++;
                        cutFound = true;
                        break;
                    }
                    offset++;
                }
            }

            size_t chunkEnd = offset;
            size_t sliceLen = chunkEnd - chunkStart;
            const uint8_t* slicePtr = buffer.data() + chunkStart;

            std::string hash = CryptoUtil::sha256Hex(slicePtr, sliceLen);

            ChunkInfo ci;
            ci.hash = hash;
            ci.rawSize = static_cast<uint32_t>(sliceLen);
            ci.offset = currentFileOffset + chunkStart;
            ci.sourcePath = filePath;
            if (withPayload) {
                ci.data.assign(slicePtr, slicePtr + sliceLen);
            }
            chunks.push_back(std::move(ci));

            chunkStart = chunkEnd;
            madeCut = true;
        }

        if (chunkStart > 0) {
            currentFileOffset += chunkStart;
            size_t remainingTail = validDataLen - chunkStart;
            if (remainingTail > 0) {
                std::memmove(buffer.data(), buffer.data() + chunkStart, remainingTail);
            }
            validDataLen = remainingTail;
        } else if (!madeCut) {
            break;
        }
    }

    return chunks;
}

FastCDC FastCDC::forFile(const std::string& filePath) {
    std::string ext;
    size_t dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos) {
        ext = filePath.substr(dotPos);
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    }

    static const std::set<std::string> codeExtensions = {
        ".exe", ".dll", ".so", ".dylib", ".json", ".xml", ".ini",
        ".cfg", ".lua", ".txt", ".js", ".ts", ".cs", ".cpp", ".c",
        ".hpp", ".h", ".py", ".yaml", ".yml", ".toml", ".bat", ".cmd",
        ".sh", ".ps1", ".md", ".csv"
    };

    if (codeExtensions.find(ext) != codeExtensions.end()) {
        return FastCDC(16 * 1024, 64 * 1024, 256 * 1024);
    }

    return FastCDC(64 * 1024, 256 * 1024, 1024 * 1024);
}

} // namespace cas
