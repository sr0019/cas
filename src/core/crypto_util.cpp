#include "crypto_util.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <random>
#include <ctime>

namespace fs = std::filesystem;

namespace cas {

namespace {

// Standard IEEE 802.3 CRC32 table
static uint32_t s_crcTable[256];
static bool s_crcInitialized = false;

void initCrcTable() {
    if (s_crcInitialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crcTable[i] = c;
    }
    s_crcInitialized = true;
}

} // anonymous namespace

std::string CryptoUtil::toHex(const uint8_t* data, size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        hex.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

std::string CryptoUtil::toHex(const std::vector<uint8_t>& data) {
    return toHex(data.data(), data.size());
}

std::vector<uint8_t> CryptoUtil::fromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.length() % 2 != 0) return bytes;
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        char h1 = hex[i];
        char h2 = hex[i + 1];
        auto val = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        bytes.push_back(static_cast<uint8_t>((val(h1) << 4) | val(h2)));
    }
    return bytes;
}

std::vector<uint8_t> CryptoUtil::sha256Raw(const void* data, size_t length) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::vector<uint8_t> hash(32, 0);

    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }

    if (length > 0) {
        status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)length, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            throw std::runtime_error("BCryptHashData failed");
        }
    }

    status = BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptFinishHash failed");
    }

    return hash;
}

std::vector<uint8_t> CryptoUtil::sha256Raw(const std::vector<uint8_t>& buffer) {
    return sha256Raw(buffer.data(), buffer.size());
}

std::string CryptoUtil::sha256Hex(const void* data, size_t length) {
    auto hash = sha256Raw(data, length);
    return toHex(hash.data(), hash.size());
}

std::string CryptoUtil::sha256Hex(const std::vector<uint8_t>& buffer) {
    return sha256Hex(buffer.data(), buffer.size());
}

std::string CryptoUtil::sha256Hex(const std::string& str) {
    return sha256Hex(str.data(), str.size());
}

std::string CryptoUtil::sha256File(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for hashing: " + filePath);
    }

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::vector<uint8_t> hash(32, 0);

    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }

    constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1 MB buffer
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            status = BCryptHashData(hHash, (PUCHAR)buffer.data(), (ULONG)bytesRead, 0);
            if (!BCRYPT_SUCCESS(status)) {
                BCryptDestroyHash(hHash);
                BCryptCloseAlgorithmProvider(hAlg, 0);
                throw std::runtime_error("BCryptHashData failed during file streaming");
            }
        }
    }

    status = BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptFinishHash failed for file");
    }

    return toHex(hash.data(), hash.size());
}

std::vector<uint8_t> CryptoUtil::hmacSha256(const std::vector<uint8_t>& key, const void* data, size_t length) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider HMAC failed");
    }

    BCRYPT_HASH_HANDLE hHash = NULL;
    status = BCryptCreateHash(hAlg, &hHash, NULL, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("BCryptCreateHash HMAC failed");
    }

    if (length > 0 && data != nullptr) {
        status = BCryptHashData(hHash, (PUCHAR)data, (ULONG)length, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            throw std::runtime_error("BCryptHashData HMAC failed");
        }
    }

    std::vector<uint8_t> mac(32, 0);
    status = BCryptFinishHash(hHash, mac.data(), (ULONG)mac.size(), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptFinishHash HMAC failed");
    }

    return mac;
}

std::vector<uint8_t> CryptoUtil::hmacSha256(const std::vector<uint8_t>& key, const std::string& data) {
    return hmacSha256(key, data.data(), data.size());
}

std::vector<uint8_t> CryptoUtil::hmacSha256(const std::string& keyStr, const std::string& data) {
    std::vector<uint8_t> key(keyStr.begin(), keyStr.end());
    return hmacSha256(key, data.data(), data.size());
}

std::string CryptoUtil::hmacSha256Hex(const std::vector<uint8_t>& key, const std::string& data) {
    auto mac = hmacSha256(key, data);
    return toHex(mac.data(), mac.size());
}

std::string CryptoUtil::hmacSha256Hex(const std::string& keyStr, const std::string& data) {
    auto mac = hmacSha256(keyStr, data);
    return toHex(mac.data(), mac.size());
}

uint32_t CryptoUtil::crc32(const void* data, size_t length) {
    initCrcTable();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc = s_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t CryptoUtil::crc32(const std::vector<uint8_t>& buffer) {
    return crc32(buffer.data(), buffer.size());
}

std::pair<std::string, std::string> CryptoUtil::getIso8601Utc() {
    SYSTEMTIME st;
    GetSystemTime(&st);

    char fullIso[32];
    snprintf(fullIso, sizeof(fullIso), "%04d%02d%02dT%02d%02d%02dZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    char dateStamp[16];
    snprintf(dateStamp, sizeof(dateStamp), "%04d%02d%02d",
             st.wYear, st.wMonth, st.wDay);

    return { std::string(fullIso), std::string(dateStamp) };
}

std::string CryptoUtil::formatBytes(uint64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    std::ostringstream ss;
    if (unitIndex == 0) {
        ss << bytes << " B";
    } else {
        ss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    }
    return ss.str();
}

std::string CryptoUtil::randomId(size_t hexLength) {
    static std::mt19937_64 rng(std::random_device{}());
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(hexLength);
    for (size_t i = 0; i < hexLength; ++i) {
        result.push_back(hexChars[rng() % 16]);
    }
    return result;
}

} // namespace cas
