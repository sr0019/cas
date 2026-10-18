#include "s3_client.hpp"
#include "crypto_util.hpp"
#include "logger.hpp"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace cas {

namespace {

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string toLower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // anonymous namespace

S3Client::S3Client(const S3Config& config)
    : m_config(config) {
}

void S3Client::setConfig(const S3Config& config) {
    m_config = config;
}

S3Client::PreparedRequest S3Client::prepareRequest(const std::string& method,
                                                  const std::string& key,
                                                  const std::string& queryString,
                                                  const uint8_t* payload,
                                                  size_t payloadLength,
                                                  const std::map<std::string, std::string>& extraHeaders) {
    PreparedRequest req;

    std::string endpoint = m_config.endpoint;
    if (endpoint.empty()) {
        endpoint = "http://w3-us.1llx.workers.dev";
    }

    bool isHttps = m_config.useSsl;
    if (endpoint.rfind("http://", 0) == 0) {
        isHttps = false;
        endpoint = endpoint.substr(7);
    } else if (endpoint.rfind("https://", 0) == 0) {
        isHttps = true;
        endpoint = endpoint.substr(8);
    }

    // Strip trailing slashes
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }

    // Determine path style vs virtual host style
    bool usePathStyle = m_config.pathStyle;
    if (endpoint.find("localhost") != std::string::npos ||
        endpoint.find("127.0.0.1") != std::string::npos ||
        endpoint.find(".workers.dev") != std::string::npos ||
        endpoint.find(':') != std::string::npos) {
        usePathStyle = true;
    }

    std::string scheme = isHttps ? "https://" : "http://";
    std::string cleanKey = key;
    while (!cleanKey.empty() && cleanKey.front() == '/') {
        cleanKey.erase(0, 1);
    }

    if (usePathStyle) {
        req.hostHeader = endpoint;
        req.canonicalUri = "/" + m_config.bucket;
        if (!cleanKey.empty()) {
            req.canonicalUri += "/" + cleanKey;
        }
        req.fullUrl = scheme + endpoint + req.canonicalUri;
    } else {
        req.hostHeader = m_config.bucket + "." + endpoint;
        req.canonicalUri = "/" + cleanKey;
        if (req.canonicalUri.empty()) req.canonicalUri = "/";
        req.fullUrl = scheme + req.hostHeader + req.canonicalUri;
    }

    req.canonicalQuery = queryString;
    if (!queryString.empty()) {
        req.fullUrl += "?" + queryString;
    }

    req.headers = extraHeaders;
    signRequest(req, method, payload, payloadLength);

    return req;
}

void S3Client::signRequest(PreparedRequest& req,
                           const std::string& method,
                           const uint8_t* payload,
                           size_t payloadLength) {
    auto timePair = CryptoUtil::getIso8601Utc();
    std::string amzDate = timePair.first;
    std::string dateStamp = timePair.second;

    std::string payloadHash = (payload && payloadLength > 0)
        ? CryptoUtil::sha256Hex(payload, payloadLength)
        : "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

    req.headers["Host"] = req.hostHeader;
    req.headers["x-amz-date"] = amzDate;
    req.headers["x-amz-content-sha256"] = payloadHash;

    // Collect headers to sign (sorted alphabetically by lowercase name)
    std::map<std::string, std::string> canonicalHeadersMap;
    for (const auto& pair : req.headers) {
        canonicalHeadersMap[toLower(pair.first)] = trim(pair.second);
    }

    std::string canonicalHeadersStr;
    std::string signedHeadersStr;
    bool firstH = true;

    for (const auto& pair : canonicalHeadersMap) {
        canonicalHeadersStr += pair.first + ":" + pair.second + "\n";
        if (!firstH) signedHeadersStr += ";";
        signedHeadersStr += pair.first;
        firstH = false;
    }

    // 1. Canonical Request
    std::string canonicalRequest = method + "\n" +
                                   req.canonicalUri + "\n" +
                                   req.canonicalQuery + "\n" +
                                   canonicalHeadersStr + "\n" +
                                   signedHeadersStr + "\n" +
                                   payloadHash;

    std::string canonicalRequestHash = CryptoUtil::sha256Hex(canonicalRequest);

    // 2. String to Sign
    std::string credentialScope = dateStamp + "/" + m_config.region + "/s3/aws4_request";
    std::string stringToSign = "AWS4-HMAC-SHA256\n" +
                               amzDate + "\n" +
                               credentialScope + "\n" +
                               canonicalRequestHash;

    // 3. Signature Key Derivation
    std::string kSecret = "AWS4" + m_config.secretKey;
    auto kDate    = CryptoUtil::hmacSha256(kSecret, dateStamp);
    auto kRegion  = CryptoUtil::hmacSha256(kDate, m_config.region);
    auto kService = CryptoUtil::hmacSha256(kRegion, "s3");
    auto kSigning = CryptoUtil::hmacSha256(kService, "aws4_request");

    std::string signature = CryptoUtil::hmacSha256Hex(kSigning, stringToSign);

    // 4. Authorization Header
    std::string authHeader = "AWS4-HMAC-SHA256 Credential=" + m_config.accessKey + "/" + credentialScope +
                             ", SignedHeaders=" + signedHeadersStr +
                             ", Signature=" + signature;

    req.headers["Authorization"] = authHeader;
}

bool S3Client::createBucket(const std::string& bucketName, std::string& outDetails) {
    std::string bkt = bucketName.empty() ? m_config.bucket : bucketName;
    try {
        Logger::info("Creating S3 bucket '" + bkt + "' at " + m_config.endpoint + " ...");
        auto savedBucket = m_config.bucket;
        m_config.bucket = bkt;
        auto req = prepareRequest("PUT", "", "", nullptr, 0, {});
        m_config.bucket = savedBucket;

        auto resp = m_http.put(req.fullUrl, nullptr, 0, "", req.headers);
        if (resp.isSuccess() || resp.statusCode == 200 || resp.statusCode == 204) {
            outDetails = "Bucket '" + bkt + "' created successfully (HTTP " + std::to_string(resp.statusCode) + ")";
            Logger::success(outDetails);
            return true;
        } else if (resp.asString().find("BucketAlreadyOwnedByYou") != std::string::npos ||
                   resp.asString().find("BucketAlreadyExists") != std::string::npos) {
            outDetails = "Bucket '" + bkt + "' already exists and is ready.";
            Logger::info(outDetails);
            return true;
        } else {
            outDetails = "Bucket creation returned HTTP " + std::to_string(resp.statusCode) + ": " + resp.asString();
            Logger::warn(outDetails);
            return false;
        }
    } catch (const std::exception& ex) {
        outDetails = "Failed to create bucket: " + std::string(ex.what());
        Logger::error(outDetails);
        return false;
    }
}

bool S3Client::testConnection(std::string& outDetails) {
    if (!m_config.isValid()) {
        outDetails = "Incomplete S3 configuration (Bucket, Access Key, and Secret Key required)";
        return false;
    }

    try {
        Logger::info("Testing direct S3 connection to bucket '" + m_config.bucket + "' at " + m_config.endpoint + " ...");
        auto req = prepareRequest("GET", "", "max-keys=1", nullptr, 0, {});
        auto resp = m_http.get(req.fullUrl, req.headers);

        if (resp.isSuccess()) {
            outDetails = "Direct S3 connection successful! Bucket '" + m_config.bucket + "' is accessible (HTTP " + std::to_string(resp.statusCode) + ")";
            Logger::success(outDetails);
            return true;
        } else if (resp.statusCode == 404) {
            Logger::info("Bucket '" + m_config.bucket + "' does not exist (HTTP 404). Attempting to auto-create bucket...");
            std::string createDetails;
            if (createBucket(m_config.bucket, createDetails)) {
                outDetails = "Bucket '" + m_config.bucket + "' created and verified successfully!";
                Logger::success(outDetails);
                return true;
            }
            outDetails = "S3 Bucket '" + m_config.bucket + "' does not exist (HTTP 404). " + createDetails;
            return false;
        } else if (resp.statusCode == 403) {
            outDetails = "S3 Access Denied (HTTP 403 Forbidden). Check your Access Key ID and Secret Access Key.";
            Logger::error(outDetails);
            return false;
        } else {
            outDetails = "S3 connection returned HTTP " + std::to_string(resp.statusCode) + ": " + resp.statusText;
            Logger::warn(outDetails);
            return resp.statusCode < 500;
        }
    } catch (const std::exception& ex) {
        outDetails = "S3 connection failed: " + std::string(ex.what());
        Logger::error(outDetails);
        return false;
    }
}

bool S3Client::uploadPack(const std::string& packId,
                          const uint8_t* data,
                          size_t size,
                          ProgressCallback progress,
                          std::string* outError) {
    if (!data || size == 0) {
        if (outError) *outError = "Empty pack data";
        return false;
    }

    std::string key = "packs/" + packId + ".vpk";
    std::map<std::string, std::string> extraHeaders;
    extraHeaders["Content-Type"] = "application/octet-stream";

    try {
        Logger::info("Direct S3 PUT: uploading pack '" + key + "' (" + CryptoUtil::formatBytes(size) + ") ...");
        auto req = prepareRequest("PUT", key, "", data, size, extraHeaders);
        auto resp = m_http.put(req.fullUrl, data, size, "application/octet-stream", req.headers, progress);

        if (resp.isSuccess()) {
            Logger::success("Direct S3 upload complete: " + key);
            return true;
        } else {
            std::string err = "S3 PUT failed with HTTP " + std::to_string(resp.statusCode) + ": " + resp.asString();
            if (outError) *outError = err;
            Logger::error(err);
            return false;
        }
    } catch (const std::exception& ex) {
        std::string err = "S3 PUT exception: " + std::string(ex.what());
        if (outError) *outError = err;
        Logger::error(err);
        return false;
    }
}

bool S3Client::uploadPack(const std::string& packId,
                          const std::vector<uint8_t>& buffer,
                          ProgressCallback progress,
                          std::string* outError) {
    return uploadPack(packId, buffer.data(), buffer.size(), progress, outError);
}

std::vector<uint8_t> S3Client::downloadChunk(const std::string& packId,
                                             uint64_t offset,
                                             uint32_t length,
                                             ProgressCallback progress,
                                             std::string* outError) {
    std::string key = "packs/" + packId + ".vpk";
    std::string rangeVal = "bytes=" + std::to_string(offset) + "-" + std::to_string(offset + length - 1);

    std::map<std::string, std::string> extraHeaders;
    extraHeaders["Range"] = rangeVal;

    try {
        auto req = prepareRequest("GET", key, "", nullptr, 0, extraHeaders);
        auto resp = m_http.get(req.fullUrl, req.headers, progress);

        if (resp.statusCode == 200 || resp.statusCode == 206) {
            return resp.body;
        } else {
            std::string err = "S3 Range GET failed with HTTP " + std::to_string(resp.statusCode);
            if (outError) *outError = err;
            Logger::error(err);
            return {};
        }
    } catch (const std::exception& ex) {
        std::string err = "S3 Range GET exception: " + std::string(ex.what());
        if (outError) *outError = err;
        Logger::error(err);
        return {};
    }
}

bool S3Client::hasPack(const std::string& packId) {
    std::string key = "packs/" + packId + ".vpk";
    try {
        auto req = prepareRequest("HEAD", key, "", nullptr, 0, {});
        auto resp = m_http.head(req.fullUrl, req.headers);
        return resp.isSuccess();
    } catch (...) {
        return false;
    }
}

} // namespace cas
