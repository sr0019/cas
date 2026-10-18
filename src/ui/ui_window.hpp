#pragma once

#include "../core/repo_manager.hpp"
#include "../core/config.hpp"

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

namespace cas {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool create(HINSTANCE hInstance, int nCmdShow);
    int run();

private:
    static LRESULT CALLBACK s_wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void createControls();
    void layoutControls(int width, int height);
    void setupFonts();
    void switchView(int viewIndex); // 0: Repositories, 1: S3 Settings, 2: Activity Logs

    // Data refresh helpers
    void refreshRepoList();
    void refreshHistoryList(const std::string& repoPath);
    void loadS3SettingsIntoControls();
    void saveS3SettingsFromControls();

    // Async worker triggers
    void startCommit();
    void startRollback(bool toCustomFolder);
    void startDiff();
    void startInspect();
    void startTestS3();

    // Helper functions
    std::string getSelectedRepoPath();
    std::string getSelectedVersionTag();
    void appendLog(const std::string& line);
    void updateProgress(uint64_t current, uint64_t total);

    HINSTANCE m_hInstance{NULL};
    HWND m_hwnd{NULL};

    // Fonts
    HFONT m_fontUi{NULL};
    HFONT m_fontBold{NULL};
    HFONT m_fontHeader{NULL};
    HFONT m_fontMono{NULL};

    // Current active view (0: Repos, 1: Settings, 2: Logs)
    int m_activeView{0};

    // Top navigation buttons
    HWND m_hBtnNavRepos{NULL};
    HWND m_hBtnNavSettings{NULL};
    HWND m_hBtnNavLogs{NULL};

    // Repositories View Controls
    HWND m_hStaticRepoHeader{NULL};
    HWND m_hListRepos{NULL};
    HWND m_hBtnAddRepo{NULL};
    HWND m_hBtnRemoveRepo{NULL};
    HWND m_hBtnOpenFolder{NULL};

    HWND m_hStaticRepoDetails{NULL};
    HWND m_hStaticCommitPrompt{NULL};
    HWND m_hEditCommitMsg{NULL};
    HWND m_hBtnCommit{NULL};

    HWND m_hListHistory{NULL}; // ListView report mode
    HWND m_hBtnRollback{NULL};
    HWND m_hBtnRestoreFolder{NULL};
    HWND m_hBtnInspect{NULL};
    HWND m_hBtnDiff{NULL};

    // S3 Settings View Controls
    HWND m_hGroupS3{NULL};
    HWND m_hLblEndpoint{NULL};
    HWND m_hEditEndpoint{NULL};
    HWND m_hLblRegion{NULL};
    HWND m_hEditRegion{NULL};
    HWND m_hLblBucket{NULL};
    HWND m_hEditBucket{NULL};
    HWND m_hLblAccessKey{NULL};
    HWND m_hEditAccessKey{NULL};
    HWND m_hLblSecretKey{NULL};
    HWND m_hEditSecretKey{NULL};
    HWND m_hChkPathStyle{NULL};
    HWND m_hChkUseSsl{NULL};
    HWND m_hBtnTestS3{NULL};
    HWND m_hBtnSaveS3{NULL};
    HWND m_hStaticS3Status{NULL};

    // Global / Activity Log Controls
    HWND m_hProgressBar{NULL};
    HWND m_hStaticStatus{NULL};
    HWND m_hEditLogs{NULL};
    HWND m_hBtnClearLogs{NULL};
    HWND m_hBtnCopyLogs{NULL};

    // Async worker thread
    std::thread m_workerThread;
    std::atomic<bool> m_isWorking{false};
    std::atomic<bool> m_cancelToken{false};
};

} // namespace cas
