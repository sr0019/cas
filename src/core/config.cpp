#include "config.hpp"
#include "json.hpp"
#include "logger.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace cas {

ConfigManager::ConfigManager() {
    load();
}

std::string ConfigManager::getAppDataDir() {
    std::string appDir = ".cas_appdata";

    WCHAR szPath[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", szPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        int size = WideCharToMultiByte(CP_UTF8, 0, szPath, -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            std::string str(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, szPath, -1, &str[0], size, NULL, NULL);
            appDir = str + "\\cas";
        }
    }

    try {
        if (!fs::exists(appDir)) {
            fs::create_directories(appDir);
        }
    } catch (...) {}

    return appDir;
}

std::string ConfigManager::getDefaultConfigPath() {
    return getAppDataDir() + "\\config.json";
}

std::string ConfigManager::getDefaultReposPath() {
    return getAppDataDir() + "\\repos.json";
}

bool ConfigManager::load(const std::string& customPath) {
    std::string filePath = customPath.empty() ? getDefaultConfigPath() : customPath;

    // Fallback to local ./cas_config.json if appdata not found
    if (!fs::exists(filePath) && fs::exists("cas_config.json")) {
        filePath = "cas_config.json";
    }

    if (!fs::exists(filePath)) {
        return false;
    }

    try {
        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto root = json::Value::parse(content);

        if (root.has("s3") && root["s3"].isObject()) {
            auto s3 = root["s3"];
            m_s3Config.endpoint = s3["endpoint"].asString(m_s3Config.endpoint);
            m_s3Config.region = s3["region"].asString(m_s3Config.region);
            m_s3Config.bucket = s3["bucket"].asString(m_s3Config.bucket);
            m_s3Config.accessKey = s3["accessKey"].asString(m_s3Config.accessKey);
            m_s3Config.secretKey = s3["secretKey"].asString(m_s3Config.secretKey);
            m_s3Config.pathStyle = s3["pathStyle"].asBool(m_s3Config.pathStyle);
            m_s3Config.useSsl = s3["useSsl"].asBool(m_s3Config.useSsl);
        } else {
            m_s3Config.endpoint = root["endpoint"].asString(m_s3Config.endpoint);
            m_s3Config.region = root["region"].asString(m_s3Config.region);
            m_s3Config.bucket = root["bucket"].asString(m_s3Config.bucket);
            m_s3Config.accessKey = root["accessKey"].asString(m_s3Config.accessKey);
            m_s3Config.secretKey = root["secretKey"].asString(m_s3Config.secretKey);
            m_s3Config.pathStyle = root["pathStyle"].asBool(m_s3Config.pathStyle);
            m_s3Config.useSsl = root["useSsl"].asBool(m_s3Config.useSsl);
        }

        return true;
    } catch (const std::exception& ex) {
        Logger::error("Failed to load configuration: " + std::string(ex.what()));
        return false;
    }
}

bool ConfigManager::save(const std::string& customPath) const {
    std::string filePath = customPath.empty() ? getDefaultConfigPath() : customPath;

    try {
        fs::path p = fs::u8path(filePath);
        if (p.has_parent_path() && !fs::exists(p.parent_path())) {
            fs::create_directories(p.parent_path());
        }

        json::Value root(json::Type::Object);
        json::Value s3(json::Type::Object);
        s3["endpoint"] = m_s3Config.endpoint;
        s3["region"] = m_s3Config.region;
        s3["bucket"] = m_s3Config.bucket;
        s3["accessKey"] = m_s3Config.accessKey;
        s3["secretKey"] = m_s3Config.secretKey;
        s3["pathStyle"] = m_s3Config.pathStyle;
        s3["useSsl"] = m_s3Config.useSsl;
        root["s3"] = s3;

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;

        std::string jsonStr = root.stringify(true);
        f.write(jsonStr.data(), jsonStr.size());
        return true;
    } catch (const std::exception& ex) {
        Logger::error("Failed to save configuration: " + std::string(ex.what()));
        return false;
    }
}

} // namespace cas
