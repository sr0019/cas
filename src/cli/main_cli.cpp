#include "../core/repo_manager.hpp"
#include "../core/config.hpp"
#include "../core/logger.hpp"
#include "../core/crypto_util.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

void printBanner() {
    std::cout << "====================================================================\n"
              << " CAS: Content Addressable Storage & Versioning System\n"
              << " Direct S3 Upload | FastCDC Chunker | Local Manifests | Zero Server\n"
              << "====================================================================\n";
}

void printHelp() {
    printBanner();
    std::cout << "Usage:\n"
              << "  cas_cli config                            Show current S3 configuration\n"
              << "  cas_cli config [options]                  Update S3 configuration\n"
              << "      --endpoint <url>                      S3 Endpoint (e.g. http://w3-us.1llx.workers.dev)\n"
              << "      --region <region>                     S3 Region (e.g. us-east-1)\n"
              << "      --bucket <name>                       S3 Bucket Name (e.g. testers)\n"
              << "      --key <access_key>                    AWS Access Key ID (e.g. public)\n"
              << "      --secret <secret_key>                 AWS Secret Access Key (e.g. public)\n"
              << "      --path-style                          Use path-style URLs\n"
              << "      --ssl                                 Use HTTPS (default: http for custom workers)\n"
              << "      --no-ssl                              Disable SSL (use plain HTTP)\n\n"
              << "  cas_cli test-s3                           Test direct S3 connection and bucket access\n"
              << "  cas_cli bucket create [name]              Create a bucket on S3 directly\n\n"
              << "  cas_cli repo list                         List all registered repositories\n"
              << "  cas_cli repo add <path> [--name <name>]   Register a folder as a CAS repository\n"
              << "  cas_cli repo remove <name_or_path>        Unregister a repository\n\n"
              << "  cas_cli commit <repo> -m \"<message>\"      Create snapshot & upload new packs directly to S3\n"
              << "  cas_cli log <repo>                        View full commit / version history\n"
              << "  cas_cli status <repo>                     Show modified / untracked files vs HEAD\n"
              << "  cas_cli show <repo> <version>             Inspect files in a specific version\n"
              << "  cas_cli diff <repo> <v1> [v2]             Compare two versions (or version vs working dir)\n"
              << "  cas_cli rollback <repo> <version>         Rollback working directory to any version\n"
              << "  cas_cli restore <repo> <version> --target <folder>  Reconstruct version into target folder\n"
              << "====================================================================\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printHelp();
        return 1;
    }

    std::string command = argv[1];

    try {
        auto& mgr = cas::RepoManager::instance();
        auto& cfgMgr = cas::ConfigManager::instance();

        // 1. CONFIG
        if (command == "config") {
            if (argc == 2) {
                const auto& s3 = cfgMgr.getS3();
                std::cout << "\nCurrent S3 Configuration:\n"
                          << "----------------------------------------------------\n"
                          << "Endpoint:        " << s3.endpoint << "\n"
                          << "Region:          " << s3.region << "\n"
                          << "Bucket:          " << s3.bucket << "\n"
                          << "Access Key:      " << (s3.accessKey.empty() ? "(not set)" : s3.accessKey) << "\n"
                          << "Secret Key:      " << (s3.secretKey.empty() ? "(not set)" : "********" + (s3.secretKey.size() > 4 ? s3.secretKey.substr(s3.secretKey.size() - 4) : "")) << "\n"
                          << "Path-Style URLs: " << (s3.pathStyle ? "true (Path Style)" : "false (Virtual Host)") << "\n"
                          << "Use SSL:         " << (s3.useSsl ? "true (HTTPS)" : "false (HTTP)") << "\n"
                          << "Valid:           " << (s3.isValid() ? "YES" : "NO (missing required credentials)") << "\n"
                          << "Config Path:     " << cfgMgr.getDefaultConfigPath() << "\n"
                          << "----------------------------------------------------\n";
                return 0;
            }

            cas::S3Config newS3 = cfgMgr.getS3();
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--endpoint" && i + 1 < argc) newS3.endpoint = argv[++i];
                else if (arg == "--region" && i + 1 < argc) newS3.region = argv[++i];
                else if (arg == "--bucket" && i + 1 < argc) newS3.bucket = argv[++i];
                else if (arg == "--key" && i + 1 < argc) newS3.accessKey = argv[++i];
                else if (arg == "--secret" && i + 1 < argc) newS3.secretKey = argv[++i];
                else if (arg == "--path-style") newS3.pathStyle = true;
                else if (arg == "--ssl") newS3.useSsl = true;
                else if (arg == "--no-ssl") newS3.useSsl = false;
            }

            cfgMgr.setS3(newS3);
            if (cfgMgr.save()) {
                std::cout << "[OK] S3 configuration saved successfully to " << cfgMgr.getDefaultConfigPath() << "\n";
                return 0;
            } else {
                std::cerr << "[ERROR] Failed to save configuration to disk.\n";
                return 1;
            }
        }

        // 2. TEST-S3
        if (command == "test-s3") {
            std::string details;
            bool ok = mgr.getS3Client().testConnection(details);
            std::cout << (ok ? "[SUCCESS] " : "[FAILURE] ") << details << "\n";
            return ok ? 0 : 1;
        }

        // 3. BUCKET CREATE
        if (command == "bucket") {
            if (argc >= 3 && std::string(argv[2]) == "create") {
                std::string bkt = (argc >= 4) ? argv[3] : cfgMgr.getS3().bucket;
                std::string details;
                bool ok = mgr.getS3Client().createBucket(bkt, details);
                std::cout << (ok ? "[SUCCESS] " : "[FAILURE] ") << details << "\n";
                return ok ? 0 : 1;
            }
        }

        // 4. REPO LIST / ADD / REMOVE
        if (command == "repo") {
            if (argc < 3) {
                printHelp();
                return 1;
            }
            std::string sub = argv[2];

            if (sub == "list") {
                auto list = mgr.listRepos();
                std::cout << "\nRegistered CAS Repositories (" << list.size() << "):\n";
                std::cout << "========================================================================================\n";
                std::cout << std::left << std::setw(20) << "NAME"
                          << std::setw(35) << "WORKING PATH"
                          << std::setw(12) << "HEAD"
                          << std::setw(10) << "VERSIONS"
                          << "LAST COMMIT\n";
                std::cout << "----------------------------------------------------------------------------------------\n";
                for (const auto& r : list) {
                    std::cout << std::left << std::setw(20) << r.name
                              << std::setw(35) << (r.path.size() > 33 ? "..." + r.path.substr(r.path.size() - 30) : r.path)
                              << std::setw(12) << (r.headVersion.empty() ? "(none)" : r.headVersion)
                              << std::setw(10) << r.totalVersions
                              << r.lastCommitTime << "\n";
                }
                std::cout << "========================================================================================\n\n";
                return 0;
            }

            if (sub == "add") {
                if (argc < 4) {
                    std::cerr << "Usage: cas_cli repo add <path> [--name <custom_name>]\n";
                    return 1;
                }
                std::string path = argv[3];
                std::string customName = "";
                if (argc >= 6 && std::string(argv[4]) == "--name") {
                    customName = argv[5];
                }
                bool ok = mgr.addRepo(path, customName);
                return ok ? 0 : 1;
            }

            if (sub == "remove") {
                if (argc < 4) {
                    std::cerr << "Usage: cas_cli repo remove <name_or_path>\n";
                    return 1;
                }
                bool ok = mgr.removeRepo(argv[3]);
                return ok ? 0 : 1;
            }
        }

        // 5. COMMIT
        if (command == "commit") {
            if (argc < 3) {
                std::cerr << "Usage: cas_cli commit <repo_name_or_path> -m \"<commit_message>\"\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            std::string message = "Snapshot commit";

            for (int i = 3; i < argc; ++i) {
                if (std::string(argv[i]) == "-m" && i + 1 < argc) {
                    message = argv[++i];
                }
            }

            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto metrics = mgr.commit(path, message);
            std::cout << "\n====================================================\n"
                      << " Snapshot Created Successfully!\n"
                      << "====================================================\n"
                      << "Version:              " << metrics.versionTag << " (" << metrics.versionId << ")\n"
                      << "Files Indexed:        " << metrics.fileCount << "\n"
                      << "Total Original Size:  " << cas::CryptoUtil::formatBytes(metrics.totalRawBytes) << "\n"
                      << "Total Chunks:         " << metrics.totalChunks << "\n"
                      << "Reused (Dedup) Chunks:" << metrics.reusedChunks << "\n"
                      << "Deduplication Savings:" << cas::CryptoUtil::formatBytes(metrics.bytesReused) << " (" << metrics.dedupRatio << ")\n"
                      << "New Chunks Uploaded:  " << metrics.newChunks << "\n"
                      << "Packs Uploaded to S3: " << metrics.packsUploaded << " (" << cas::CryptoUtil::formatBytes(metrics.bytesUploaded) << ")\n"
                      << "Time Elapsed:         " << std::fixed << std::setprecision(2) << metrics.elapsedSeconds << " seconds\n"
                      << "====================================================\n\n";
            return 0;
        }

        // 6. LOG / HISTORY
        if (command == "log") {
            if (argc < 3) {
                std::cerr << "Usage: cas_cli log <repo_name_or_path>\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto history = mgr.getHistory(path);
            if (history.empty()) {
                std::cout << "No commit history found for " << path << "\n";
                return 0;
            }

            std::cout << "\nCommit History for " << path << " (" << history.size() << " versions):\n";
            std::cout << "========================================================================================\n";
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                std::cout << "commit " << it->versionId << " (tag: " << it->versionTag << ")\n"
                          << "Author:    " << it->author << "\n"
                          << "Date:      " << it->timestamp << "\n"
                          << "Summary:   " << it->fileCount << " files | "
                          << cas::CryptoUtil::formatBytes(it->totalRawBytes) << " raw | "
                          << "Dedup: " << it->dedupRatio << " (" << cas::CryptoUtil::formatBytes(it->dedupSavingsBytes) << " saved)\n"
                          << "\n    " << it->message << "\n\n";
            }
            std::cout << "========================================================================================\n";
            return 0;
        }

        // 7. STATUS
        if (command == "status") {
            if (argc < 3) {
                std::cerr << "Usage: cas_cli status <repo_name_or_path>\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto st = mgr.status(path);
            std::cout << "\nOn version: " << st.headVersion << "\n";
            if (st.isClean()) {
                std::cout << "Working directory clean! No uncommitted changes.\n\n";
                return 0;
            }

            if (!st.modifiedFiles.empty()) {
                std::cout << "\nModified files (" << st.modifiedFiles.size() << "):\n";
                for (const auto& f : st.modifiedFiles) std::cout << "  modified:  " << f << "\n";
            }
            if (!st.untrackedFiles.empty()) {
                std::cout << "\nUntracked new files (" << st.untrackedFiles.size() << "):\n";
                for (const auto& f : st.untrackedFiles) std::cout << "  untracked: " << f << "\n";
            }
            if (!st.deletedFiles.empty()) {
                std::cout << "\nDeleted files (" << st.deletedFiles.size() << "):\n";
                for (const auto& f : st.deletedFiles) std::cout << "  deleted:   " << f << "\n";
            }
            std::cout << "\n";
            return 0;
        }

        // 8. SHOW / INSPECT
        if (command == "show") {
            if (argc < 4) {
                std::cerr << "Usage: cas_cli show <repo_name_or_path> <version>\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            std::string version = argv[3];

            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            cas::VersionManifest man;
            if (!mgr.getManifest(path, version, man)) {
                std::cerr << "Version not found: " << version << "\n";
                return 1;
            }

            std::cout << "\nSnapshot " << man.versionId << " (v" << man.versionNumber << "):\n"
                      << "Date:    " << man.timestamp << "\n"
                      << "Message: " << man.message << "\n"
                      << "Files (" << man.files.size() << ", Total: " << cas::CryptoUtil::formatBytes(man.totalRawBytes) << "):\n";
            std::cout << "----------------------------------------------------------------------------------------\n";
            for (const auto& f : man.files) {
                std::cout << "• " << f.path << " (" << cas::CryptoUtil::formatBytes(f.size)
                          << ", " << f.chunks.size() << " chunks)\n";
            }
            std::cout << "----------------------------------------------------------------------------------------\n\n";
            return 0;
        }

        // 9. DIFF
        if (command == "diff") {
            if (argc < 4) {
                std::cerr << "Usage: cas_cli diff <repo_name_or_path> <v1> [v2]\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            std::string v1 = argv[3];
            std::string v2 = (argc >= 5) ? argv[4] : "WORKING";

            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto res = mgr.diff(path, v1, v2);
            std::cout << "\nDiff: " << res.fromVersion << " -> " << res.toVersion << "\n";
            std::cout << "====================================================================\n";
            std::cout << "Identical: " << res.identicalFiles.size() << " files\n"
                      << "Added:     " << res.addedFiles.size() << " files\n"
                      << "Modified:  " << res.modifiedFiles.size() << " files\n"
                      << "Deleted:   " << res.deletedFiles.size() << " files\n"
                      << "Transfer:  " << cas::CryptoUtil::formatBytes(res.transferBytesNeeded) << "\n";
            std::cout << "--------------------------------------------------------------------\n";

            for (const auto& f : res.addedFiles)    std::cout << "+ Added:    " << f << "\n";
            for (const auto& f : res.modifiedFiles) std::cout << "~ Modified: " << f << "\n";
            for (const auto& f : res.deletedFiles)  std::cout << "- Deleted:  " << f << "\n";
            std::cout << "====================================================================\n\n";
            return 0;
        }

        // 10. ROLLBACK
        if (command == "rollback") {
            if (argc < 4) {
                std::cerr << "Usage: cas_cli rollback <repo_name_or_path> <version>\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            std::string version = argv[3];

            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto metrics = mgr.rollback(path, version);
            std::cout << "\n====================================================\n"
                      << " Rollback Complete!\n"
                      << "====================================================\n"
                      << "Target Version:        " << metrics.targetVersion << "\n"
                      << "Total Files in Target: " << metrics.totalFiles << "\n"
                      << "Identical (Skipped):   " << metrics.identicalFilesSkipped << " (0 bytes downloaded)\n"
                      << "Files Restored:        " << metrics.filesRestored << "\n"
                      << "Files Cleaned/Removed: " << metrics.filesDeleted << "\n"
                      << "Chunks From Cache:     " << metrics.chunksFromCache << "\n"
                      << "Chunks From S3:        " << metrics.chunksFromS3 << "\n"
                      << "Downloaded From S3:    " << cas::CryptoUtil::formatBytes(metrics.bytesDownloaded) << "\n"
                      << "Time Elapsed:          " << std::fixed << std::setprecision(2) << metrics.elapsedSeconds << " seconds\n"
                      << "====================================================\n\n";
            return 0;
        }

        // 11. RESTORE TO FOLDER
        if (command == "restore") {
            if (argc < 6 || std::string(argv[4]) != "--target") {
                std::cerr << "Usage: cas_cli restore <repo_name_or_path> <version> --target <folder>\n";
                return 1;
            }
            std::string repoTarget = argv[2];
            std::string version = argv[3];
            std::string targetFolder = argv[5];

            cas::RepoInfo info;
            std::string path = repoTarget;
            if (mgr.getRepo(repoTarget, info)) {
                path = info.path;
            }

            auto metrics = mgr.rollback(path, version, targetFolder);
            std::cout << "\n[OK] Restored version " << metrics.targetVersion << " into " << targetFolder << " ("
                      << metrics.filesRestored << " files, " << cas::CryptoUtil::formatBytes(metrics.bytesDownloaded) << " downloaded from S3).\n";
            return 0;
        }

        std::cerr << "Unknown command: " << command << "\n";
        printHelp();
        return 1;

    } catch (const std::exception& ex) {
        std::cerr << "\n[FATAL ERROR] " << ex.what() << "\n";
        return 1;
    }
}
