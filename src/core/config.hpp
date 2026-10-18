#pragma once

#include <string>
#include <vector>

namespace cas {

struct S3Config {
    std::string endpoint{"http://w3-us.1llx.workers.dev"};
    std::string region{"us-east-1"};
    std::string bucket{"testers"};
    std::string accessKey{"public"};
    std::string secretKey{"public"};
    bool pathStyle{true};    // true for path-style addressing
    bool useSsl{false};      // false for plain http://

    bool isValid() const {
        return !bucket.empty() && !accessKey.empty() && !secretKey.empty();
    }
};

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager s_inst;
        return s_inst;
    }

    // Get current active S3 config
    const S3Config& getS3() const { return m_s3Config; }
    void setS3(const S3Config& cfg) { m_s3Config = cfg; }

    // Load configuration from standard location (%APPDATA%/cas/config.json or ./cas_config.json)
    bool load(const std::string& customPath = "");

    // Save configuration to disk
    bool save(const std::string& customPath = "") const;

    // Standard application data directory (e.g. C:\Users\<user>\AppData\Roaming\cas)
    static std::string getAppDataDir();

    // Standard config file path
    static std::string getDefaultConfigPath();

    // Standard global repositories list path
    static std::string getDefaultReposPath();

private:
    ConfigManager();
    S3Config m_s3Config;
};

} // namespace cas
