@echo off
rem Builds out\bb-probe.exe in the private build environment (windows\setup.cmd first).
rem Arguments go to build.sh, e.g.  windows\build.cmd --test
call "%~dp0msys.cmd" bash build.sh %*
