#include "ui_window.hpp"
#include "../core/logger.hpp"
#include "../core/crypto_util.hpp"

#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Control IDs
#define IDC_NAV_REPOS          101
#define IDC_NAV_SETTINGS       102
#define IDC_NAV_LOGS           103

#define IDC_LIST_REPOS         201
#define IDC_BTN_ADD_REPO       202
#define IDC_BTN_REMOVE_REPO    203
#define IDC_BTN_OPEN_FOLDER    204

#define IDC_EDIT_COMMIT_MSG    205
#define IDC_BTN_COMMIT         206
#define IDC_LIST_HISTORY       207
#define IDC_BTN_ROLLBACK       208
#define IDC_BTN_RESTORE_DIR    209
#define IDC_BTN_INSPECT        210
#define IDC_BTN_DIFF           211

#define IDC_EDIT_S3_ENDPOINT   301
#define IDC_EDIT_S3_REGION     302
#define IDC_EDIT_S3_BUCKET     303
#define IDC_EDIT_S3_KEY        304
#define IDC_EDIT_S3_SECRET     305
#define IDC_CHK_PATH_STYLE     306
#define IDC_CHK_USE_SSL        307
#define IDC_BTN_TEST_S3        308
#define IDC_BTN_SAVE_S3        309

#define IDC_EDIT_LOGS          401
#define IDC_BTN_CLEAR_LOGS     402
#define IDC_BTN_COPY_LOGS      403
#define IDC_PROGRESS_BAR       404

// Custom Windows Messages
#define WM_APP_LOG_MESSAGE     (WM_USER + 101)
#define WM_APP_PROGRESS        (WM_USER + 102)
#define WM_APP_WORK_DONE       (WM_USER + 103)

namespace cas {

namespace {

std::wstring toWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

std::string toString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);
    return str;
}

std::string getControlText(HWND hwnd) {
    if (!hwnd) return "";
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return "";
    std::wstring wstr(len + 1, 0);
    GetWindowTextW(hwnd, &wstr[0], len + 1);
    wstr.resize(len);
    return toString(wstr);
}

void setControlText(HWND hwnd, const std::string& text) {
    if (!hwnd || !IsWindow(hwnd)) return;
    std::wstring wstr = toWString(text);
    SetWindowTextW(hwnd, wstr.c_str());
}

std::string browseForFolder(HWND parent, const std::string& title) {
    BROWSEINFOW bi{};
    bi.hwndOwner = parent;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    std::wstring wTitle = toWString(title);
    bi.lpszTitle = wTitle.c_str();

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return "";

    WCHAR szPath[MAX_PATH] = {0};
    if (SHGetPathFromIDListW(pidl, szPath)) {
        CoTaskMemFree(pidl);
        return toString(szPath);
    }
    CoTaskMemFree(pidl);
    return "";
}

} // anonymous namespace

MainWindow::MainWindow() {}

MainWindow::~MainWindow() {
    if (m_workerThread.joinable()) {
        m_cancelToken.store(true);
        m_workerThread.join();
    }
    if (m_fontUi) DeleteObject(m_fontUi);
    if (m_fontBold) DeleteObject(m_fontBold);
    if (m_fontHeader) DeleteObject(m_fontHeader);
    if (m_fontMono) DeleteObject(m_fontMono);
}

bool MainWindow::create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    setupFonts();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = s_wndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"CASMainWindowClass";

    RegisterClassExW(&wc);

    int winWidth = 1060;
    int winHeight = 720;
    int scrWidth = GetSystemMetrics(SM_CXSCREEN);
    int scrHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (scrWidth - winWidth) / 2;
    int y = (scrHeight - winHeight) / 2;

    m_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"CAS - Content Addressable Storage & Versioning System (Direct S3)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, winWidth, winHeight,
        NULL, NULL, hInstance, this
    );

    if (!m_hwnd) return false;

    createControls();
    switchView(0);
    refreshRepoList();
    loadS3SettingsIntoControls();

    // Hook logger
    Logger::instance().addListener([this](LogLevel level, const std::string& formattedMsg, const std::string& rawMsg) {
        if (m_hwnd && IsWindow(m_hwnd)) {
            char* pCopy = new char[formattedMsg.size() + 1];
            strcpy_s(pCopy, formattedMsg.size() + 1, formattedMsg.c_str());
            PostMessageW(m_hwnd, WM_APP_LOG_MESSAGE, (WPARAM)pCopy, 0);
        }
    });

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    Logger::info("CAS Windows Client Ready. Direct S3 Upload & FastCDC Active.");
    return true;
}

int MainWindow::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void MainWindow::setupFonts() {
    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    auto makeFont = [logPixelsY](int ptSize, int weight, const wchar_t* face) {
        int height = -MulDiv(ptSize, logPixelsY, 72);
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    };

    m_fontUi = makeFont(9, FW_NORMAL, L"Segoe UI");
    m_fontBold = makeFont(9, FW_BOLD, L"Segoe UI");
    m_fontHeader = makeFont(12, FW_BOLD, L"Segoe UI");
    m_fontMono = makeFont(9, FW_NORMAL, L"Consolas");
}

void MainWindow::createControls() {
    // 1. Navigation Toolbar
    m_hBtnNavRepos = CreateWindowExW(0, L"BUTTON", L"📁 Repositories", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     10, 10, 150, 32, m_hwnd, (HMENU)IDC_NAV_REPOS, m_hInstance, NULL);
    m_hBtnNavSettings = CreateWindowExW(0, L"BUTTON", L"⚙ S3 Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        165, 10, 150, 32, m_hwnd, (HMENU)IDC_NAV_SETTINGS, m_hInstance, NULL);
    m_hBtnNavLogs = CreateWindowExW(0, L"BUTTON", L"📜 Activity Log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    320, 10, 150, 32, m_hwnd, (HMENU)IDC_NAV_LOGS, m_hInstance, NULL);

    // 2. Repositories View Controls
    // Left Sidebar: Repositories List
    m_hStaticRepoHeader = CreateWindowExW(0, L"STATIC", L"REGISTERED REPOSITORIES", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          10, 52, 260, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hListRepos = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
                                   10, 75, 260, 480, m_hwnd, (HMENU)IDC_LIST_REPOS, m_hInstance, NULL);

    m_hBtnAddRepo = CreateWindowExW(0, L"BUTTON", L"+ Add / Init Repo...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    10, 560, 125, 28, m_hwnd, (HMENU)IDC_BTN_ADD_REPO, m_hInstance, NULL);
    m_hBtnRemoveRepo = CreateWindowExW(0, L"BUTTON", L"- Remove", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       140, 560, 60, 28, m_hwnd, (HMENU)IDC_BTN_REMOVE_REPO, m_hInstance, NULL);
    m_hBtnOpenFolder = CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                       205, 560, 65, 28, m_hwnd, (HMENU)IDC_BTN_OPEN_FOLDER, m_hInstance, NULL);

    // Right Details: Repo Name, Commit Box & History
    m_hStaticRepoDetails = CreateWindowExW(0, L"STATIC", L"Select or add a repository to view history", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                          285, 52, 740, 20, m_hwnd, NULL, m_hInstance, NULL);

    m_hStaticCommitPrompt = CreateWindowExW(0, L"STATIC", L"Commit Message:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                            285, 78, 110, 22, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditCommitMsg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Updated project assets and code",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       400, 75, 430, 24, m_hwnd, (HMENU)IDC_EDIT_COMMIT_MSG, m_hInstance, NULL);
    m_hBtnCommit = CreateWindowExW(0, L"BUTTON", L"🚀 Snapshot & Upload to S3", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   840, 73, 190, 28, m_hwnd, (HMENU)IDC_BTN_COMMIT, m_hInstance, NULL);

    // History ListView
    m_hListHistory = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                     285, 110, 745, 445, m_hwnd, (HMENU)IDC_LIST_HISTORY, m_hInstance, NULL);
    ListView_SetExtendedListViewStyle(m_hListHistory, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Add History ListView Columns
    auto addCol = [this](int idx, const wchar_t* title, int width) {
        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.pszText = const_cast<LPWSTR>(title);
        lvc.cx = width;
        ListView_InsertColumn(m_hListHistory, idx, &lvc);
    };

    addCol(0, L"Version", 65);
    addCol(1, L"Commit ID", 80);
    addCol(2, L"Message", 190);
    addCol(3, L"Date & Time", 130);
    addCol(4, L"Files", 55);
    addCol(5, L"Raw Size", 75);
    addCol(6, L"Uploaded", 75);
    addCol(7, L"Dedup %", 65);

    // Action buttons for selected version
    m_hBtnRollback = CreateWindowExW(0, L"BUTTON", L"⏪ Rollback Working Dir", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     285, 560, 180, 28, m_hwnd, (HMENU)IDC_BTN_ROLLBACK, m_hInstance, NULL);
    m_hBtnRestoreFolder = CreateWindowExW(0, L"BUTTON", L"📂 Export to Folder...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         475, 560, 150, 28, m_hwnd, (HMENU)IDC_BTN_RESTORE_DIR, m_hInstance, NULL);
    m_hBtnInspect = CreateWindowExW(0, L"BUTTON", L"🔍 Inspect Files", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    635, 560, 120, 28, m_hwnd, (HMENU)IDC_BTN_INSPECT, m_hInstance, NULL);
    m_hBtnDiff = CreateWindowExW(0, L"BUTTON", L"⚖ Diff vs Working Dir", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 765, 560, 155, 28, m_hwnd, (HMENU)IDC_BTN_DIFF, m_hInstance, NULL);

    // 3. S3 Settings View Controls
    m_hGroupS3 = CreateWindowExW(0, L"BUTTON", L" Direct S3 Connection Settings (Zero Server Dependency) ",
                                 WS_CHILD | BS_GROUPBOX,
                                 20, 60, 680, 360, m_hwnd, NULL, m_hInstance, NULL);

    m_hLblEndpoint = CreateWindowExW(0, L"STATIC", L"S3 Endpoint URL:", WS_CHILD | SS_LEFT,
                                     40, 95, 140, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditEndpoint = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"https://s3.amazonaws.com",
                                      WS_CHILD | ES_AUTOHSCROLL,
                                      190, 92, 480, 24, m_hwnd, (HMENU)IDC_EDIT_S3_ENDPOINT, m_hInstance, NULL);

    m_hLblRegion = CreateWindowExW(0, L"STATIC", L"S3 Region:", WS_CHILD | SS_LEFT,
                                   40, 130, 140, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditRegion = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"us-east-1",
                                    WS_CHILD | ES_AUTOHSCROLL,
                                    190, 127, 480, 24, m_hwnd, (HMENU)IDC_EDIT_S3_REGION, m_hInstance, NULL);

    m_hLblBucket = CreateWindowExW(0, L"STATIC", L"S3 Bucket Name:", WS_CHILD | SS_LEFT,
                                   40, 165, 140, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditBucket = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"vstore-storage",
                                    WS_CHILD | ES_AUTOHSCROLL,
                                    190, 162, 480, 24, m_hwnd, (HMENU)IDC_EDIT_S3_BUCKET, m_hInstance, NULL);

    m_hLblAccessKey = CreateWindowExW(0, L"STATIC", L"Access Key ID:", WS_CHILD | SS_LEFT,
                                      40, 200, 140, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditAccessKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | ES_AUTOHSCROLL,
                                       190, 197, 480, 24, m_hwnd, (HMENU)IDC_EDIT_S3_KEY, m_hInstance, NULL);

    m_hLblSecretKey = CreateWindowExW(0, L"STATIC", L"Secret Access Key:", WS_CHILD | SS_LEFT,
                                      40, 235, 140, 20, m_hwnd, NULL, m_hInstance, NULL);
    m_hEditSecretKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD,
                                       190, 232, 480, 24, m_hwnd, (HMENU)IDC_EDIT_S3_SECRET, m_hInstance, NULL);

    m_hChkPathStyle = CreateWindowExW(0, L"BUTTON", L"Use Path-Style URLs (Required for MinIO, LocalStack, and Ceph)",
                                      WS_CHILD | BS_AUTOCHECKBOX,
                                      190, 270, 480, 22, m_hwnd, (HMENU)IDC_CHK_PATH_STYLE, m_hInstance, NULL);
    m_hChkUseSsl = CreateWindowExW(0, L"BUTTON", L"Use SSL / HTTPS",
                                   WS_CHILD | BS_AUTOCHECKBOX,
                                   190, 298, 480, 22, m_hwnd, (HMENU)IDC_CHK_USE_SSL, m_hInstance, NULL);

    m_hBtnTestS3 = CreateWindowExW(0, L"BUTTON", L"🔌 Test S3 Connection", WS_CHILD | BS_PUSHBUTTON,
                                   190, 335, 180, 30, m_hwnd, (HMENU)IDC_BTN_TEST_S3, m_hInstance, NULL);
    m_hBtnSaveS3 = CreateWindowExW(0, L"BUTTON", L"💾 Save Settings", WS_CHILD | BS_DEFPUSHBUTTON,
                                   385, 335, 160, 30, m_hwnd, (HMENU)IDC_BTN_SAVE_S3, m_hInstance, NULL);

    m_hStaticS3Status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT,
                                        190, 375, 480, 35, m_hwnd, NULL, m_hInstance, NULL);

    // 4. Global Bottom Bar: Progress & Status
    m_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
                                     WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                     10, 600, 1020, 16, m_hwnd, (HMENU)IDC_PROGRESS_BAR, m_hInstance, NULL);
    m_hStaticStatus = CreateWindowExW(0, L"STATIC", L"Ready.", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                      10, 622, 1020, 20, m_hwnd, NULL, m_hInstance, NULL);

    // 5. Activity Log View Controls
    m_hEditLogs = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
                                  10, 52, 1020, 535, m_hwnd, (HMENU)IDC_EDIT_LOGS, m_hInstance, NULL);
    m_hBtnClearLogs = CreateWindowExW(0, L"BUTTON", L"Clear Logs", WS_CHILD | BS_PUSHBUTTON,
                                      810, 12, 100, 28, m_hwnd, (HMENU)IDC_BTN_CLEAR_LOGS, m_hInstance, NULL);
    m_hBtnCopyLogs = CreateWindowExW(0, L"BUTTON", L"Copy Logs", WS_CHILD | BS_PUSHBUTTON,
                                     920, 12, 110, 28, m_hwnd, (HMENU)IDC_BTN_COPY_LOGS, m_hInstance, NULL);

    // Set Fonts on all controls
    HWND allControls[] = {
        m_hBtnNavRepos, m_hBtnNavSettings, m_hBtnNavLogs,
        m_hListRepos, m_hBtnAddRepo, m_hBtnRemoveRepo, m_hBtnOpenFolder,
        m_hStaticRepoDetails, m_hStaticCommitPrompt, m_hEditCommitMsg, m_hBtnCommit,
        m_hListHistory, m_hBtnRollback, m_hBtnRestoreFolder, m_hBtnInspect, m_hBtnDiff,
        m_hGroupS3, m_hLblEndpoint, m_hEditEndpoint, m_hLblRegion, m_hEditRegion,
        m_hLblBucket, m_hEditBucket, m_hLblAccessKey, m_hEditAccessKey, m_hLblSecretKey, m_hEditSecretKey,
        m_hChkPathStyle, m_hChkUseSsl, m_hBtnTestS3, m_hBtnSaveS3, m_hStaticS3Status,
        m_hStaticStatus, m_hBtnClearLogs, m_hBtnCopyLogs
    };

    for (HWND h : allControls) {
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)m_fontUi, TRUE);
    }

    SendMessageW(m_hStaticRepoHeader, WM_SETFONT, (WPARAM)m_fontBold, TRUE);
    SendMessageW(m_hEditLogs, WM_SETFONT, (WPARAM)m_fontMono, TRUE);
}

void MainWindow::switchView(int viewIndex) {
    m_activeView = viewIndex;

    bool showRepos = (viewIndex == 0);
    bool showSettings = (viewIndex == 1);
    bool showLogs = (viewIndex == 2);

    auto show = [](HWND h, bool b) { if (h) ShowWindow(h, b ? SW_SHOW : SW_HIDE); };

    // Repos View
    show(m_hStaticRepoHeader, showRepos);
    show(m_hListRepos, showRepos);
    show(m_hBtnAddRepo, showRepos);
    show(m_hBtnRemoveRepo, showRepos);
    show(m_hBtnOpenFolder, showRepos);
    show(m_hStaticRepoDetails, showRepos);
    show(m_hStaticCommitPrompt, showRepos);
    show(m_hEditCommitMsg, showRepos);
    show(m_hBtnCommit, showRepos);
    show(m_hListHistory, showRepos);
    show(m_hBtnRollback, showRepos);
    show(m_hBtnRestoreFolder, showRepos);
    show(m_hBtnInspect, showRepos);
    show(m_hBtnDiff, showRepos);

    // Settings View
    show(m_hGroupS3, showSettings);
    show(m_hLblEndpoint, showSettings);
    show(m_hEditEndpoint, showSettings);
    show(m_hLblRegion, showSettings);
    show(m_hEditRegion, showSettings);
    show(m_hLblBucket, showSettings);
    show(m_hEditBucket, showSettings);
    show(m_hLblAccessKey, showSettings);
    show(m_hEditAccessKey, showSettings);
    show(m_hLblSecretKey, showSettings);
    show(m_hEditSecretKey, showSettings);
    show(m_hChkPathStyle, showSettings);
    show(m_hChkUseSsl, showSettings);
    show(m_hBtnTestS3, showSettings);
    show(m_hBtnSaveS3, showSettings);
    show(m_hStaticS3Status, showSettings);

    // Logs View
    show(m_hEditLogs, showLogs);
    show(m_hBtnClearLogs, showLogs);
    show(m_hBtnCopyLogs, showLogs);

    // Invalidate main window
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void MainWindow::layoutControls(int width, int height) {
    if (width <= 0 || height <= 0) return;

    // Repos View layout
    int sidebarWidth = 270;
    int bottomBarY = height - 55;
    int mainAreaX = sidebarWidth + 20;
    int mainAreaWidth = width - mainAreaX - 15;

    SetWindowPos(m_hListRepos, NULL, 10, 75, sidebarWidth, bottomBarY - 120, SWP_NOZORDER);
    SetWindowPos(m_hBtnAddRepo, NULL, 10, bottomBarY - 35, 130, 28, SWP_NOZORDER);
    SetWindowPos(m_hBtnRemoveRepo, NULL, 145, bottomBarY - 35, 60, 28, SWP_NOZORDER);
    SetWindowPos(m_hBtnOpenFolder, NULL, 210, bottomBarY - 35, 70, 28, SWP_NOZORDER);

    SetWindowPos(m_hStaticRepoDetails, NULL, mainAreaX, 52, mainAreaWidth, 20, SWP_NOZORDER);
    SetWindowPos(m_hEditCommitMsg, NULL, mainAreaX + 115, 75, (std::max)(150, mainAreaWidth - 315), 24, SWP_NOZORDER);
    SetWindowPos(m_hBtnCommit, NULL, width - 195, 73, 185, 28, SWP_NOZORDER);

    SetWindowPos(m_hListHistory, NULL, mainAreaX, 110, mainAreaWidth, bottomBarY - 155, SWP_NOZORDER);

    SetWindowPos(m_hBtnRollback, NULL, mainAreaX, bottomBarY - 35, 175, 28, SWP_NOZORDER);
    SetWindowPos(m_hBtnRestoreFolder, NULL, mainAreaX + 185, bottomBarY - 35, 150, 28, SWP_NOZORDER);
    SetWindowPos(m_hBtnInspect, NULL, mainAreaX + 345, bottomBarY - 35, 120, 28, SWP_NOZORDER);
    SetWindowPos(m_hBtnDiff, NULL, mainAreaX + 475, bottomBarY - 35, 155, 28, SWP_NOZORDER);

    // Global bottom progress
    SetWindowPos(m_hProgressBar, NULL, 10, height - 45, width - 20, 16, SWP_NOZORDER);
    SetWindowPos(m_hStaticStatus, NULL, 10, height - 26, width - 20, 20, SWP_NOZORDER);

    // Logs view
    SetWindowPos(m_hEditLogs, NULL, 10, 52, width - 20, height - 110, SWP_NOZORDER);
}

void MainWindow::refreshRepoList() {
    SendMessageW(m_hListRepos, LB_RESETCONTENT, 0, 0);
    auto repos = RepoManager::instance().listRepos();

    for (const auto& r : repos) {
        std::string label = r.name;
        if (!r.headVersion.empty()) {
            label += " [" + r.headVersion + "]";
        }
        std::wstring wLabel = toWString(label);
        SendMessageW(m_hListRepos, LB_ADDSTRING, 0, (LPARAM)wLabel.c_str());
    }

    if (!repos.empty()) {
        SendMessageW(m_hListRepos, LB_SETCURSEL, 0, 0);
        refreshHistoryList(repos[0].path);
    } else {
        ListView_DeleteAllItems(m_hListHistory);
        setControlText(m_hStaticRepoDetails, "No repositories registered. Click '+ Add / Init Repo' to begin.");
    }
}

std::string MainWindow::getSelectedRepoPath() {
    int idx = (int)SendMessageW(m_hListRepos, LB_GETCURSEL, 0, 0);
    if (idx == LB_ERR) return "";

    auto repos = RepoManager::instance().listRepos();
    if (idx >= 0 && idx < static_cast<int>(repos.size())) {
        return repos[idx].path;
    }
    return "";
}

std::string MainWindow::getSelectedVersionTag() {
    int idx = ListView_GetNextItem(m_hListHistory, -1, LVNI_SELECTED);
    if (idx == -1) return "";

    WCHAR szText[64] = {0};
    ListView_GetItemText(m_hListHistory, idx, 0, szText, 64);
    return toString(szText);
}

void MainWindow::refreshHistoryList(const std::string& repoPath) {
    ListView_DeleteAllItems(m_hListHistory);
    if (repoPath.empty()) return;

    RepoInfo info;
    if (RepoManager::instance().getRepo(repoPath, info)) {
        std::string details = "Repo: " + info.name + " (" + info.path + ") | HEAD: " +
                              (info.headVersion.empty() ? "(none)" : info.headVersion) +
                              " | Total Versions: " + std::to_string(info.totalVersions);
        setControlText(m_hStaticRepoDetails, details);
    }

    auto history = RepoManager::instance().getHistory(repoPath);

    for (int i = static_cast<int>(history.size()) - 1; i >= 0; --i) {
        const auto& rec = history[i];

        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(history.size()) - 1 - i;

        std::wstring wTag = toWString(rec.versionTag);
        lvi.pszText = const_cast<LPWSTR>(wTag.c_str());
        int itemIndex = ListView_InsertItem(m_hListHistory, &lvi);

        std::wstring wId = toWString(rec.versionId.substr(0, 8));
        ListView_SetItemText(m_hListHistory, itemIndex, 1, const_cast<LPWSTR>(wId.c_str()));

        std::wstring wMsg = toWString(rec.message);
        ListView_SetItemText(m_hListHistory, itemIndex, 2, const_cast<LPWSTR>(wMsg.c_str()));

        std::wstring wTime = toWString(rec.timestamp);
        ListView_SetItemText(m_hListHistory, itemIndex, 3, const_cast<LPWSTR>(wTime.c_str()));

        std::wstring wFiles = toWString(std::to_string(rec.fileCount));
        ListView_SetItemText(m_hListHistory, itemIndex, 4, const_cast<LPWSTR>(wFiles.c_str()));

        std::wstring wRaw = toWString(CryptoUtil::formatBytes(rec.totalRawBytes));
        ListView_SetItemText(m_hListHistory, itemIndex, 5, const_cast<LPWSTR>(wRaw.c_str()));

        std::wstring wStored = toWString(CryptoUtil::formatBytes(rec.totalStoredBytes));
        ListView_SetItemText(m_hListHistory, itemIndex, 6, const_cast<LPWSTR>(wStored.c_str()));

        std::wstring wDedup = toWString(rec.dedupRatio);
        ListView_SetItemText(m_hListHistory, itemIndex, 7, const_cast<LPWSTR>(wDedup.c_str()));
    }

    if (!history.empty()) {
        ListView_SetItemState(m_hListHistory, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void MainWindow::loadS3SettingsIntoControls() {
    const auto& s3 = ConfigManager::instance().getS3();
    setControlText(m_hEditEndpoint, s3.endpoint);
    setControlText(m_hEditRegion, s3.region);
    setControlText(m_hEditBucket, s3.bucket);
    setControlText(m_hEditAccessKey, s3.accessKey);
    setControlText(m_hEditSecretKey, s3.secretKey);
    SendMessageW(m_hChkPathStyle, BM_SETCHECK, s3.pathStyle ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_hChkUseSsl, BM_SETCHECK, s3.useSsl ? BST_CHECKED : BST_UNCHECKED, 0);
}

void MainWindow::saveS3SettingsFromControls() {
    S3Config s3;
    s3.endpoint = getControlText(m_hEditEndpoint);
    s3.region = getControlText(m_hEditRegion);
    s3.bucket = getControlText(m_hEditBucket);
    s3.accessKey = getControlText(m_hEditAccessKey);
    s3.secretKey = getControlText(m_hEditSecretKey);
    s3.pathStyle = (SendMessageW(m_hChkPathStyle, BM_GETCHECK, 0, 0) == BST_CHECKED);
    s3.useSsl = (SendMessageW(m_hChkUseSsl, BM_GETCHECK, 0, 0) == BST_CHECKED);

    ConfigManager::instance().setS3(s3);
    if (ConfigManager::instance().save()) {
        RepoManager::instance().getS3Client().setConfig(s3);
        setControlText(m_hStaticS3Status, "Settings saved to disk successfully.");
        MessageBoxW(m_hwnd, L"S3 settings saved successfully!", L"Settings Saved", MB_OK | MB_ICONINFORMATION);
    } else {
        setControlText(m_hStaticS3Status, "Failed to save settings to disk.");
    }
}

void MainWindow::startTestS3() {
    if (m_isWorking.load()) return;
    saveS3SettingsFromControls();

    setControlText(m_hStaticS3Status, "Testing S3 connection with SigV4...");
    m_isWorking.store(true);

    if (m_workerThread.joinable()) m_workerThread.join();
    m_workerThread = std::thread([this]() {
        std::string details;
        bool ok = RepoManager::instance().getS3Client().testConnection(details);
        m_isWorking.store(false);

        if (m_hwnd && IsWindow(m_hwnd)) {
            char* pCopy = new char[details.size() + 1];
            strcpy_s(pCopy, details.size() + 1, details.c_str());
            PostMessageW(m_hwnd, WM_APP_WORK_DONE, ok ? 1 : 0, (LPARAM)pCopy);
        }
    });
}

void MainWindow::startCommit() {
    if (m_isWorking.load()) return;

    std::string repoPath = getSelectedRepoPath();
    if (repoPath.empty()) {
        MessageBoxW(m_hwnd, L"Please select or add a repository first.", L"No Repository", MB_OK | MB_ICONWARNING);
        return;
    }

    std::string commitMsg = getControlText(m_hEditCommitMsg);
    if (commitMsg.empty()) commitMsg = "Snapshot commit";

    m_isWorking.store(true);
    m_cancelToken.store(false);
    setControlText(m_hStaticStatus, "Starting snapshot commit and direct S3 upload...");
    updateProgress(0, 100);

    if (m_workerThread.joinable()) m_workerThread.join();
    m_workerThread = std::thread([this, repoPath, commitMsg]() {
        try {
            auto metrics = RepoManager::instance().commit(
                repoPath, commitMsg,
                [this](uint64_t cur, uint64_t total) {
                    this->updateProgress(cur, total);
                },
                &m_cancelToken
            );

            std::string res = "Commit [" + metrics.versionTag + "] done! Dedup ratio: " + metrics.dedupRatio +
                              " | Uploaded: " + CryptoUtil::formatBytes(metrics.bytesUploaded);
            m_isWorking.store(false);

            char* pCopy = new char[res.size() + 1];
            strcpy_s(pCopy, res.size() + 1, res.c_str());
            PostMessageW(m_hwnd, WM_APP_WORK_DONE, 1, (LPARAM)pCopy);
        } catch (const std::exception& ex) {
            m_isWorking.store(false);
            std::string err = "Commit error: " + std::string(ex.what());
            char* pCopy = new char[err.size() + 1];
            strcpy_s(pCopy, err.size() + 1, err.c_str());
            PostMessageW(m_hwnd, WM_APP_WORK_DONE, 0, (LPARAM)pCopy);
        }
    });
}

void MainWindow::startRollback(bool toCustomFolder) {
    if (m_isWorking.load()) return;

    std::string repoPath = getSelectedRepoPath();
    if (repoPath.empty()) return;

    std::string verTag = getSelectedVersionTag();
    if (verTag.empty()) {
        MessageBoxW(m_hwnd, L"Please select a version from the history table to rollback/restore.", L"No Version Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::string targetDir = repoPath;
    if (toCustomFolder) {
        targetDir = browseForFolder(m_hwnd, "Select destination folder to restore " + verTag);
        if (targetDir.empty()) return;
    } else {
        std::wstring confirmMsg = L"Are you sure you want to rollback the working directory to " + toWString(verTag) +
                                  L"?\n\nAny local files differing from this snapshot will be updated or replaced.";
        if (MessageBoxW(m_hwnd, confirmMsg.c_str(), L"Confirm Rollback", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
    }

    m_isWorking.store(true);
    m_cancelToken.store(false);
    setControlText(m_hStaticStatus, "Rolling back to " + verTag + " ...");
    updateProgress(0, 100);

    if (m_workerThread.joinable()) m_workerThread.join();
    m_workerThread = std::thread([this, repoPath, verTag, targetDir]() {
        try {
            auto metrics = RepoManager::instance().rollback(
                repoPath, verTag, targetDir,
                [this](uint64_t cur, uint64_t total) {
                    this->updateProgress(cur, total);
                },
                &m_cancelToken
            );

            std::string res = "Rollback to " + metrics.targetVersion + " complete! Restored: " +
                              std::to_string(metrics.filesRestored) + " | Skipped (identical): " +
                              std::to_string(metrics.identicalFilesSkipped) + " | Downloaded: " +
                              CryptoUtil::formatBytes(metrics.bytesDownloaded);
            m_isWorking.store(false);

            char* pCopy = new char[res.size() + 1];
            strcpy_s(pCopy, res.size() + 1, res.c_str());
            PostMessageW(m_hwnd, WM_APP_WORK_DONE, 2, (LPARAM)pCopy);
        } catch (const std::exception& ex) {
            m_isWorking.store(false);
            std::string err = "Rollback error: " + std::string(ex.what());
            char* pCopy = new char[err.size() + 1];
            strcpy_s(pCopy, err.size() + 1, err.c_str());
            PostMessageW(m_hwnd, WM_APP_WORK_DONE, 0, (LPARAM)pCopy);
        }
    });
}

void MainWindow::startInspect() {
    std::string repoPath = getSelectedRepoPath();
    std::string verTag = getSelectedVersionTag();
    if (repoPath.empty() || verTag.empty()) return;

    VersionManifest man;
    if (!RepoManager::instance().getManifest(repoPath, verTag, man)) {
        MessageBoxW(m_hwnd, L"Could not load manifest for this version.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    std::ostringstream ss;
    ss << "Snapshot " << man.versionId << " (" << verTag << ")\n"
       << "Date: " << man.timestamp << "\n"
       << "Message: " << man.message << "\n"
       << "Files (" << man.files.size() << ", Total: " << CryptoUtil::formatBytes(man.totalRawBytes) << "):\n\n";

    for (size_t i = 0; i < (std::min)(man.files.size(), size_t(30)); ++i) {
        ss << "• " << man.files[i].path << " (" << CryptoUtil::formatBytes(man.files[i].size)
           << ", " << man.files[i].chunks.size() << " chunks)\n";
    }
    if (man.files.size() > 30) {
        ss << "... and " << (man.files.size() - 30) << " more files.\n";
    }

    std::wstring wText = toWString(ss.str());
    MessageBoxW(m_hwnd, wText.c_str(), L"Manifest File Inspector", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::startDiff() {
    std::string repoPath = getSelectedRepoPath();
    std::string verTag = getSelectedVersionTag();
    if (repoPath.empty() || verTag.empty()) return;

    try {
        auto res = RepoManager::instance().diff(repoPath, verTag);
        std::ostringstream ss;
        ss << "Diff: Snapshot [" << res.fromVersion << "] vs Current Working Directory\n"
           << "====================================================\n"
           << "Identical: " << res.identicalFiles.size() << " files\n"
           << "Added:     " << res.addedFiles.size() << " files\n"
           << "Modified:  " << res.modifiedFiles.size() << " files\n"
           << "Deleted:   " << res.deletedFiles.size() << " files\n\n";

        for (size_t i = 0; i < (std::min)(res.addedFiles.size(), size_t(10)); ++i)
            ss << "+ Added: " << res.addedFiles[i] << "\n";
        for (size_t i = 0; i < (std::min)(res.modifiedFiles.size(), size_t(10)); ++i)
            ss << "~ Modified: " << res.modifiedFiles[i] << "\n";
        for (size_t i = 0; i < (std::min)(res.deletedFiles.size(), size_t(10)); ++i)
            ss << "- Deleted: " << res.deletedFiles[i] << "\n";

        std::wstring wText = toWString(ss.str());
        MessageBoxW(m_hwnd, wText.c_str(), L"Version Diff", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& ex) {
        std::wstring err = toWString(ex.what());
        MessageBoxW(m_hwnd, err.c_str(), L"Diff Error", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::updateProgress(uint64_t current, uint64_t total) {
    if (m_hwnd && IsWindow(m_hwnd)) {
        PostMessageW(m_hwnd, WM_APP_PROGRESS, (WPARAM)current, (LPARAM)total);
    }
}

void MainWindow::appendLog(const std::string& line) {
    if (!m_hEditLogs) return;
    int len = GetWindowTextLengthW(m_hEditLogs);
    SendMessageW(m_hEditLogs, EM_SETSEL, len, len);
    std::wstring wLine = toWString(line + "\r\n");
    SendMessageW(m_hEditLogs, EM_REPLACESEL, FALSE, (LPARAM)wLine.c_str());
}

LRESULT CALLBACK MainWindow::s_wndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            layoutControls(width, height);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (id == IDC_NAV_REPOS) {
                switchView(0);
                return 0;
            }
            if (id == IDC_NAV_SETTINGS) {
                switchView(1);
                return 0;
            }
            if (id == IDC_NAV_LOGS) {
                switchView(2);
                return 0;
            }

            if (id == IDC_LIST_REPOS && code == LBN_SELCHANGE) {
                std::string path = getSelectedRepoPath();
                refreshHistoryList(path);
                return 0;
            }

            if (id == IDC_BTN_ADD_REPO) {
                std::string folder = browseForFolder(m_hwnd, "Select a folder to register as a CAS repository");
                if (!folder.empty()) {
                    RepoManager::instance().addRepo(folder);
                    refreshRepoList();
                }
                return 0;
            }

            if (id == IDC_BTN_REMOVE_REPO) {
                std::string path = getSelectedRepoPath();
                if (!path.empty()) {
                    RepoManager::instance().removeRepo(path);
                    refreshRepoList();
                }
                return 0;
            }

            if (id == IDC_BTN_OPEN_FOLDER) {
                std::string path = getSelectedRepoPath();
                if (!path.empty()) {
                    ShellExecuteW(NULL, L"open", toWString(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                return 0;
            }

            if (id == IDC_BTN_COMMIT) {
                startCommit();
                return 0;
            }

            if (id == IDC_BTN_ROLLBACK) {
                startRollback(false);
                return 0;
            }

            if (id == IDC_BTN_RESTORE_DIR) {
                startRollback(true);
                return 0;
            }

            if (id == IDC_BTN_INSPECT) {
                startInspect();
                return 0;
            }

            if (id == IDC_BTN_DIFF) {
                startDiff();
                return 0;
            }

            if (id == IDC_BTN_TEST_S3) {
                startTestS3();
                return 0;
            }

            if (id == IDC_BTN_SAVE_S3) {
                saveS3SettingsFromControls();
                return 0;
            }

            if (id == IDC_BTN_CLEAR_LOGS) {
                SetWindowTextW(m_hEditLogs, L"");
                return 0;
            }

            if (id == IDC_BTN_COPY_LOGS) {
                int len = GetWindowTextLengthW(m_hEditLogs);
                if (len > 0) {
                    std::wstring wstr(len + 1, 0);
                    GetWindowTextW(m_hEditLogs, &wstr[0], len + 1);
                    if (OpenClipboard(m_hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(WCHAR));
                        if (hGlob) {
                            memcpy(GlobalLock(hGlob), wstr.c_str(), (len + 1) * sizeof(WCHAR));
                            GlobalUnlock(hGlob);
                            SetClipboardData(CF_UNICODETEXT, hGlob);
                        }
                        CloseClipboard();
                    }
                }
                return 0;
            }

            break;
        }

        case WM_APP_LOG_MESSAGE: {
            char* pMsg = reinterpret_cast<char*>(wParam);
            if (pMsg) {
                appendLog(pMsg);
                delete[] pMsg;
            }
            return 0;
        }

        case WM_APP_PROGRESS: {
            uint64_t current = static_cast<uint64_t>(wParam);
            uint64_t total = static_cast<uint64_t>(lParam);
            if (total > 0) {
                int pct = static_cast<int>((current * 100) / total);
                SendMessageW(m_hProgressBar, PBM_SETPOS, pct, 0);
                std::string statusText = "Processing: " + std::to_string(pct) + "% (" +
                                         std::to_string(current) + " / " + std::to_string(total) + ")";
                setControlText(m_hStaticStatus, statusText);
            }
            return 0;
        }

        case WM_APP_WORK_DONE: {
            int code = static_cast<int>(wParam);
            char* pMsg = reinterpret_cast<char*>(lParam);
            std::string msg = pMsg ? pMsg : "";
            delete[] pMsg;

            SendMessageW(m_hProgressBar, PBM_SETPOS, code != 0 ? 100 : 0, 0);
            setControlText(m_hStaticStatus, msg);

            if (code == 1) { // Commit done
                refreshHistoryList(getSelectedRepoPath());
                MessageBoxW(m_hwnd, toWString(msg).c_str(), L"Commit Successful", MB_OK | MB_ICONINFORMATION);
            } else if (code == 2) { // Rollback done
                refreshHistoryList(getSelectedRepoPath());
                MessageBoxW(m_hwnd, toWString(msg).c_str(), L"Rollback Successful", MB_OK | MB_ICONINFORMATION);
            } else if (code == 0) { // Error
                setControlText(m_hStaticS3Status, msg);
                MessageBoxW(m_hwnd, toWString(msg).c_str(), L"Operation Notice", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

} // namespace cas
