@echo off
setlocal
cd /d "%~dp0\.."
set "TEST_OUT=%CD%\bin"
if defined ORB_OUT_DIR for %%D in ("%ORB_OUT_DIR%") do set "TEST_OUT=%%~fD"
if /i not "%TEST_OUT%"=="%CD%\bin" (
  echo ERROR: test.bat requires the default bin output. Use alternate ORB_OUT_DIR for build and packaging only.
  exit /b 1
)
call scripts\build.bat
if errorlevel 1 exit /b 1
bin\process-test.exe
if errorlevel 1 exit /b 1
set "EVIDENCE=%TEMP%\24510_0rb-tests-%RANDOM%"
powershell.exe -NoProfile -File tests\foundation.ps1 -EvidenceDirectory "%EVIDENCE%"
set "RC=%ERRORLEVEL%"
echo Test evidence: "%EVIDENCE%"
exit /b %RC%
