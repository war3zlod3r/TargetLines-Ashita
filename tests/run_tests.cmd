@echo off
setlocal
rem Builds and runs the offline unit tests (no Ashita SDK or game required).
rem Usage: tests\run_tests.cmd   (from the repository root or the tests folder)

set "ROOT=%~dp0.."
set "OUT=%ROOT%\out\tests"
if not exist "%OUT%" mkdir "%OUT%"

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Visual Studio with the C++ toolset was not found.
    exit /b 1
)

call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 -no_logo >nul 2>&1

cl /nologo /EHsc /W4 /std:c++20 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /I "%ROOT%\src" ^
    "%ROOT%\tests\test_main.cpp" "%ROOT%\src\action_packet.cpp" "%ROOT%\src\settings.cpp" ^
    /Fo"%OUT%\\" /Fe"%OUT%\targetlines_tests.exe"
if errorlevel 1 exit /b 1

"%OUT%\targetlines_tests.exe"
