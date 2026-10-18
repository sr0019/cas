# ⚡ CAS: Content Addressable Storage

> **The Open-Source, Git-Like Packed Storage & Binary Versioning Engine**  
> *Built with Native C++17 • Client-Side Content-Defined Chunking • Direct-to-S3 AWS SigV4 • Zero Server Required*

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=flat-square&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Windows%20(Cross--Platform%20Ready)-lightgrey.svg?style=flat-square&logo=windows)](https://github.com/sr0019/cas)
[![Direct-to-S3](https://img.shields.io/badge/Cloud-Direct%20S3%20(SigV4)-orange.svg?style=flat-square&logo=amazons3)](https://github.com/sr0019/cas)
[![License](https://img.shields.io/badge/License-MIT%2FApache--2.0-green.svg?style=flat-square)](https://github.com/sr0019/cas/blob/main/LICENSE)
[![Free Dev S3](https://img.shields.io/badge/Free%20Dev%20S3-Live-brightgreen.svg?style=flat-square)](#-free-cloud-s3-for-development)
[![Contributions Welcome](https://img.shields.io/badge/Contributions-Welcome-ff69b4.svg?style=flat-square)](#-calling-all-developers--make-this-good)

---

## 🌟 The Vision: A Solid Foundation Ready for Community Power

**CAS** is a high-performance content-addressable storage and version control engine designed to solve the massive binary file and dataset problem that traditional tools (like Git and Git-LFS) struggle with.

### 🧱 This is the Base — Join Us to Make It Legendary!
What you see here is the **solid, high-speed foundation**:
- ✅ **FastCDC (Content-Defined Chunking)** with rolling Gear hashes for shift-resistant deduplication.
- ✅ **Custom `CAS1` Packed Binary Format** with CRC32 integrity checks, SHA-256 content addressing, and instant HTTP byte-range extraction.
- ✅ **Direct-to-S3 AWS Signature V4 Engine**: Zero intermediary servers, zero presigned URL token APIs. All signing is executed directly on the client machine via hardware-accelerated crypto.
- ✅ **Git-Like Local Repositories**: Immutable snapshot manifests, full version history logs, instant rollback, diff engine, and incremental delta checkouts.
- ✅ **Dual Interfaces**: Native headless CLI (`cas_cli.exe`) and lightweight, DPI-aware Windows Desktop GUI (`cas_app.exe`).

**We are opening this base up to developers worldwide. Anyone who wants to work on this project is invited to contribute, build features, optimize the engine, and make CAS the definitive open-source binary storage and versioning ecosystem!**

---

## ☁️ Free Cloud S3 for Development

To make it as easy as possible to jump in, hack, test, and contribute immediately without needing your own cloud infrastructure, **a free S3-compatible cloud storage backend is provided out-of-the-box for development**:

| Setting | Pre-Configured Value |
| :--- | :--- |
| **S3 Endpoint** | `http://w3-us.1llx.workers.dev` |
| **Default Bucket** | `testers` |
| **Region** | `us-east-1` |
| **Access Key** | `public` |
| **Secret Key** | `public` |
| **Addressing Style** | Path-Style (`true`) |
| **SSL** | Plain HTTP (`false`) |

> [!IMPORTANT]
> ### ⚠️ 90 MB Payload Limit
> The development S3 endpoint enforces a **maximum payload limit of 90 MB per single request**.  
> Whether uploading directly via `PUT` or sending individual parts in a multipart upload, **each single part / payload must not exceed 90 MB**.  
> CAS's packed chunk architecture is designed to pack data into modular containers well within this threshold!

---

## 🎁 Free Private S3 Keys for Contributors!

Are you building features, testing high-throughput pipelines, or contributing to CAS?

> 🚀 **Any developer who actively works on or contributes to this project will receive dedicated, private S3 credentials and their own private bucket for free!**

To claim your private credentials:
1. [Open an Issue](https://github.com/sr0019/cas/issues) or [Pull Request](https://github.com/sr0019/cas/pulls) on GitHub describing what you're working on.
2. Reach out to the maintainer or comment with your request.
3. Your dedicated private Access Key and Secret Key will be issued to you at zero cost for your development work.

---

## 🚀 Calling All Developers — Let's Make This Good!

We want to build something that fundamentally shifts how developers, 3D studios, game engineers, and data scientists store, version, and collaborate on massive assets.

Here are some high-impact areas where you can make a huge difference:

- [ ] **Cross-Platform Portability**: Port HTTP and crypto primitives to POSIX / Linux / macOS using standard OpenSSL / libcurl.
- [ ] **Multipart S3 Uploads**: Implement S3 multipart upload protocol for large packs, keeping every individual chunk part <= **90 MB**.
- [ ] **Modern Compression Engines**: Integrate **Zstandard (zstd)** and **LZ4** alongside existing Zlib deflate for blazingly fast compression.
- [ ] **Virtual File System / FUSE**: Mount CAS repositories as virtual drives (Dokan / WinFsp / FUSE) so files are streamed on-demand without full local checkouts.
- [ ] **Peer-to-Peer & LAN Chunk Sync**: Enable nodes on the same local network to exchange deduplicated chunk packs directly before hitting S3.
- [ ] **Client-Side Encryption**: Implement optional zero-knowledge client-side encryption (AES-256-GCM / ChaCha20-Poly1305) before packs leave the machine.
- [ ] **Rich GUI Visualizations**: Interactive commit graph, branch management, visual file diff inspector, and deduplication analytics charts.
- [ ] **Remote Manifest Sync**: Add remote repository index syncing to allow multi-user collaboration and distributed team branching.

If you have ideas, open an [issue](https://github.com/sr0019/cas/issues) or [discussion](https://github.com/sr0019/cas/discussions) — all contributions, from micro-optimizations to major architectural features, are welcome!

---

## 🏗️ Architecture Overview

```
                      +-----------------------------+
                      |     Your Working Files      |
                      +-----------------------------+
                                     |
                                     v
                   [ FastCDC Content-Defined Chunking ]
                        (16 KB - 1 MB Gear Hash)
                                     |
                                     v
                       [ CAS1 Binary Pack Engine ]
                  (CRC32 + SHA-256 + Optional Deflate)
                                     |
                     +---------------+---------------+
                     |                               |
                     v                               v
          [ Local Repository ]            [ Direct S3 Client ]
         .cas/manifests/<id>.json           AWS SigV4 Signing
        (Zero-overhead metadata)          (No Intermediary Server)
                                                     |
                                                     v
                                      +------------------------------+
                                      | S3 |
                                      |  w3-us.1llx.workers.dev      |
                                      |   (Free Dev S3 - Max 90MB)   |
                                      +------------------------------+
```

### Directory Structure

```
app/
├── build.bat                   # 1-Click native compiler script (MinGW GCC)
├── CMakeLists.txt              # Standard CMake build definition
├── README.md                   # This open-source guide
├── APP_GUIDE.md                # Comprehensive technical manual & CLI/GUI reference
├── bin/
│   ├── cas_cli.exe             # Headless Command Line Interface
│   └── cas_app.exe             # Windows Native Desktop GUI
└── src/
    ├── core/                   # Pure Engine (Zero UI dependencies)
    │   ├── crypto_util.hpp/.cpp# BCrypt SHA-256, HMAC-SHA256, CRC-32 & formatting
    │   ├── fastcdc.hpp/.cpp    # FastCDC algorithm with 256-entry Gear rolling hash
    │   ├── pack_format.hpp/.cpp# CAS1 binary pack writer, reader, deflate & verification
    │   ├── http_client.hpp/.cpp# Native WinHTTP client (GET, Range GET, PUT, HEAD)
    │   ├── s3_client.hpp/.cpp  # Direct S3 AWS SigV4 client (PUT pack, Range GET, CreateBucket)
    │   ├── config.hpp/.cpp     # Local settings manager (%APPDATA%/cas/config.json)
    │   ├── repo_manager.hpp/.cpp# Repository registry, commit, rollback, manifest & diff
    │   ├── json.hpp/.cpp       # Lightweight, robust JSON parser & serializer
    │   └── logger.hpp          # Decoupled thread-safe logging event bus
    ├── cli/
    │   └── main_cli.cpp        # Console CLI executable entry point
    └── ui/                     # Windows Native GUI Layer
        ├── ui_window.hpp/.cpp  # DPI-aware Win32 window, modern controls & worker threads
        └── main_ui.cpp         # Desktop WinMain entry point
```

---

## ⚡ Quickstart

### 1. Build from Source
On Windows with MinGW-w64 or GCC 11+:
```cmd
cd app
build.bat
```
*(Or use `cmake -B build -G "Ninja" && cmake --build build`)*

Both executables will be output directly to `app/bin/`.

---

### 2. Verify Your Free S3 Connection
```cmd
bin\cas_cli.exe test-s3
```
Output:
```
Testing S3 connection with configured settings:
  Endpoint:   http://w3-us.1llx.workers.dev
  Bucket:     testers
  Access Key: public
  Secret Key: public
  Path Style: true
  Use SSL:    false

[OK] S3 connection successful! Bucket 'testers' is accessible and ready.
```

---

### 3. Track a Directory & Create Your First Snapshot
```cmd
# 1. Register a project folder as a CAS repository
bin\cas_cli.exe repo add C:\Projects\MyGame --name "my-game"

# 2. Commit working directory and upload delta pack directly to S3
bin\cas_cli.exe commit "my-game" -m "Initial release with 3D assets"

# 3. View commit history
bin\cas_cli.exe log "my-game"

# 4. Instant rollback to any previous version
bin\cas_cli.exe rollback "my-game" v1
```

---

### 4. Or Use the Native Windows GUI (`cas_app.exe`)
Double-click `bin\cas_app.exe`:
- **Repositories Tab**: Browse registered repos, add folders, take snapshots with 1-click, view visual commit tables, and execute rollbacks or folder exports.
- **S3 Settings Tab**: Pre-filled with the free dev S3 credentials. Click **Test S3 Connection** to verify in real-time.
- **Activity Log Tab**: Real-time streaming log of FastCDC chunking and S3 transfers.

---

## 📖 In-Depth Technical Manual

For exhaustive CLI flags, binary container specifications (`CAS1`), algorithm details, and API references, check out:
👉 **[APP_GUIDE.md](APP_GUIDE.md)** *(or browse online at [github.com/sr0019/cas/app/APP_GUIDE.md](https://github.com/sr0019/cas/blob/main/app/APP_GUIDE.md))*

---

## 🤝 How to Contribute

1. **Fork** the repository on GitHub: [github.com/sr0019/cas](https://github.com/sr0019/cas)
2. **Clone** your fork locally:
   ```cmd
   git clone https://github.com/sr0019/cas.git
   ```
3. Check the [Open Issues](https://github.com/sr0019/cas/issues) or propose a new feature.
4. Need private credentials? Reach out to receive your **Free Private S3 Keys**.
5. Create a feature branch (`git checkout -b feature/amazing-feature`).
6. Commit your changes and push to your fork.
7. Open a [Pull Request](https://github.com/sr0019/cas/pulls) with a description of your work.

Let's build the fastest, cleanest, and most accessible content-addressable storage tool together!
