#pragma once

#include "config.hpp"
#include "http_client.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace cas {

class S3Client {
public:
    S3Client(const S3Config& config = ConfigManager::instance().getS3());

    void setConfig(const S3Config& config);
    const S3Config& getConfig() const { return m_config; }

    // Test bucket access & credentials
    bool testConnection(std::string& outDetails);

    // Create a bucket on S3
    bool createBucket(const std::string& bucketName, std::string& outDetails);

    // Direct upload of a .vpk pack file to S3
    bool uploadPack(const std::string& packId,
                    const uint8_t* data,
                    size_t size,
                    ProgressCallback progress = nullptr,
                    std::string* outError = nullptr);

    bool uploadPack(const std::string& packId,
                    const std::vector<uint8_t>& buffer,
                    ProgressCallback progress = nullptr,
                    std::string* outError = nullptr);

    // Direct byte-range download of a chunk from S3
    std::vector<uint8_t> downloadChunk(const std::string& packId,
                                       uint64_t offset,
                                       uint32_t length,
                                       ProgressCallback progress = nullptr,
                                       std::string* outError = nullptr);

    // Check if a pack object exists on S3
    bool hasPack(const std::string& packId);

private:
    S3Config m_config;
    HttpClient m_http;

    struct PreparedRequest {
        std::string fullUrl;
        std::string hostHeader;
        std::string canonicalUri;
        std::string canonicalQuery;
        std::map<std::string, std::string> headers;
    };

    PreparedRequest prepareRequest(const std::string& method,
                                   const std::string& key,
                                   const std::string& queryString,
                                   const uint8_t* payload,
                                   size_t payloadLength,
                                   const std::map<std::string, std::string>& extraHeaders);

    void signRequest(PreparedRequest& req,
                     const std::string& method,
                     const uint8_t* payload,
                     size_t payloadLength);
};

} // namespace cas
