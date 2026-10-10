@echo off
setlocal
cd /d "%~dp0\.."
set "GCC="
if defined ORB_GCC set "GCC=%ORB_GCC%"
if not defined GCC if defined MSYS2_LOCATION set "GCC=%MSYS2_LOCATION%\ucrt64\bin\gcc.exe"
if defined GCC if not exist "%GCC%" (
  echo ERROR: Explicit UCRT64 compiler does not exist.
  exit /b 1
)
if not defined GCC if exist "C:\msys64\ucrt64\bin\gcc.exe" set "GCC=C:\msys64\ucrt64\bin\gcc.exe"
if not defined GCC if exist "C:\tools\msys64\ucrt64\bin\gcc.exe" set "GCC=C:\tools\msys64\ucrt64\bin\gcc.exe"
if not defined GCC for %%G in (gcc.exe) do set "GCC=%%~$PATH:G"
if not defined GCC (
  echo ERROR: MSYS2 UCRT64 GCC required.
  exit /b 1
)
for %%D in ("%GCC%") do set "PATH=%%~dpD;%PATH%"
set "OUT_DIR=%CD%\bin"
if defined ORB_OUT_DIR set "OUT_DIR=%ORB_OUT_DIR%"
for %%D in ("%OUT_DIR%") do set "OUT_DIR=%%~fD"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if not exist "%OUT_DIR%" exit /b 1
"%GCC%" -dumpmachine > "%OUT_DIR%\compiler-target.txt"
if errorlevel 1 exit /b 1
set /p "TARGET=" < "%OUT_DIR%\compiler-target.txt"
if not "%TARGET%"=="x86_64-w64-mingw32" (
  echo ERROR: An x64 MinGW UCRT64 compiler is required.
  exit /b 1
)
"%GCC%" -dM -E -x c -include _mingw.h NUL > "%OUT_DIR%\compiler-macros.txt"
if errorlevel 1 exit /b 1
findstr /b /c:"#define _UCRT " "%OUT_DIR%\compiler-macros.txt" >nul
if errorlevel 1 (
  echo ERROR: The compiler headers must target UCRT.
  exit /b 1
)
for %%D in ("%GCC%") do set "WINDRES=%%~dpDwindres.exe"
"%WINDRES%" -I src src\helper.rc -O coff -o "%OUT_DIR%\helper-resource.o"
if errorlevel 1 exit /b 1
set "FLAGS=-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -D_WIN32_WINNT=0x0602 -Wl,--no-insert-timestamp"
"%GCC%" --version
"%GCC%" --version > "%OUT_DIR%\compiler-version.txt"
for %%D in ("%GCC%") do set "GCC_DIR=%%~dpD"
for %%D in ("%GCC_DIR%..\..") do set "MSYS_ROOT=%%~fD"
"%GCC_DIR%objdump.exe" --version > "%OUT_DIR%\binutils-version.txt"
if errorlevel 1 exit /b 1
"%MSYS_ROOT%\usr\bin\pacman.exe" -Q mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-binutils mingw-w64-ucrt-x86_64-crt mingw-w64-ucrt-x86_64-headers > "%OUT_DIR%\compiler-packages.txt"
if errorlevel 1 exit /b 1
git rev-parse HEAD > "%OUT_DIR%\build-source-revision.txt"
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode src\helper.c src\discord_process.c src\cdp.c src\orb_state.c "%OUT_DIR%\helper-resource.o" -o "%OUT_DIR%\orb-cdp.exe" -lwinhttp -lbcrypt -luser32 -lshell32 -liphlpapi -lws2_32
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode tests\controller_driver.c src\cdp.c -o "%OUT_DIR%\controller-test.exe"
if errorlevel 1 exit /b 1
if not exist "%OUT_DIR%\mock" mkdir "%OUT_DIR%\mock"
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -DORB_CDP_TEST_MOCK -municode src\helper.c src\orb_state.c "%OUT_DIR%\helper-resource.o" -o "%OUT_DIR%\mock\orb-cdp.exe" -lwinhttp -lbcrypt
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode tests\payload_test.c -o "%OUT_DIR%\payload-test.exe" -lbcrypt
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." tests\orb_state_test.c src\orb_state.c -o "%OUT_DIR%\orb-state-test.exe"
if errorlevel 1 exit /b 1
echo Native helper, separate fixture helper, payload/state tests and controller driver built.
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode tests\discord_process_test.c src\discord_process.c src\cdp.c -o "%OUT_DIR%\process-test.exe" -luser32 -lshell32 -liphlpapi -lws2_32
if errorlevel 1 exit /b 1
echo Native Discord process test driver built.
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -c src\settings.c -o "%OUT_DIR%\settings.o"
if errorlevel 1 exit /b 1
for %%D in ("%GCC%") do set "WINDRES=%%~dpDwindres.exe"
"%WINDRES%" -I src src\gui.rc -O coff -o "%OUT_DIR%\gui-resource.o"
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode -mwindows src\main.c src\gui.c src\settings.c src\discord_process.c src\cdp.c src\orb_state.c "%OUT_DIR%\gui-resource.o" -o "%OUT_DIR%\24510_0rb.exe" -lbcrypt -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -liphlpapi -lws2_32
if errorlevel 1 exit /b 1
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -DORB_UI_TEST_MOCK -municode -mwindows src\main.c src\gui.c src\settings.c src\discord_process.c src\cdp.c src\orb_state.c "%OUT_DIR%\gui-resource.o" -o "%OUT_DIR%\mock\24510_0rb-ui-mock.exe" -lbcrypt -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -liphlpapi -lws2_32
if errorlevel 1 exit /b 1
if not exist "%OUT_DIR%\payloads" mkdir "%OUT_DIR%\payloads"
if errorlevel 1 exit /b 1
if not exist "%OUT_DIR%\payloads" exit /b 1
copy /y payloads\hello.js "%OUT_DIR%\payloads\hello.js" >nul
if errorlevel 1 exit /b 1
copy /y payloads\banner.js "%OUT_DIR%\payloads\banner.js" >nul
if errorlevel 1 exit /b 1
if not exist "%OUT_DIR%\legacy" mkdir "%OUT_DIR%\legacy"
if errorlevel 1 exit /b 1
copy /y legacy\dc_cmd_script.js "%OUT_DIR%\legacy\dc_cmd_script.js" >nul
if errorlevel 1 exit /b 1
copy /y legacy\NOTICE.md "%OUT_DIR%\legacy\NOTICE.md" >nul
if errorlevel 1 exit /b 1
copy /y legacy\README.md "%OUT_DIR%\legacy\README.md" >nul
if errorlevel 1 exit /b 1
echo Native Windows GUI and clearly labeled mock GUI built.
"%GCC%" %FLAGS% "-ffile-prefix-map=%CD%=." -municode tests\gui_harness.c -o "%OUT_DIR%\gui-harness.exe" -luser32 -lgdi32
if errorlevel 1 exit /b 1
echo Exact-PID native GUI harness built.
exit /b 0
