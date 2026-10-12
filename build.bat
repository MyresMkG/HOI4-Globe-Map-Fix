@echo off
setlocal
if not "%MINGW_BIN%"=="" set "PATH=%MINGW_BIN%;%PATH%"
where g++ >nul 2>nul
if errorlevel 1 (
  echo MinGW-w64 g++ not found. Set MINGW_BIN to its bin directory.
  exit /b 1
)
set "ROOT=%~dp0"
set "OUT=%OUTDIR%"
if "%OUT%"=="" set "OUT=%ROOT%..\..\full_releases\Globe Map Fix"
if not exist "%OUT%" mkdir "%OUT%"
g++ -std=c++17 -O2 -ffp-contract=off -fno-math-errno -Wall -Wextra -Wpedantic -Werror -static -static-libgcc -static-libstdc++ -shared "%ROOT%src\dllmain.cpp" -o "%OUT%\Globe Map Fix.dll" -lshell32 -lole32 -luuid -Wl,--no-insert-timestamp
if errorlevel 1 exit /b 1
copy /y "%ROOT%Globe Map Fix.ini" "%OUT%\Globe Map Fix.ini" >nul
if errorlevel 1 exit /b 1
copy /y "%ROOT%README.md" "%OUT%\浣跨敤璇存槑.md" >nul
if errorlevel 1 exit /b 1
echo Built: %OUT%\Globe Map Fix.dll
endlocal
