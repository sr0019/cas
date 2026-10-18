#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace cas {

// Codec constants matching pack-format
enum class CodecType : uint8_t {
    NONE = 0,
    DEFLATE = 1
};

inline const char* codecToString(CodecType c) {
    switch (c) {
        case CodecType::DEFLATE: return "deflate";
        default: return "none";
    }
}

inline CodecType stringToCodec(const std::string& str) {
    if (str == "deflate") return CodecType::DEFLATE;
    return CodecType::NONE;
}

struct ChunkLocation {
    std::string id;          // UUID
    std::string chunkHash;   // 64-character SHA-256 hex
    std::string packId;      // 32-character pack identifier
    uint64_t payloadOffset{0};  // Absolute byte offset in S3 pack object
    uint32_t storedLength{0};   // Stored byte length (compressed or raw)
    uint32_t rawLength{0};      // Uncompressed byte length
    CodecType codec{CodecType::NONE}; // Compression codec
};

struct BuiltPack {
    std::string packId;
    std::vector<uint8_t> buffer;
    size_t objectSize{0};
    uint32_t recordCount{0};
    std::string checksum;    // SHA-256 hex of complete pack buffer
    uint64_t totalRawSize{0};
    uint64_t totalStoredBytes{0};
    std::vector<ChunkLocation> locations;
};

class PackFormat {
public:
    static constexpr size_t PACK_HEADER_SIZE = 64;
    static constexpr size_t RECORD_HEADER_SIZE = 48;

    // Compress chunk data using zlib deflate level 6 if beneficial
    static std::pair<std::vector<uint8_t>, CodecType> compressChunk(const uint8_t* rawData, size_t rawSize);

    // Decompress chunk data using zlib inflate
    static std::vector<uint8_t> decompressChunk(const uint8_t* storedData, size_t storedSize, CodecType codec, uint32_t expectedRawSize);

    // Verify chunk integrity: payload CRC32 and decompressed SHA-256
    static bool verifyAndDecompressChunk(const uint8_t* storedData,
                                         size_t storedSize,
                                         CodecType codec,
                                         uint32_t expectedRawSize,
                                         const std::string& expectedSha256,
                                         std::vector<uint8_t>& outRawData,
                                         std::string& outError);
};

class PackWriter {
public:
    PackWriter(const std::string& packId = "");

    const std::string& getPackId() const { return m_packId; }
    size_t getChunkCount() const { return m_chunks.size(); }
    uint64_t getTotalRawSize() const { return m_totalRawSize; }

    // Add a chunk to be packed
    void addChunk(const std::string& hash, const uint8_t* data, size_t size);
    void addChunk(const std::string& hash, const std::vector<uint8_t>& data);

    // Assemble and return the complete binary VPK1 pack buffer
    BuiltPack build();

private:
    struct PendingChunk {
        std::string hash;
        std::vector<uint8_t> rawData;
        std::vector<uint8_t> storedData;
        uint32_t rawLength{0};
        uint32_t storedLength{0};
        CodecType codec{CodecType::NONE};
    };

    std::string m_packId;
    std::vector<PendingChunk> m_chunks;
    uint64_t m_totalRawSize{0};
};

} // namespace cas
