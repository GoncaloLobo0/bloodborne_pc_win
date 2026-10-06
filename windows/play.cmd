@echo off
rem Starts the game without the launcher.
rem   windows\play.cmd                     the launcher's saved settings
rem   windows\play.cmd D:\path\CUSA03173   that game folder (other settings from bbport.ini)
setlocal
if "%~1"=="" (
    call "%~dp0msys.cmd" python3 launcher/bbport_launcher.py --play
) else (
    set "BB_GAME_DIR=%~f1"
    call "%~dp0msys.cmd" bash run.sh
)
