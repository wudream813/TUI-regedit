@echo off
chcp 65001 >nul
REM build.bat - Windows 构建脚本 (MSVC 或 MinGW 二选一)
REM 用法: 双击运行, 或在“Developer Command Prompt”里执行 build.bat

setlocal
cd /d %~dp0

where cl >nul 2>nul
if %errorlevel%==0 goto :msvc

where g++ >nul 2>nul
if %errorlevel%==0 goto :mingw

echo [错误] 未找到 cl (MSVC) 或 g++ (MinGW)。
echo 请安装其一:
echo   - MSVC: 安装 Visual Studio 并使用“Developer Command Prompt”
echo   - MinGW: 安装 MSYS2 / mingw-w64 并把 bin 加入 PATH
pause
exit /b 1

:msvc
echo [构建] 使用 MSVC (cl)...
if not exist build mkdir build
cl /std:c++17 /EHsc /O2 /Fe:build\tui-regedit.exe src\main.cpp src\app.cpp src\tui.cpp src\registry_mock.cpp src\registry_win.cpp advapi32.lib /source-charset:utf-8 /execution-charset:utf-8
if %errorlevel% neq 0 ( echo [失败] 编译出错。 & pause & exit /b 1 )
echo [成功] build\tui-regedit.exe
echo.
echo 运行: build\tui-regedit.exe        (真实注册表, 改 HKLM 请右键管理员运行)
echo 试玩: build\tui-regedit.exe --mock (演示数据, 安全)
pause
exit /b 0

:mingw
echo [构建] 使用 MinGW (g++)...
if not exist build mkdir build
g++ -std=c++17 -O2 -Wall -Isrc -o build\tui-regedit.exe src\main.cpp src\app.cpp src\tui.cpp src\registry_mock.cpp src\registry_win.cpp -ladvapi32
if %errorlevel% neq 0 ( echo [失败] 编译出错。 & pause & exit /b 1 )
echo [成功] build\tui-regedit.exe
echo.
echo 运行: build\tui-regedit.exe        (真实注册表, 改 HKLM 请右键管理员运行)
echo 试玩: build\tui-regedit.exe --mock (演示数据, 安全)
pause
exit /b 0
