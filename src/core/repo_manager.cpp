#include "repo_manager.hpp"
#include "crypto_util.hpp"
#include "json.hpp"
#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace cas {

namespace {

std::string normalizePath(const std::string& path) {
    if (path.empty()) return "";
    try {
        fs::path p = fs::u8path(path);
        std::string s = p.lexically_normal().string();
        std::replace(s.begin(), s.end(), '/', '\\');
        while (!s.empty() && s.back() == '\\') s.pop_back();
        return s;
    } catch (...) {
        return path;
    }
}

std::string getIsoTimeNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &tt);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

bool shouldIgnore(const std::string& relPath) {
    size_t start = 0;
    while (start < relPath.size()) {
        size_t end = relPath.find_first_of("/\\", start);
        std::string part = (end == std::string::npos) ? relPath.substr(start) : relPath.substr(start, end - start);
        if (part == ".cas" || part == ".vstore" || part == ".git" || part == ".vs" || part == ".idea" || part == "node_modules") {
            return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

} // anonymous namespace

RepoManager::RepoManager()
    : m_s3(ConfigManager::instance().getS3()) {
}

std::string RepoManager::getCasDir(const std::string& repoPath) {
    std::string base = normalizePath(repoPath);
    // Backward compatibility if .vstore exists but not .cas
    if (!fs::exists(base + "\\.cas") && fs::exists(base + "\\.vstore")) {
        return base + "\\.vstore";
    }
    return base + "\\.cas";
}

std::string RepoManager::getManifestsDir(const std::string& repoPath) {
    return getCasDir(repoPath) + "\\manifests";
}

std::string RepoManager::getCacheDir(const std::string& repoPath) {
    return getCasDir(repoPath) + "\\cache";
}

std::string RepoManager::getCatalogPath(const std::string& repoPath) {
    return getCasDir(repoPath) + "\\catalog.json";
}

std::string RepoManager::getHistoryPath(const std::string& repoPath) {
    return getCasDir(repoPath) + "\\history.json";
}

std::string RepoManager::getRepoConfigPath(const std::string& repoPath) {
    return getCasDir(repoPath) + "\\config.json";
}

std::vector<RepoInfo> RepoManager::listRepos() {
    std::vector<RepoInfo> result;
    std::string reposPath = ConfigManager::getDefaultReposPath();

    if (!fs::exists(reposPath)) {
        return result;
    }

    try {
        std::ifstream f(reposPath, std::ios::binary);
        if (!f.is_open()) return result;

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto root = json::Value::parse(content);

        if (root.has("repositories") && root["repositories"].isArray()) {
            for (size_t i = 0; i < root["repositories"].size(); ++i) {
                auto item = root["repositories"][i];
                RepoInfo info;
                info.id = item["id"].asString();
                info.name = item["name"].asString();
                info.path = item["path"].asString();
                info.headVersion = item["headVersion"].asString();
                info.lastCommitTime = item["lastCommitTime"].asString();
                info.totalVersions = static_cast<size_t>(item["totalVersions"].asInt64(0));

                if (fs::exists(info.path)) {
                    result.push_back(info);
                }
            }
        }
    } catch (...) {}

    return result;
}

void RepoManager::saveReposRegistry(const std::vector<RepoInfo>& repos) {
    std::string reposPath = ConfigManager::getDefaultReposPath();
    try {
        fs::path p = fs::u8path(reposPath);
        if (p.has_parent_path() && !fs::exists(p.parent_path())) {
            fs::create_directories(p.parent_path());
        }

        json::Value root(json::Type::Object);
        json::Value arr(json::Type::Array);

        for (const auto& r : repos) {
            json::Value item(json::Type::Object);
            item["id"] = r.id;
            item["name"] = r.name;
            item["path"] = r.path;
            item["headVersion"] = r.headVersion;
            item["lastCommitTime"] = r.lastCommitTime;
            item["totalVersions"] = static_cast<int64_t>(r.totalVersions);
            arr.push_back(item);
        }

        root["repositories"] = arr;

        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            std::string str = root.stringify(true);
            f.write(str.data(), str.size());
        }
    } catch (...) {}
}

bool RepoManager::initRepo(const std::string& repoPath, const std::string& name) {
    std::string normPath = normalizePath(repoPath);
    if (!fs::exists(normPath)) {
        try {
            fs::create_directories(normPath);
        } catch (...) {
            return false;
        }
    }

    std::string casDir = getCasDir(normPath);
    std::string manifestsDir = getManifestsDir(normPath);
    std::string cacheDir = getCacheDir(normPath);

    try {
        fs::create_directories(casDir);
        fs::create_directories(manifestsDir);
        fs::create_directories(cacheDir);

        std::string cfgPath = getRepoConfigPath(normPath);
        if (!fs::exists(cfgPath)) {
            std::string repoName = name;
            if (repoName.empty()) {
                fs::path p = fs::u8path(normPath);
                repoName = p.filename().string();
                if (repoName.empty()) repoName = "repo_" + CryptoUtil::randomId(6);
            }

            json::Value cfg(json::Type::Object);
            cfg["id"] = "repo_" + CryptoUtil::randomId(12);
            cfg["name"] = repoName;
            cfg["createdAt"] = getIsoTimeNow();
            cfg["headVersion"] = "";

            std::ofstream f(cfgPath, std::ios::binary | std::ios::trunc);
            std::string s = cfg.stringify(true);
            f.write(s.data(), s.size());
        }

        std::string histPath = getHistoryPath(normPath);
        if (!fs::exists(histPath)) {
            json::Value hist(json::Type::Array);
            std::ofstream f(histPath, std::ios::binary | std::ios::trunc);
            std::string s = hist.stringify(true);
            f.write(s.data(), s.size());
        }

        std::string catPath = getCatalogPath(normPath);
        if (!fs::exists(catPath)) {
            json::Value cat(json::Type::Object);
            cat["chunks"] = json::Value(json::Type::Object);
            std::ofstream f(catPath, std::ios::binary | std::ios::trunc);
            std::string s = cat.stringify(true);
            f.write(s.data(), s.size());
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool RepoManager::addRepo(const std::string& path, const std::string& customName) {
    std::string normPath = normalizePath(path);
    if (!fs::exists(normPath)) {
        Logger::error("Directory does not exist: " + normPath);
        return false;
    }

    if (!initRepo(normPath, customName)) {
        Logger::error("Failed to initialize .cas repository in " + normPath);
        return false;
    }

    // Read repo config
    std::string repoName = customName;
    std::string repoId = "";
    std::string headVer = "";

    try {
        std::ifstream f(getRepoConfigPath(normPath), std::ios::binary);
        if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto root = json::Value::parse(content);
            repoId = root["id"].asString();
            if (repoName.empty()) repoName = root["name"].asString();
            headVer = root["headVersion"].asString();
        }
    } catch (...) {}

    auto history = getHistory(normPath);

    auto repos = listRepos();
    bool exists = false;
    for (auto& r : repos) {
        if (normalizePath(r.path) == normPath) {
            r.name = repoName;
            r.headVersion = headVer;
            r.totalVersions = history.size();
            exists = true;
            break;
        }
    }

    if (!exists) {
        RepoInfo info;
        info.id = repoId.empty() ? ("repo_" + CryptoUtil::randomId(12)) : repoId;
        info.name = repoName;
        info.path = normPath;
        info.headVersion = headVer;
        info.lastCommitTime = history.empty() ? getIsoTimeNow() : history.back().timestamp;
        info.totalVersions = history.size();
        repos.push_back(info);
    }

    saveReposRegistry(repos);
    Logger::success("Registered repository '" + repoName + "' (" + normPath + ")");
    return true;
}

bool RepoManager::removeRepo(const std::string& nameOrPath) {
    std::string target = normalizePath(nameOrPath);
    auto repos = listRepos();
    auto it = std::remove_if(repos.begin(), repos.end(), [&](const RepoInfo& r) {
        return r.name == nameOrPath || normalizePath(r.path) == target || r.id == nameOrPath;
    });

    if (it != repos.end()) {
        repos.erase(it, repos.end());
        saveReposRegistry(repos);
        Logger::success("Removed repository reference: " + nameOrPath);
        return true;
    }
    return false;
}

bool RepoManager::getRepo(const std::string& nameOrPath, RepoInfo& outRepo) {
    std::string target = normalizePath(nameOrPath);
    auto repos = listRepos();
    for (const auto& r : repos) {
        if (r.name == nameOrPath || normalizePath(r.path) == target || r.id == nameOrPath) {
            outRepo = r;
            return true;
        }
    }
    return false;
}

std::vector<std::string> RepoManager::scanWorkingDir(const std::string& rootDir) {
    std::vector<std::string> files;
    fs::path root = fs::u8path(rootDir);

    if (!fs::exists(root)) return files;

    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file()) {
            std::string relPath = fs::relative(entry.path(), root).generic_string();
            if (!shouldIgnore(relPath)) {
                files.push_back(relPath);
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::map<std::string, ChunkLocation> RepoManager::loadCatalog(const std::string& repoPath) {
    std::map<std::string, ChunkLocation> catalog;
    std::string catPath = getCatalogPath(repoPath);

    if (!fs::exists(catPath)) return catalog;

    try {
        std::ifstream f(catPath, std::ios::binary);
        if (!f.is_open()) return catalog;

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto root = json::Value::parse(content);

        if (root.has("chunks") && root["chunks"].isObject()) {
            for (const auto& pair : root["chunks"].getObject()) {
                const auto& item = pair.second;
                ChunkLocation loc;
                loc.chunkHash = pair.first;
                loc.packId = item["packId"].asString();
                loc.payloadOffset = item["payloadOffset"].asUInt64(0);
                loc.storedLength = static_cast<uint32_t>(item["storedLength"].asUInt64(0));
                loc.rawLength = static_cast<uint32_t>(item["rawLength"].asUInt64(0));
                loc.codec = stringToCodec(item["codec"].asString("none"));
                catalog[loc.chunkHash] = loc;
            }
        }
    } catch (...) {}

    return catalog;
}

void RepoManager::saveCatalog(const std::string& repoPath, const std::map<std::string, ChunkLocation>& catalog) {
    std::string catPath = getCatalogPath(repoPath);
    try {
        json::Value root(json::Type::Object);
        json::Value chunksObj(json::Type::Object);

        for (const auto& pair : catalog) {
            const auto& loc = pair.second;
            json::Value item(json::Type::Object);
            item["packId"] = loc.packId;
            item["payloadOffset"] = loc.payloadOffset;
            item["storedLength"] = static_cast<uint64_t>(loc.storedLength);
            item["rawLength"] = static_cast<uint64_t>(loc.rawLength);
            item["codec"] = codecToString(loc.codec);
            chunksObj[pair.first] = item;
        }

        root["chunks"] = chunksObj;

        std::ofstream f(catPath, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            std::string s = root.stringify(false);
            f.write(s.data(), s.size());
        }
    } catch (...) {}
}

std::vector<CommitRecord> RepoManager::getHistory(const std::string& repoPath) {
    std::vector<CommitRecord> history;
    std::string histPath = getHistoryPath(repoPath);

    if (!fs::exists(histPath)) return history;

    try {
        std::ifstream f(histPath, std::ios::binary);
        if (!f.is_open()) return history;

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto root = json::Value::parse(content);

        if (root.isArray()) {
            for (size_t i = 0; i < root.size(); ++i) {
                auto item = root[i];
                CommitRecord rec;
                rec.versionId = item["versionId"].asString();
                rec.versionNumber = static_cast<uint32_t>(item["versionNumber"].asInt64(0));
                rec.versionTag = item["versionTag"].asString();
                rec.message = item["message"].asString();
                rec.timestamp = item["timestamp"].asString();
                rec.author = item["author"].asString("local");
                rec.manifestHash = item["manifestHash"].asString();
                rec.fileCount = static_cast<size_t>(item["fileCount"].asInt64(0));
                rec.totalRawBytes = item["totalRawBytes"].asUInt64(0);
                rec.totalStoredBytes = item["totalStoredBytes"].asUInt64(0);
                rec.totalChunks = static_cast<size_t>(item["totalChunks"].asInt64(0));
                rec.reusedChunks = static_cast<size_t>(item["reusedChunks"].asInt64(0));
                rec.newChunks = static_cast<size_t>(item["newChunks"].asInt64(0));
                rec.dedupSavingsBytes = item["dedupSavingsBytes"].asUInt64(0);
                rec.dedupRatio = item["dedupRatio"].asString("0%");
                history.push_back(rec);
            }
        }
    } catch (...) {}

    return history;
}

std::string RepoManager::resolveVersionId(const std::string& repoPath, const std::string& versionIdOrTag) {
    auto history = getHistory(repoPath);
    if (history.empty()) return "";

    if (versionIdOrTag.empty() || versionIdOrTag == "latest" || versionIdOrTag == "HEAD") {
        return history.back().versionId;
    }

    for (const auto& rec : history) {
        if (rec.versionTag == versionIdOrTag || rec.versionId == versionIdOrTag) {
            return rec.versionId;
        }
        if (rec.versionId.rfind(versionIdOrTag, 0) == 0) {
            return rec.versionId;
        }
    }

    return "";
}

bool RepoManager::getManifest(const std::string& repoPath, const std::string& versionIdOrTag, VersionManifest& outManifest) {
    std::string vId = resolveVersionId(repoPath, versionIdOrTag);
    if (vId.empty()) return false;

    std::string manifestFile = getManifestsDir(repoPath) + "\\" + vId + ".json";
    if (!fs::exists(manifestFile)) return false;

    try {
        std::ifstream f(manifestFile, std::ios::binary);
        if (!f.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto root = json::Value::parse(content);

        outManifest.versionId = root["versionId"].asString();
        outManifest.versionNumber = static_cast<uint32_t>(root["versionNumber"].asInt64(0));
        outManifest.repoName = root["repoName"].asString();
        outManifest.timestamp = root["timestamp"].asString();
        outManifest.message = root["message"].asString();
        outManifest.totalFiles = static_cast<size_t>(root["totalFiles"].asInt64(0));
        outManifest.totalRawBytes = root["totalRawBytes"].asUInt64(0);

        outManifest.files.clear();
        if (root.has("files") && root["files"].isArray()) {
            for (size_t i = 0; i < root["files"].size(); ++i) {
                auto fItem = root["files"][i];
                ManifestFile mf;
                mf.path = fItem["path"].asString();
                mf.size = fItem["size"].asUInt64(0);
                mf.fileSha256 = fItem["fileSha256"].asString();
                mf.mtime = fItem["mtime"].asUInt64(0);

                if (fItem.has("chunks") && fItem["chunks"].isArray()) {
                    for (size_t j = 0; j < fItem["chunks"].size(); ++j) {
                        auto cItem = fItem["chunks"][j];
                        ManifestFileChunk mfc;
                        mfc.hash = cItem["hash"].asString();
                        mfc.rawLength = static_cast<uint32_t>(cItem["rawLength"].asUInt64(0));
                        mfc.storedLength = static_cast<uint32_t>(cItem["storedLength"].asUInt64(0));
                        mfc.packId = cItem["packId"].asString();
                        mfc.payloadOffset = cItem["payloadOffset"].asUInt64(0);
                        mfc.codec = cItem["codec"].asString("none");
                        mf.chunks.push_back(mfc);
                    }
                }

                outManifest.files.push_back(mf);
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

CommitMetrics RepoManager::commit(const std::string& repoPath,
                                  const std::string& message,
                                  ProgressCallback progress,
                                  std::atomic<bool>* cancelToken) {
    auto startTime = std::chrono::high_resolution_clock::now();
    std::string normPath = normalizePath(repoPath);

    if (!initRepo(normPath)) {
        throw std::runtime_error("Could not initialize repository at " + normPath);
    }

    // Refresh S3 config from ConfigManager
    m_s3.setConfig(ConfigManager::instance().getS3());

    auto files = scanWorkingDir(normPath);
    if (files.empty()) {
        Logger::warn("No files found in " + normPath);
    }

    auto history = getHistory(normPath);
    uint32_t nextVersionNum = static_cast<uint32_t>(history.size() + 1);
    std::string versionTag = "v" + std::to_string(nextVersionNum);
    std::string versionId = CryptoUtil::randomId(12);

    Logger::info("Starting commit [" + versionTag + "] '" + message + "' for " + std::to_string(files.size()) + " files...");

    auto catalog = loadCatalog(normPath);

    CommitMetrics metrics;
    metrics.versionId = versionId;
    metrics.versionTag = versionTag;
    metrics.fileCount = files.size();

    VersionManifest manifest;
    manifest.versionId = versionId;
    manifest.versionNumber = nextVersionNum;
    manifest.repoName = fs::u8path(normPath).filename().string();
    manifest.timestamp = getIsoTimeNow();
    manifest.message = message.empty() ? ("Snapshot " + versionTag) : message;
    manifest.totalFiles = files.size();

    PackWriter packWriter;
    const uint64_t MAX_PACK_SIZE = 8 * 1024 * 1024; // 8 MB pack limit

    auto flushPack = [&]() {
        if (packWriter.getChunkCount() == 0) return;
        BuiltPack pack = packWriter.build();

        std::string err;
        bool ok = m_s3.uploadPack(pack.packId, pack.buffer, progress, &err);
        if (!ok) {
            // Attempt to create bucket if it does not exist
            std::string createDetails;
            if (m_s3.createBucket(m_s3.getConfig().bucket, createDetails)) {
                ok = m_s3.uploadPack(pack.packId, pack.buffer, progress, &err);
            }
            if (!ok) {
                throw std::runtime_error("Direct S3 upload failed for pack " + pack.packId + ": " + err);
            }
        }

        metrics.bytesUploaded += pack.objectSize;
        metrics.packsUploaded++;

        for (const auto& loc : pack.locations) {
            catalog[loc.chunkHash] = loc;
        }

        packWriter = PackWriter();
    };

    uint64_t totalOriginalBytes = 0;

    for (size_t fIdx = 0; fIdx < files.size(); ++fIdx) {
        if (cancelToken && cancelToken->load()) {
            throw std::runtime_error("Commit operation cancelled by user");
        }

        const auto& relPath = files[fIdx];
        std::string fullPath = normPath + "\\" + relPath;

        uint64_t fileSize = fs::file_size(fs::u8path(fullPath));
        totalOriginalBytes += fileSize;

        ManifestFile mFile;
        mFile.path = relPath;
        mFile.size = fileSize;
        mFile.fileSha256 = CryptoUtil::sha256File(fullPath);
        mFile.mtime = static_cast<uint64_t>(fs::last_write_time(fs::u8path(fullPath)).time_since_epoch().count());

        FastCDC chunker = FastCDC::forFile(fullPath);
        auto chunks = chunker.chunkFile(fullPath, true);

        metrics.totalChunks += chunks.size();

        for (const auto& ch : chunks) {
            ManifestFileChunk mfc;
            mfc.hash = ch.hash;
            mfc.rawLength = ch.rawSize;

            auto it = catalog.find(ch.hash);
            if (it != catalog.end()) {
                metrics.reusedChunks++;
                metrics.bytesReused += ch.rawSize;

                mfc.storedLength = it->second.storedLength;
                mfc.packId = it->second.packId;
                mfc.payloadOffset = it->second.payloadOffset;
                mfc.codec = codecToString(it->second.codec);
            } else {
                metrics.newChunks++;
                packWriter.addChunk(ch.hash, ch.data);

                mfc.storedLength = ch.rawSize;
                mfc.packId = packWriter.getPackId();
                mfc.payloadOffset = 0;
                mfc.codec = "none";

                if (packWriter.getTotalRawSize() >= MAX_PACK_SIZE) {
                    flushPack();
                }
            }

            mFile.chunks.push_back(mfc);
        }

        manifest.files.push_back(mFile);

        if (progress) {
            progress(fIdx + 1, files.size());
        }
    }

    flushPack();

    for (auto& mf : manifest.files) {
        for (auto& ch : mf.chunks) {
            auto it = catalog.find(ch.hash);
            if (it != catalog.end()) {
                ch.packId = it->second.packId;
                ch.payloadOffset = it->second.payloadOffset;
                ch.storedLength = it->second.storedLength;
                ch.codec = codecToString(it->second.codec);
            }
        }
    }

    manifest.totalRawBytes = totalOriginalBytes;
    metrics.totalRawBytes = totalOriginalBytes;
    metrics.totalStoredBytes = metrics.bytesUploaded;

    saveCatalog(normPath, catalog);

    json::Value manJson(json::Type::Object);
    manJson["format"] = "cas-manifest-v1";
    manJson["versionId"] = manifest.versionId;
    manJson["versionNumber"] = manifest.versionNumber;
    manJson["repoName"] = manifest.repoName;
    manJson["timestamp"] = manifest.timestamp;
    manJson["message"] = manifest.message;
    manJson["totalFiles"] = static_cast<int64_t>(manifest.totalFiles);
    manJson["totalRawBytes"] = manifest.totalRawBytes;

    json::Value filesArr(json::Type::Array);
    for (const auto& f : manifest.files) {
        json::Value fVal(json::Type::Object);
        fVal["path"] = f.path;
        fVal["size"] = f.size;
        fVal["fileSha256"] = f.fileSha256;
        fVal["mtime"] = f.mtime;

        json::Value chunksArr(json::Type::Array);
        for (const auto& c : f.chunks) {
            json::Value cVal(json::Type::Object);
            cVal["hash"] = c.hash;
            cVal["rawLength"] = c.rawLength;
            cVal["storedLength"] = c.storedLength;
            cVal["packId"] = c.packId;
            cVal["payloadOffset"] = c.payloadOffset;
            cVal["codec"] = c.codec;
            chunksArr.push_back(cVal);
        }
        fVal["chunks"] = chunksArr;
        filesArr.push_back(fVal);
    }
    manJson["files"] = filesArr;

    std::string manStr = manJson.stringify(true);
    std::string manifestHash = CryptoUtil::sha256Hex(manStr);

    std::string manifestPath = getManifestsDir(normPath) + "\\" + versionId + ".json";
    std::ofstream mfFile(manifestPath, std::ios::binary | std::ios::trunc);
    mfFile.write(manStr.data(), manStr.size());
    mfFile.close();

    uint64_t savings = metrics.bytesReused;
    double ratio = (totalOriginalBytes > 0) ? (static_cast<double>(savings) / totalOriginalBytes * 100.0) : 0.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << ratio << "%";
    metrics.dedupRatio = ss.str();

    CommitRecord rec;
    rec.versionId = versionId;
    rec.versionNumber = nextVersionNum;
    rec.versionTag = versionTag;
    rec.message = manifest.message;
    rec.timestamp = manifest.timestamp;
    rec.author = "local";
    rec.manifestHash = manifestHash;
    rec.fileCount = files.size();
    rec.totalRawBytes = totalOriginalBytes;
    rec.totalStoredBytes = metrics.bytesUploaded;
    rec.totalChunks = metrics.totalChunks;
    rec.reusedChunks = metrics.reusedChunks;
    rec.newChunks = metrics.newChunks;
    rec.dedupSavingsBytes = savings;
    rec.dedupRatio = metrics.dedupRatio;

    history.push_back(rec);

    json::Value histArr(json::Type::Array);
    for (const auto& h : history) {
        json::Value hItem(json::Type::Object);
        hItem["versionId"] = h.versionId;
        hItem["versionNumber"] = static_cast<int64_t>(h.versionNumber);
        hItem["versionTag"] = h.versionTag;
        hItem["message"] = h.message;
        hItem["timestamp"] = h.timestamp;
        hItem["author"] = h.author;
        hItem["manifestHash"] = h.manifestHash;
        hItem["fileCount"] = static_cast<int64_t>(h.fileCount);
        hItem["totalRawBytes"] = h.totalRawBytes;
        hItem["totalStoredBytes"] = h.totalStoredBytes;
        hItem["totalChunks"] = static_cast<int64_t>(h.totalChunks);
        hItem["reusedChunks"] = static_cast<int64_t>(h.reusedChunks);
        hItem["newChunks"] = static_cast<int64_t>(h.newChunks);
        hItem["dedupSavingsBytes"] = h.dedupSavingsBytes;
        hItem["dedupRatio"] = h.dedupRatio;
        histArr.push_back(hItem);
    }

    std::ofstream hFile(getHistoryPath(normPath), std::ios::binary | std::ios::trunc);
    std::string hStr = histArr.stringify(true);
    hFile.write(hStr.data(), hStr.size());
    hFile.close();

    try {
        std::ifstream cf(getRepoConfigPath(normPath), std::ios::binary);
        if (cf.is_open()) {
            std::string cStr((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
            auto cRoot = json::Value::parse(cStr);
            cRoot["headVersion"] = versionTag;
            cf.close();

            std::ofstream cOut(getRepoConfigPath(normPath), std::ios::binary | std::ios::trunc);
            std::string updated = cRoot.stringify(true);
            cOut.write(updated.data(), updated.size());
        }
    } catch (...) {}

    auto repos = listRepos();
    for (auto& r : repos) {
        if (normalizePath(r.path) == normPath) {
            r.headVersion = versionTag;
            r.lastCommitTime = manifest.timestamp;
            r.totalVersions = history.size();
            break;
        }
    }
    saveReposRegistry(repos);

    auto endTime = std::chrono::high_resolution_clock::now();
    metrics.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    Logger::success("Commit [" + versionTag + "] finished in " + std::to_string(metrics.elapsedSeconds).substr(0, 4) +
                   "s. Saved " + CryptoUtil::formatBytes(savings) + " (" + metrics.dedupRatio + " dedup). Uploaded: " +
                   CryptoUtil::formatBytes(metrics.bytesUploaded));

    return metrics;
}

RollbackMetrics RepoManager::rollback(const std::string& repoPath,
                                      const std::string& targetVersionOrTag,
                                      const std::string& customTargetPath,
                                      ProgressCallback progress,
                                      std::atomic<bool>* cancelToken) {
    auto startTime = std::chrono::high_resolution_clock::now();
    std::string normPath = normalizePath(repoPath);

    VersionManifest manifest;
    if (!getManifest(normPath, targetVersionOrTag, manifest)) {
        throw std::runtime_error("Version manifest not found for: " + targetVersionOrTag);
    }

    m_s3.setConfig(ConfigManager::instance().getS3());

    std::string destDir = customTargetPath.empty() ? normPath : normalizePath(customTargetPath);
    bool isWorkingDir = (destDir == normPath);

    Logger::info("Rolling back to version [" + manifest.versionId.substr(0, 8) + "] (v" +
                 std::to_string(manifest.versionNumber) + ") in " + destDir + " ...");

    RollbackMetrics metrics;
    metrics.targetVersion = "v" + std::to_string(manifest.versionNumber);
    metrics.totalFiles = manifest.files.size();
    metrics.totalBytes = manifest.totalRawBytes;

    std::string cacheDir = getCacheDir(normPath);
    fs::create_directories(cacheDir);

    std::set<std::string> manifestPaths;

    for (size_t fIdx = 0; fIdx < manifest.files.size(); ++fIdx) {
        if (cancelToken && cancelToken->load()) {
            throw std::runtime_error("Rollback cancelled by user");
        }

        const auto& mf = manifest.files[fIdx];
        manifestPaths.insert(mf.path);

        std::string targetFilePath = destDir + "\\" + mf.path;
        fs::path p = fs::u8path(targetFilePath);

        if (fs::exists(p) && fs::file_size(p) == mf.size) {
            std::string currentHash = CryptoUtil::sha256File(targetFilePath);
            if (currentHash == mf.fileSha256) {
                metrics.identicalFilesSkipped++;
                if (progress) progress(fIdx + 1, manifest.files.size());
                continue;
            }
        }

        if (p.has_parent_path() && !fs::exists(p.parent_path())) {
            fs::create_directories(p.parent_path());
        }

        std::ofstream outFile(p, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            throw std::runtime_error("Cannot create destination file: " + targetFilePath);
        }

        for (const auto& ch : mf.chunks) {
            std::vector<uint8_t> rawChunk;
            std::string cacheChunkPath = cacheDir + "\\" + ch.hash + ".bin";

            if (fs::exists(cacheChunkPath) && fs::file_size(fs::u8path(cacheChunkPath)) == ch.rawLength) {
                std::ifstream cFile(cacheChunkPath, std::ios::binary);
                rawChunk.resize(ch.rawLength);
                cFile.read(reinterpret_cast<char*>(rawChunk.data()), ch.rawLength);
                metrics.chunksFromCache++;
            } else {
                std::string err;
                auto storedData = m_s3.downloadChunk(ch.packId, ch.payloadOffset, ch.storedLength, nullptr, &err);
                if (storedData.empty()) {
                    throw std::runtime_error("Failed to download chunk " + ch.hash + " from S3: " + err);
                }

                CodecType codec = stringToCodec(ch.codec);
                std::string decompressErr;
                bool ok = PackFormat::verifyAndDecompressChunk(storedData.data(), storedData.size(), codec, ch.rawLength, ch.hash, rawChunk, decompressErr);
                if (!ok) {
                    throw std::runtime_error("Chunk integrity verification failed: " + decompressErr);
                }

                metrics.chunksFromS3++;
                metrics.bytesDownloaded += storedData.size();

                std::ofstream cOut(cacheChunkPath, std::ios::binary | std::ios::trunc);
                if (cOut.is_open()) {
                    cOut.write(reinterpret_cast<const char*>(rawChunk.data()), rawChunk.size());
                }
            }

            outFile.write(reinterpret_cast<const char*>(rawChunk.data()), rawChunk.size());
        }

        outFile.close();
        metrics.filesRestored++;

        if (progress) {
            progress(fIdx + 1, manifest.files.size());
        }
    }

    if (isWorkingDir) {
        auto currentFiles = scanWorkingDir(normPath);
        for (const auto& cur : currentFiles) {
            if (manifestPaths.find(cur) == manifestPaths.end()) {
                std::string delPath = normPath + "\\" + cur;
                try {
                    fs::remove(fs::u8path(delPath));
                    metrics.filesDeleted++;
                } catch (...) {}
            }
        }

        try {
            std::ifstream cf(getRepoConfigPath(normPath), std::ios::binary);
            if (cf.is_open()) {
                std::string cStr((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
                auto cRoot = json::Value::parse(cStr);
                cRoot["headVersion"] = metrics.targetVersion;
                cf.close();

                std::ofstream cOut(getRepoConfigPath(normPath), std::ios::binary | std::ios::trunc);
                std::string updated = cRoot.stringify(true);
                cOut.write(updated.data(), updated.size());
            }
        } catch (...) {}

        auto repos = listRepos();
        for (auto& r : repos) {
            if (normalizePath(r.path) == normPath) {
                r.headVersion = metrics.targetVersion;
                break;
            }
        }
        saveReposRegistry(repos);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    metrics.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    Logger::success("Rollback to [" + metrics.targetVersion + "] complete in " +
                   std::to_string(metrics.elapsedSeconds).substr(0, 4) + "s. " +
                   std::to_string(metrics.identicalFilesSkipped) + " skipped (identical), " +
                   std::to_string(metrics.filesRestored) + " restored, " +
                   std::to_string(metrics.filesDeleted) + " removed. Downloaded: " +
                   CryptoUtil::formatBytes(metrics.bytesDownloaded));

    return metrics;
}

DiffResult RepoManager::diff(const std::string& repoPath, const std::string& v1, const std::string& v2) {
    DiffResult result;
    std::string normPath = normalizePath(repoPath);

    VersionManifest man1;
    if (!getManifest(normPath, v1, man1)) {
        throw std::runtime_error("Version manifest not found for " + v1);
    }

    std::map<std::string, ManifestFile> files1;
    for (const auto& f : man1.files) files1[f.path] = f;

    result.fromVersion = "v" + std::to_string(man1.versionNumber);

    if (v2.empty() || v2 == "WORKING" || v2 == "current") {
        result.toVersion = "WORKING_DIR";
        auto currentFiles = scanWorkingDir(normPath);
        std::set<std::string> curSet(currentFiles.begin(), currentFiles.end());

        for (const auto& cur : currentFiles) {
            auto it = files1.find(cur);
            if (it == files1.end()) {
                result.addedFiles.push_back(cur);
            } else {
                std::string fullPath = normPath + "\\" + cur;
                std::string h = CryptoUtil::sha256File(fullPath);
                if (h == it->second.fileSha256) {
                    result.identicalFiles.push_back(cur);
                } else {
                    result.modifiedFiles.push_back(cur);
                }
            }
        }

        for (const auto& pair : files1) {
            if (curSet.find(pair.first) == curSet.end()) {
                result.deletedFiles.push_back(pair.first);
            }
        }
    } else {
        VersionManifest man2;
        if (!getManifest(normPath, v2, man2)) {
            throw std::runtime_error("Version manifest not found for " + v2);
        }

        result.toVersion = "v" + std::to_string(man2.versionNumber);

        std::map<std::string, ManifestFile> files2;
        for (const auto& f : man2.files) files2[f.path] = f;

        for (const auto& pair : files2) {
            auto it = files1.find(pair.first);
            if (it == files1.end()) {
                result.addedFiles.push_back(pair.first);
                result.transferBytesNeeded += pair.second.size;
            } else if (it->second.fileSha256 != pair.second.fileSha256) {
                result.modifiedFiles.push_back(pair.first);
                result.transferBytesNeeded += pair.second.size;
            } else {
                result.identicalFiles.push_back(pair.first);
            }
        }

        for (const auto& pair : files1) {
            if (files2.find(pair.first) == files2.end()) {
                result.deletedFiles.push_back(pair.first);
            }
        }
    }

    return result;
}

StatusResult RepoManager::status(const std::string& repoPath) {
    StatusResult res;
    std::string normPath = normalizePath(repoPath);

    auto history = getHistory(normPath);
    if (history.empty()) {
        res.headVersion = "none (uninitialized)";
        res.untrackedFiles = scanWorkingDir(normPath);
        return res;
    }

    res.headVersion = history.back().versionTag;
    VersionManifest headMan;
    getManifest(normPath, history.back().versionId, headMan);

    std::map<std::string, ManifestFile> headFiles;
    for (const auto& f : headMan.files) headFiles[f.path] = f;

    auto curFiles = scanWorkingDir(normPath);
    std::set<std::string> curSet(curFiles.begin(), curFiles.end());

    for (const auto& cf : curFiles) {
        auto it = headFiles.find(cf);
        if (it == headFiles.end()) {
            res.untrackedFiles.push_back(cf);
        } else {
            std::string fullPath = normPath + "\\" + cf;
            if (fs::file_size(fs::u8path(fullPath)) != it->second.size ||
                CryptoUtil::sha256File(fullPath) != it->second.fileSha256) {
                res.modifiedFiles.push_back(cf);
            }
        }
    }

    for (const auto& pair : headFiles) {
        if (curSet.find(pair.first) == curSet.end()) {
            res.deletedFiles.push_back(pair.first);
        }
    }

    return res;
}

} // namespace cas
