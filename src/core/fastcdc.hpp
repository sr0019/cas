#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace cas {

struct ChunkInfo {
    std::string hash;          // 64-character lowercase SHA-256 hex
    uint32_t rawSize{0};       // Length of raw chunk data in bytes
    uint64_t offset{0};        // Byte offset in source file
    std::string sourcePath;    // Optional source file path on disk
    std::vector<uint8_t> data; // Chunk payload (if loaded in memory)
};

class FastCDC {
public:
    FastCDC(uint32_t minSize = 64 * 1024,
            uint32_t avgSize = 256 * 1024,
            uint32_t maxSize = 1024 * 1024);

    // Split in-memory buffer into Content-Defined Chunks
    std::vector<ChunkInfo> chunkBuffer(const uint8_t* buffer, size_t length, uint64_t baseOffset = 0, bool withPayload = true) const;
    std::vector<ChunkInfo> chunkBuffer(const std::vector<uint8_t>& buffer, uint64_t baseOffset = 0, bool withPayload = true) const;

    // Split file on disk into Content-Defined Chunks using constant 4MB streaming buffer
    std::vector<ChunkInfo> chunkFile(const std::string& filePath, bool withPayload = true) const;

    // Adaptive profile selector for files (code vs assets)
    static FastCDC forFile(const std::string& filePath);

    uint32_t getMinSize() const { return m_minSize; }
    uint32_t getAvgSize() const { return m_avgSize; }
    uint32_t getMaxSize() const { return m_maxSize; }

private:
    uint32_t m_minSize;
    uint32_t m_avgSize;
    uint32_t m_maxSize;
    uint32_t m_maskS;
    uint32_t m_maskL;
};

} // namespace cas
