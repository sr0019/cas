#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <functional>

namespace cas {

struct HttpResponse {
    int statusCode{0};
    std::string statusText;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;

    bool isSuccess() const { return statusCode >= 200 && statusCode < 300; }
    std::string asString() const {
        return std::string(reinterpret_cast<const char*>(body.data()), body.size());
    }
};

using ProgressCallback = std::function<void(uint64_t bytesTransferred, uint64_t totalBytes)>;

class HttpClient {
public:
    HttpClient(const std::string& userAgent = "CAS-NativeClient/2.0");
    ~HttpClient();

    // GET request (returns response body)
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {},
                     ProgressCallback progress = nullptr);

    // Byte-range GET request
    HttpResponse getRange(const std::string& url,
                          uint64_t startByte,
                          uint64_t endByte,
                          const std::map<std::string, std::string>& headers = {},
                          ProgressCallback progress = nullptr);

    // HEAD request
    HttpResponse head(const std::string& url,
                      const std::map<std::string, std::string>& headers = {});

    // PUT request (binary buffer)
    HttpResponse put(const std::string& url,
                     const uint8_t* data,
                     size_t length,
                     const std::string& contentType = "application/octet-stream",
                     const std::map<std::string, std::string>& headers = {},
                     ProgressCallback progress = nullptr);

    HttpResponse put(const std::string& url,
                     const std::vector<uint8_t>& buffer,
                     const std::string& contentType = "application/octet-stream",
                     const std::map<std::string, std::string>& headers = {},
                     ProgressCallback progress = nullptr);

    // POST request with JSON or binary payload
    HttpResponse post(const std::string& url,
                      const std::string& jsonBody,
                      const std::map<std::string, std::string>& headers = {});

private:
    std::string m_userAgent;
    void* m_hSession{nullptr}; // HINTERNET

    HttpResponse executeRequest(const std::wstring& verb,
                                const std::string& url,
                                const uint8_t* sendData,
                                size_t sendLength,
                                const std::map<std::string, std::string>& headers,
                                ProgressCallback progress);
};

} // namespace cas
