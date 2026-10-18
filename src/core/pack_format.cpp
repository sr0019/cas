#include "pack_format.hpp"
#include "crypto_util.hpp"

#include <zlib.h>
#include <cstring>
#include <random>
#include <stdexcept>
#include <algorithm>

namespace cas {

namespace {

void writeUint16BE(uint8_t* dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[1] = static_cast<uint8_t>(value & 0xFF);
}

void writeUint32BE(uint8_t* dest, uint32_t value) {
    dest[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dest[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dest[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[3] = static_cast<uint8_t>(value & 0xFF);
}

void writeUint64BE(uint8_t* dest, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        dest[i] = static_cast<uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

std::string generateRandomId(size_t len = 32) {
    static const char chars[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(chars[dis(gen)]);
    }
    return s;
}

} // anonymous namespace

std::pair<std::vector<uint8_t>, CodecType> PackFormat::compressChunk(const uint8_t* rawData, size_t rawSize) {
    if (rawSize < 1024) {
        return { std::vector<uint8_t>(rawData, rawData + rawSize), CodecType::NONE };
    }

    uLongf destLen = compressBound(static_cast<uLong>(rawSize));
    std::vector<uint8_t> dest(destLen);

    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if (deflateInit(&strm, 6) != Z_OK) {
        return { std::vector<uint8_t>(rawData, rawData + rawSize), CodecType::NONE };
    }

    strm.next_in = const_cast<Bytef*>(rawData);
    strm.avail_in = static_cast<uInt>(rawSize);
    strm.next_out = dest.data();
    strm.avail_out = static_cast<uInt>(destLen);

    int res = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (res == Z_STREAM_END) {
        size_t compressedSize = strm.total_out;
        if (compressedSize < static_cast<size_t>(rawSize * 0.95)) {
            dest.resize(compressedSize);
            return { dest, CodecType::DEFLATE };
        }
    }

    return { std::vector<uint8_t>(rawData, rawData + rawSize), CodecType::NONE };
}

std::vector<uint8_t> PackFormat::decompressChunk(const uint8_t* storedData, size_t storedSize, CodecType codec, uint32_t expectedRawSize) {
    if (codec == CodecType::NONE) {
        return std::vector<uint8_t>(storedData, storedData + storedSize);
    }

    if (codec == CodecType::DEFLATE) {
        std::vector<uint8_t> dest(expectedRawSize);
        z_stream strm{};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;

        if (inflateInit(&strm) != Z_OK) {
            throw std::runtime_error("inflateInit failed");
        }

        strm.next_in = const_cast<Bytef*>(storedData);
        strm.avail_in = static_cast<uInt>(storedSize);
        strm.next_out = dest.data();
        strm.avail_out = static_cast<uInt>(expectedRawSize);

        int res = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);

        if (res != Z_STREAM_END && res != Z_OK) {
            throw std::runtime_error("zlib inflate decompression error: " + std::to_string(res));
        }

        dest.resize(strm.total_out);
        return dest;
    }

    throw std::runtime_error("Unsupported codec type");
}

bool PackFormat::verifyAndDecompressChunk(const uint8_t* storedData,
                                         size_t storedSize,
                                         CodecType codec,
                                         uint32_t expectedRawSize,
                                         const std::string& expectedSha256,
                                         std::vector<uint8_t>& outRawData,
                                         std::string& outError) {
    try {
        outRawData = decompressChunk(storedData, storedSize, codec, expectedRawSize);
        if (outRawData.size() != expectedRawSize) {
            outError = "Decompressed chunk size mismatch: expected " + std::to_string(expectedRawSize) + ", got " + std::to_string(outRawData.size());
            return false;
        }

        std::string actualHash = CryptoUtil::sha256Hex(outRawData.data(), outRawData.size());
        if (actualHash != expectedSha256) {
            outError = "Chunk SHA-256 mismatch: expected " + expectedSha256 + ", got " + actualHash;
            return false;
        }

        return true;
    } catch (const std::exception& ex) {
        outError = ex.what();
        return false;
    }
}

PackWriter::PackWriter(const std::string& packId)
    : m_packId(packId.empty() ? generateRandomId(32) : packId),
      m_totalRawSize(0) {
}

void PackWriter::addChunk(const std::string& hash, const uint8_t* data, size_t size) {
    auto comp = PackFormat::compressChunk(data, size);

    PendingChunk pc;
    pc.hash = hash;
    pc.rawData.assign(data, data + size);
    pc.storedData = std::move(comp.first);
    pc.rawLength = static_cast<uint32_t>(size);
    pc.storedLength = static_cast<uint32_t>(pc.storedData.size());
    pc.codec = comp.second;

    m_chunks.push_back(std::move(pc));
    m_totalRawSize += size;
}

void PackWriter::addChunk(const std::string& hash, const std::vector<uint8_t>& data) {
    addChunk(hash, data.data(), data.size());
}

BuiltPack PackWriter::build() {
    BuiltPack result;
    result.packId = m_packId;
    result.recordCount = static_cast<uint32_t>(m_chunks.size());
    result.totalRawSize = m_totalRawSize;

    uint64_t totalStoredBytes = 0;
    for (const auto& ch : m_chunks) {
        totalStoredBytes += ch.storedLength;
    }
    result.totalStoredBytes = totalStoredBytes;

    size_t totalBufferSize = PackFormat::PACK_HEADER_SIZE;
    for (const auto& ch : m_chunks) {
        totalBufferSize += PackFormat::RECORD_HEADER_SIZE + ch.storedLength;
    }

    result.buffer.resize(totalBufferSize, 0);
    uint8_t* pBuf = result.buffer.data();

    // 1. Pack Header (64 bytes)
    pBuf[0] = 'C'; pBuf[1] = 'A'; pBuf[2] = 'S'; pBuf[3] = '1';
    writeUint16BE(pBuf + 4, 1);
    writeUint16BE(pBuf + 6, 0);

    std::memset(pBuf + 8, 0, 32);
    size_t idLen = std::min(m_packId.length(), size_t(32));
    std::memcpy(pBuf + 8, m_packId.data(), idLen);

    writeUint32BE(pBuf + 40, result.recordCount);
    writeUint64BE(pBuf + 44, result.totalRawSize);
    writeUint64BE(pBuf + 52, result.totalStoredBytes);

    uint32_t headerCrc = CryptoUtil::crc32(pBuf, 60);
    writeUint32BE(pBuf + 60, headerCrc);

    // 2. Records
    size_t currentOffset = PackFormat::PACK_HEADER_SIZE;

    for (const auto& ch : m_chunks) {
        uint8_t* recHeader = pBuf + currentOffset;

        recHeader[0] = 'R'; recHeader[1] = 'C';

        std::vector<uint8_t> hashBin = CryptoUtil::fromHex(ch.hash);
        if (hashBin.size() == 32) {
            std::memcpy(recHeader + 2, hashBin.data(), 32);
        } else {
            std::memset(recHeader + 2, 0, 32);
        }

        writeUint32BE(recHeader + 34, ch.rawLength);
        writeUint32BE(recHeader + 38, ch.storedLength);
        recHeader[42] = static_cast<uint8_t>(ch.codec);
        recHeader[43] = 0;

        uint32_t payloadCrc = CryptoUtil::crc32(ch.storedData.data(), ch.storedData.size());
        writeUint32BE(recHeader + 44, payloadCrc);

        uint8_t* payloadDest = pBuf + currentOffset + PackFormat::RECORD_HEADER_SIZE;
        std::memcpy(payloadDest, ch.storedData.data(), ch.storedLength);

        ChunkLocation loc;
        loc.id = generateRandomId(32);
        loc.chunkHash = ch.hash;
        loc.packId = m_packId;
        loc.payloadOffset = currentOffset + PackFormat::RECORD_HEADER_SIZE;
        loc.storedLength = ch.storedLength;
        loc.rawLength = ch.rawLength;
        loc.codec = ch.codec;
        result.locations.push_back(loc);

        currentOffset += PackFormat::RECORD_HEADER_SIZE + ch.storedLength;
    }

    result.objectSize = result.buffer.size();
    result.checksum = CryptoUtil::sha256Hex(result.buffer.data(), result.buffer.size());

    return result;
}

} // namespace cas
