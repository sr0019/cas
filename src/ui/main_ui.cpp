#include "ui_window.hpp"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    SetProcessDPIAware();

    cas::MainWindow app;
    if (!app.create(hInstance, nCmdShow)) {
        MessageBoxW(NULL, L"Failed to create CAS GUI window.", L"Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.run();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return wWinMain(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow);
}

int main(int argc, char* argv[]) {
    HINSTANCE hInstance = GetModuleHandle(NULL);
    return wWinMain(hInstance, NULL, GetCommandLineW(), SW_SHOWDEFAULT);
}
