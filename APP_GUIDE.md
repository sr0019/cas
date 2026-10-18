# CAS: Complete Technical Manual & Guide

This file contains the complete technical reference for the CAS native C++ engine, CLI, and Windows desktop GUI application. For the project overview and contribution guide, see [README.md](README.md) on [GitHub (sr0019/cas)](https://github.com/sr0019/cas).

---

# CAS: Content Addressable Storage & Direct-to-S3 Versioning Client

A high-performance packed file storage and version control application built with **native C++17**.

CAS brings a **Git-like repository and commit workflow** to binary and packed cloud storage with **client-side Content-Defined Chunking (FastCDC)**, **direct-to-S3 uploads and byte-range downloads (AWS SigV4)**, and **local immutable manifests**.

**Zero server required**: No backend server or presigned URL generator is needed. The application signs all requests directly on your machine and communicates straight with any S3-compatible cloud or local storage (Cloudflare Workers S3, AWS S3, MinIO, Wasabi, Ceph).

---

## Key Features

1. **Fully Decoupled Architecture**:
   - **`src/core/`**: Headless, UI-free engine containing FastCDC, binary CAS1/VPK1 packing, AWS SigV4 direct S3 client, and local repository version manager.
   - **`src/cli/`**: Headless Command Line Interface (`cas_cli.exe`) for automation, terminal workflows, and scripting.
   - **`src/ui/`**: Windows native desktop GUI (`cas_app.exe`) with DPI-aware Segoe UI styling, background worker threads, and live logs.

2. **Direct-to-S3 Communication (No Server)**:
   - Implements native **AWS Signature Version 4 (SigV4)** with hardware-accelerated Windows BCrypt HMAC-SHA256.
   - Direct `PUT` object uploads for immutable packs (`packs/<packId>.vpk`).
   - Direct byte-range `GET` requests (`Range: bytes=X-Y`) to download only missing chunks during checkouts.
   - Default configuration configured for `http://w3-us.1llx.workers.dev` and bucket `testers`.
   - Automatic bucket creation if missing during connection test or upload.

3. **Content-Defined Chunking (FastCDC)**:
   - Gear rolling hash with normalized chunk boundaries (64 KB min, 256 KB avg, 1 MB max).
   - High-granularity profile (16 KB min, 64 KB avg, 256 KB max) automatically applied for code, configs, and text files.
   - Shift-resistant chunk boundaries guarantee deduplication even when bytes are prepended or shifted.

4. **Binary Packed Format (`CAS1`)**:
   - 64-byte file header with CRC32 verification.
   - 48-byte record headers with 32-byte binary SHA-256 and CRC32 payload checksums.
   - Absolute byte offsets for zero-overhead HTTP byte-range retrieval.
   - Optional zlib deflate compression applied per-chunk if beneficial.

5. **Git-Like Repository & Versioning System**:
   - Register any directory as a CAS repository (`.cas/`).
   - **Local Manifests**: Deterministic snapshot metadata stays on your local machine (`.cas/manifests/<versionId>.json`).
   - **Full History**: Inspect commits, timestamps, authors, deduplication ratios, and file trees.
   - **Instant Rollback**: Reconstruct the working directory or an external target folder to **any historical version**.
   - **Smart Incremental Checkout**: Files with matching SHA-256 hashes are skipped (0 network transfer). Only delta chunks are fetched from S3.
   - **Version Diff**: Compare any two snapshots (or a snapshot vs working directory) to view added, modified, and deleted files.

6. **Dedicated Settings Panel**:
   - Visual GUI & CLI settings manager.
   - Configure Endpoint, Region, Bucket Name, Access Key, Secret Key, Path-Style Addressing, and SSL.
   - Live **Test S3 Connection** button to verify bucket accessibility and credentials instantly.

---

## Directory Structure

```
app/
├── build.bat                   # 1-Click native compiler script (MinGW/GCC)
├── CMakeLists.txt              # Standard CMake build configuration
├── README.md                   # GitHub project overview & contribution guide
├── APP_GUIDE.md                # This comprehensive technical manual
├── bin/
│   ├── cas_app.exe             # Native Windows GUI Desktop Application
│   └── cas_cli.exe             # Headless Command Line Interface
└── src/
    ├── core/                   # 100% Pure Core Engine (Zero UI dependencies)
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

## Building from Source

From the `app/` folder, run:
```cmd
build.bat
```
This compiles both executables into `bin/`:
- `bin\cas_cli.exe`
- `bin\cas_app.exe`

---

## Using the CLI (`cas_cli.exe`)

### 1. View & Configure Direct S3 Settings
```cmd
# Display current settings
bin\cas_cli.exe config

# Configure S3 credentials
bin\cas_cli.exe config --endpoint http://w3-us.1llx.workers.dev --bucket testers --key public --secret public --path-style --no-ssl
```

### 2. Test S3 Connectivity & Create Bucket
```cmd
# Test connection (auto-creates bucket if missing)
bin\cas_cli.exe test-s3

# Or explicitly create a bucket
bin\cas_cli.exe bucket create testers
```

### 3. Repository Management
```cmd
# List registered repositories
bin\cas_cli.exe repo list

# Register a folder as a repository
bin\cas_cli.exe repo add C:\Projects\MyGame --name "my-game"

# Unregister a repository
bin\cas_cli.exe repo remove "my-game"
```

### 4. Create a Snapshot (Commit & Upload to S3)
```cmd
bin\cas_cli.exe commit "my-game" -m "Initial commit with core assets and scripts"
```

### 5. Check Working Directory Status
```cmd
bin\cas_cli.exe status "my-game"
```

### 6. View Commit History (Log)
```cmd
bin\cas_cli.exe log "my-game"
```

### 7. Inspect Files in a Version
```cmd
bin\cas_cli.exe show "my-game" v2
```

### 8. Compare Versions (Diff)
```cmd
# Compare two snapshot versions
bin\cas_cli.exe diff "my-game" v1 v2

# Compare snapshot against current working directory
bin\cas_cli.exe diff "my-game" v2
```

### 9. Rollback Working Directory to Any Version
```cmd
bin\cas_cli.exe rollback "my-game" v1
```

### 10. Restore / Reconstruct Version to an External Folder
```cmd
bin\cas_cli.exe restore "my-game" v1 --target C:\Exports\GameRelease_v1
```

---

## Using the Windows GUI (`cas_app.exe`)

Launch `bin\cas_app.exe` to open the native desktop client:

1. **Top Navigation Bar**:
   - `[ 📁 Repositories ]`: Main workspace for repository tracking, commit history, and rollbacks.
   - `[ ⚙ S3 Settings ]`: Configure S3 credentials and test connectivity.
   - `[ 📜 Activity Log ]`: Real-time streaming log of FastCDC chunking and S3 transfers.

2. **Repositories Tab**:
   - **Left Sidebar**: Browse registered repositories, add folders (`+ Add / Init Repo...`), or open folders in Windows Explorer.
   - **Commit Box**: Type a commit message and click `[ 🚀 Snapshot & Upload to S3 ]`.
   - **History Table**: View all historical snapshots with Version tags (`v1`, `v2`), Commit IDs, Messages, Timestamps, File Counts, Raw Sizes, Uploaded Bytes, and Deduplication %.
   - **Version Actions**:
     - `[ ⏪ Rollback Working Dir ]`: Safely rolls back your local repository to the selected snapshot.
     - `[ 📂 Export to Folder... ]`: Reconstructs the exact snapshot into any folder.
     - `[ 🔍 Inspect Files ]`: View file breakdown with chunk counts and sizes.
     - `[ ⚖ Diff vs Working Dir ]`: Inspect added, modified, and deleted files.

3. **S3 Settings Tab**:
   - Pre-configured for `http://w3-us.1llx.workers.dev` and bucket `testers`.
   - Click `[ 🔌 Test S3 Connection ]` for instant validation via AWS SigV4.
   - Click `[ 💾 Save Settings ]` to store settings locally in `%APPDATA%\cas\config.json`.
