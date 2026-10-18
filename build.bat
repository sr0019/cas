@echo off
setlocal enabledelayedexpansion

echo ====================================================================
echo  Building CAS (Content Addressable Storage ^& Versioning Client)
echo  Pure C++17 ^| Direct S3 Upload ^| FastCDC ^| CLI ^& Windows Native GUI
echo ====================================================================

set GXX=C:\msys64\ucrt64\bin\g++.exe
if not exist "%GXX%" (
    where g++.exe >nul 2>nul
    if %ERRORLEVEL% equ 0 (
        set GXX=g++.exe
    ) else (
        echo [ERROR] g++.exe compiler not found. Please install MinGW-w64 or MSYS2.
        exit /b 1
    )
)

echo Using compiler: %GXX%

if not exist bin mkdir bin

set CORE_SRC=src\core\crypto_util.cpp src\core\fastcdc.cpp src\core\pack_format.cpp src\core\json.cpp src\core\http_client.cpp src\core\config.cpp src\core\s3_client.cpp src\core\repo_manager.cpp
set LIBS=-lwinhttp -lbcrypt -lz -lcomctl32 -lole32 -lshell32 -luuid

echo.
echo [1/2] Compiling Headless CLI: bin\cas_cli.exe ...
"%GXX%" -std=c++17 -DUNICODE -D_UNICODE -O2 -I src\core %CORE_SRC% src\cli\main_cli.cpp -o bin\cas_cli.exe %LIBS%
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CLI compilation failed!
    exit /b %ERRORLEVEL%
)
echo [OK] Successfully built bin\cas_cli.exe

echo.
echo [2/2] Compiling Windows Native GUI: bin\cas_app.exe ...
"%GXX%" -std=c++17 -DUNICODE -D_UNICODE -O2 -I src\core %CORE_SRC% src\ui\ui_window.cpp src\ui\main_ui.cpp -o bin\cas_app.exe %LIBS% -mwindows
if %ERRORLEVEL% neq 0 (
    echo [ERROR] GUI compilation failed!
    exit /b %ERRORLEVEL%
)
echo [OK] Successfully built bin\cas_app.exe

echo.
echo ====================================================================
echo  BUILD SUCCEEDED!
echo  CLI Executable: bin\cas_cli.exe
echo  GUI Executable: bin\cas_app.exe
echo ====================================================================
