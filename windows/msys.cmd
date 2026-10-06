@echo off
rem Runs a command in the repository root inside the private MSYS2 CLANG64 environment
rem (.toolchain\msys64), e.g.  windows\msys.cmd bash build.sh
setlocal
set "ROOT=%~dp0.."
set "MSYS=%ROOT%\.toolchain\msys64"
if not exist "%MSYS%\usr\bin\bash.exe" (
    echo MSYS2 is missing: run windows\setup.cmd first. 1>&2
    exit /b 1
)
set MSYSTEM=CLANG64
set MSYS2_PATH_TYPE=minimal
set CHERE_INVOKING=1
rem GTK/GLib caches and settings (fontconfig, recently used files) stay in .toolchain, not in
rem the user profile on C:.
set "XDG_CACHE_HOME=%ROOT%\.toolchain\xdg\cache"
set "XDG_CONFIG_HOME=%ROOT%\.toolchain\xdg\config"
set "XDG_DATA_HOME=%ROOT%\.toolchain\xdg\data"
set "XDG_STATE_HOME=%ROOT%\.toolchain\xdg\state"
pushd "%ROOT%"
if "%~1"=="" (
    "%MSYS%\usr\bin\bash.exe" -l
) else (
    "%MSYS%\usr\bin\bash.exe" -lc "%*"
)
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
