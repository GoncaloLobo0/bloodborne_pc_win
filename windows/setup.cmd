@echo off
rem Sets up the build environment of the Windows port in .toolchain\ next to this repository:
rem a private MSYS2 (CLANG64) with the compiler and libraries. Nothing is installed system-wide.
setlocal
set "ROOT=%~dp0.."
set "TOOLCHAIN=%ROOT%\.toolchain"
set "MSYS=%TOOLCHAIN%\msys64"
if not exist "%TOOLCHAIN%" mkdir "%TOOLCHAIN%"
if not exist "%MSYS%\usr\bin\bash.exe" (
    echo Downloading MSYS2 into %TOOLCHAIN% ...
    curl.exe -L -o "%TOOLCHAIN%\msys2-base.sfx.exe" https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe || goto :error
    "%TOOLCHAIN%\msys2-base.sfx.exe" -y -o"%TOOLCHAIN%\\" || goto :error
    del "%TOOLCHAIN%\msys2-base.sfx.exe"
    set MSYS2_PATH_TYPE=minimal
    "%MSYS%\usr\bin\bash.exe" -lc "exit" || goto :error
    "%MSYS%\usr\bin\bash.exe" -lc "pacman -Syuu --noconfirm" || goto :error
    "%MSYS%\usr\bin\bash.exe" -lc "pacman -Syuu --noconfirm" || goto :error
)
call "%~dp0msys.cmd" bash windows/setup-toolchain.sh || goto :error
echo.
echo Done. Build with windows\build.cmd
exit /b 0
:error
echo Setup failed.
exit /b 1
