#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <utility>

namespace cas {

class CryptoUtil {
public:
    // Compute 64-character lowercase hex SHA-256 of memory buffer
    static std::string sha256Hex(const void* data, size_t length);
    static std::string sha256Hex(const std::vector<uint8_t>& buffer);
    static std::string sha256Hex(const std::string& str);

    // Compute binary 32-byte SHA-256
    static std::vector<uint8_t> sha256Raw(const void* data, size_t length);
    static std::vector<uint8_t> sha256Raw(const std::vector<uint8_t>& buffer);

    // Compute SHA-256 of a file on disk (streamed)
    static std::string sha256File(const std::string& filePath);

    // Compute HMAC-SHA256 (key, data) -> binary 32 bytes
    static std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const void* data, size_t length);
    static std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::string& data);
    static std::vector<uint8_t> hmacSha256(const std::string& keyStr, const std::string& data);

    // Compute HMAC-SHA256 returning 64-char lowercase hex
    static std::string hmacSha256Hex(const std::vector<uint8_t>& key, const std::string& data);
    static std::string hmacSha256Hex(const std::string& keyStr, const std::string& data);

    // Compute IEEE 802.3 CRC32
    static uint32_t crc32(const void* data, size_t length);
    static uint32_t crc32(const std::vector<uint8_t>& buffer);

    // Hex encode / decode helpers
    static std::string toHex(const uint8_t* data, size_t length);
    static std::string toHex(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> fromHex(const std::string& hex);

    // UTC timestamps: returns { "20261018T120000Z", "20261018" }
    static std::pair<std::string, std::string> getIso8601Utc();

    // Human readable size formatter (e.g. "14.2 MB", "850 KB")
    static std::string formatBytes(uint64_t bytes);

    // Short random ID generator (hex)
    static std::string randomId(size_t hexLength = 16);
};

} // namespace cas
