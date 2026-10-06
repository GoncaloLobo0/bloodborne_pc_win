@echo off
rem Starts the GTK launcher (game folder, settings, Start) in the private environment.
call "%~dp0msys.cmd" python3 launcher/bbport_launcher.py %*
