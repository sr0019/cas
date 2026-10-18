#include "http_client.hpp"

#include <windows.h>
#include <winhttp.h>
#include <stdexcept>
#include <iostream>
#include <sstream>

namespace cas {

namespace {

std::wstring toWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

std::string toString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

struct AutoWinHttpHandle {
    HINTERNET handle{NULL};
    explicit AutoWinHttpHandle(HINTERNET h = NULL) : handle(h) {}
    ~AutoWinHttpHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
            handle = NULL;
        }
    }
    AutoWinHttpHandle(const AutoWinHttpHandle&) = delete;
    AutoWinHttpHandle& operator=(const AutoWinHttpHandle&) = delete;
    operator HINTERNET() const { return handle; }
    HINTERNET get() const { return handle; }
    void reset(HINTERNET h = NULL) {
        if (handle) WinHttpCloseHandle(handle);
        handle = h;
    }
};

} // anonymous namespace

HttpClient::HttpClient(const std::string& userAgent)
    : m_userAgent(userAgent) {
    std::wstring wAgent = toWString(m_userAgent);
    m_hSession = WinHttpOpen(wAgent.c_str(),
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS,
                             0);
    if (!m_hSession) {
        throw std::runtime_error("WinHttpOpen failed to initialize HTTP session");
    }

    // Connect timeouts: Resolve(10s), Connect(15s), Send(60s), Receive(120s)
    WinHttpSetTimeouts((HINTERNET)m_hSession, 10000, 15000, 60000, 120000);
}

HttpClient::~HttpClient() {
    if (m_hSession) {
        WinHttpCloseHandle((HINTERNET)m_hSession);
        m_hSession = nullptr;
    }
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             ProgressCallback progress) {
    return executeRequest(L"GET", url, nullptr, 0, headers, progress);
}

HttpResponse HttpClient::getRange(const std::string& url,
                                  uint64_t startByte,
                                  uint64_t endByte,
                                  const std::map<std::string, std::string>& headers,
                                  ProgressCallback progress) {
    auto reqHeaders = headers;
    reqHeaders["Range"] = "bytes=" + std::to_string(startByte) + "-" + std::to_string(endByte);
    return executeRequest(L"GET", url, nullptr, 0, reqHeaders, progress);
}

HttpResponse HttpClient::head(const std::string& url,
                              const std::map<std::string, std::string>& headers) {
    return executeRequest(L"HEAD", url, nullptr, 0, headers, nullptr);
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& jsonBody,
                              const std::map<std::string, std::string>& headers) {
    auto reqHeaders = headers;
    reqHeaders["Content-Type"] = "application/json";
    return executeRequest(L"POST", url,
                          reinterpret_cast<const uint8_t*>(jsonBody.data()),
                          jsonBody.size(),
                          reqHeaders, nullptr);
}

HttpResponse HttpClient::put(const std::string& url,
                             const uint8_t* data,
                             size_t length,
                             const std::string& contentType,
                             const std::map<std::string, std::string>& headers,
                             ProgressCallback progress) {
    auto reqHeaders = headers;
    if (!contentType.empty() && reqHeaders.find("Content-Type") == reqHeaders.end()) {
        reqHeaders["Content-Type"] = contentType;
    }
    return executeRequest(L"PUT", url, data, length, reqHeaders, progress);
}

HttpResponse HttpClient::put(const std::string& url,
                             const std::vector<uint8_t>& buffer,
                             const std::string& contentType,
                             const std::map<std::string, std::string>& headers,
                             ProgressCallback progress) {
    return put(url, buffer.data(), buffer.size(), contentType, headers, progress);
}

HttpResponse HttpClient::executeRequest(const std::wstring& verb,
                                        const std::string& url,
                                        const uint8_t* sendData,
                                        size_t sendLength,
                                        const std::map<std::string, std::string>& headers,
                                        ProgressCallback progress) {
    HttpResponse response;

    std::wstring wUrl = toWString(url);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        throw std::runtime_error("Invalid URL format: " + url);
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";

    bool isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = urlComp.nPort;

    AutoWinHttpHandle hConnect(WinHttpConnect((HINTERNET)m_hSession, host.c_str(), port, 0));
    if (!hConnect) {
        throw std::runtime_error("WinHttpConnect failed to connect to host: " + toString(host));
    }

    DWORD reqFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    AutoWinHttpHandle hRequest(WinHttpOpenRequest(hConnect,
                                                 verb.c_str(),
                                                 path.c_str(),
                                                 NULL,
                                                 WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 reqFlags));
    if (!hRequest) {
        throw std::runtime_error("WinHttpOpenRequest failed for path: " + toString(path));
    }

    if (isHttps) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    std::wstring wHeaders;
    for (const auto& h : headers) {
        wHeaders += toWString(h.first) + L": " + toWString(h.second) + L"\r\n";
    }

    DWORD totalSendBytes = static_cast<DWORD>(sendLength);
    BOOL sendOk = WinHttpSendRequest(hRequest,
                                     wHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wHeaders.c_str(),
                                     (DWORD)wHeaders.length(),
                                     WINHTTP_NO_REQUEST_DATA,
                                     0,
                                     totalSendBytes,
                                     0);

    if (!sendOk) {
        DWORD err = GetLastError();
        throw std::runtime_error("WinHttpSendRequest failed with error code: " + std::to_string(err));
    }

    // Stream sendData in 64 KB chunks if uploading
    if (sendData && sendLength > 0) {
        const size_t CHUNK_SIZE = 64 * 1024;
        size_t bytesSent = 0;

        while (bytesSent < sendLength) {
            size_t toWrite = (std::min)(CHUNK_SIZE, sendLength - bytesSent);
            DWORD written = 0;

            if (!WinHttpWriteData(hRequest, sendData + bytesSent, (DWORD)toWrite, &written)) {
                throw std::runtime_error("WinHttpWriteData failed while uploading");
            }

            bytesSent += written;
            if (progress) {
                progress(bytesSent, sendLength);
            }
        }
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        throw std::runtime_error("WinHttpReceiveResponse failed with error code: " + std::to_string(err));
    }

    // Read HTTP Status Code
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &dwStatusCode,
                        &dwSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = static_cast<int>(dwStatusCode);

    // Read Status Text
    WCHAR szStatusText[256] = {0};
    dwSize = sizeof(szStatusText);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_TEXT,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            szStatusText,
                            &dwSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        response.statusText = toString(szStatusText);
    }

    // If HEAD request, do not read body
    if (verb == L"HEAD") {
        return response;
    }

    // Determine expected content length if available
    uint64_t expectedBodySize = 0;
    WCHAR szContentLength[64] = {0};
    dwSize = sizeof(szContentLength);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            szContentLength,
                            &dwSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        try {
            expectedBodySize = std::stoull(toString(szContentLength));
        } catch (...) {}
    }

    // Read Response Body
    DWORD dwDownloaded = 0;
    uint64_t totalRead = 0;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        size_t currentPos = response.body.size();
        response.body.resize(currentPos + dwSize);

        if (!WinHttpReadData(hRequest, response.body.data() + currentPos, dwSize, &dwDownloaded)) {
            response.body.resize(currentPos);
            break;
        }

        response.body.resize(currentPos + dwDownloaded);
        totalRead += dwDownloaded;

        if (progress && expectedBodySize > 0) {
            progress(totalRead, expectedBodySize);
        }
    } while (dwSize > 0);

    return response;
}

} // namespace cas
