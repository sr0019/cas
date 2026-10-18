#pragma once

#include "fastcdc.hpp"
#include "pack_format.hpp"
#include "s3_client.hpp"

#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <functional>

namespace cas {

struct RepoInfo {
    std::string id;
    std::string name;
    std::string path;
    std::string headVersion;
    std::string lastCommitTime;
    size_t totalVersions{0};
};

struct CommitRecord {
    std::string versionId;       // Short unique hex id
    uint32_t versionNumber{0};   // 1, 2, 3...
    std::string versionTag;      // "v1", "v2"...
    std::string message;
    std::string timestamp;
    std::string author;
    std::string manifestHash;
    size_t fileCount{0};
    uint64_t totalRawBytes{0};
    uint64_t totalStoredBytes{0};
    size_t totalChunks{0};
    size_t reusedChunks{0};
    size_t newChunks{0};
    uint64_t dedupSavingsBytes{0};
    std::string dedupRatio;
};

struct ManifestFileChunk {
    std::string hash;
    uint32_t rawLength{0};
    uint32_t storedLength{0};
    std::string packId;
    uint64_t payloadOffset{0};
    std::string codec{"none"};
};

struct ManifestFile {
    std::string path;
    uint64_t size{0};
    std::string fileSha256;
    uint64_t mtime{0};
    std::vector<ManifestFileChunk> chunks;
};

struct VersionManifest {
    std::string versionId;
    uint32_t versionNumber{0};
    std::string repoName;
    std::string timestamp;
    std::string message;
    size_t totalFiles{0};
    uint64_t totalRawBytes{0};
    std::vector<ManifestFile> files;
};

struct CommitMetrics {
    std::string versionId;
    std::string versionTag;
    size_t fileCount{0};
    uint64_t totalRawBytes{0};
    uint64_t totalStoredBytes{0};
    size_t totalChunks{0};
    size_t reusedChunks{0};
    size_t newChunks{0};
    uint64_t bytesReused{0};
    uint64_t bytesUploaded{0};
    size_t packsUploaded{0};
    double elapsedSeconds{0.0};
    std::string dedupRatio;
};

struct RollbackMetrics {
    std::string targetVersion;
    size_t totalFiles{0};
    size_t identicalFilesSkipped{0};
    size_t filesRestored{0};
    size_t filesDeleted{0};
    uint64_t totalBytes{0};
    uint64_t bytesDownloaded{0};
    size_t chunksFromCache{0};
    size_t chunksFromS3{0};
    double elapsedSeconds{0.0};
};

struct DiffResult {
    std::string fromVersion;
    std::string toVersion;
    std::vector<std::string> addedFiles;
    std::vector<std::string> modifiedFiles;
    std::vector<std::string> deletedFiles;
    std::vector<std::string> identicalFiles;
    uint64_t transferBytesNeeded{0};
};

struct StatusResult {
    std::string headVersion;
    std::vector<std::string> untrackedFiles;
    std::vector<std::string> modifiedFiles;
    std::vector<std::string> deletedFiles;
    bool isClean() const {
        return untrackedFiles.empty() && modifiedFiles.empty() && deletedFiles.empty();
    }
};

class RepoManager {
public:
    static RepoManager& instance() {
        static RepoManager s_mgr;
        return s_mgr;
    }

    // Repositories registry management
    std::vector<RepoInfo> listRepos();
    bool addRepo(const std::string& path, const std::string& customName = "");
    bool removeRepo(const std::string& nameOrPath);
    bool getRepo(const std::string& nameOrPath, RepoInfo& outRepo);

    // Initialize .cas in directory if not present
    bool initRepo(const std::string& repoPath, const std::string& name = "");

    // Commit snapshot to local history and upload new packs directly to S3
    CommitMetrics commit(const std::string& repoPath,
                         const std::string& message,
                         ProgressCallback progress = nullptr,
                         std::atomic<bool>* cancelToken = nullptr);

    // Rollback / checkout working directory or target directory to any version
    RollbackMetrics rollback(const std::string& repoPath,
                             const std::string& targetVersionOrTag,
                             const std::string& customTargetPath = "",
                             ProgressCallback progress = nullptr,
                             std::atomic<bool>* cancelToken = nullptr);

    // Commit history
    std::vector<CommitRecord> getHistory(const std::string& repoPath);

    // Inspect files of a specific version
    bool getManifest(const std::string& repoPath, const std::string& versionIdOrTag, VersionManifest& outManifest);

    // Diff between two versions (or between version and current working directory)
    DiffResult diff(const std::string& repoPath, const std::string& v1, const std::string& v2 = "");

    // Status: compare current working directory to HEAD
    StatusResult status(const std::string& repoPath);

    // S3 client access
    S3Client& getS3Client() { return m_s3; }

private:
    RepoManager();

    S3Client m_s3;

    // Helper functions
    std::string getCasDir(const std::string& repoPath);
    std::string getManifestsDir(const std::string& repoPath);
    std::string getCacheDir(const std::string& repoPath);
    std::string getCatalogPath(const std::string& repoPath);
    std::string getHistoryPath(const std::string& repoPath);
    std::string getRepoConfigPath(const std::string& repoPath);

    std::map<std::string, ChunkLocation> loadCatalog(const std::string& repoPath);
    void saveCatalog(const std::string& repoPath, const std::map<std::string, ChunkLocation>& catalog);

    std::vector<std::string> scanWorkingDir(const std::string& rootDir);
    std::string resolveVersionId(const std::string& repoPath, const std::string& versionIdOrTag);

    void saveReposRegistry(const std::vector<RepoInfo>& repos);
};

} // namespace cas
