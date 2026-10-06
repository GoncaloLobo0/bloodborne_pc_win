@echo off
rem Game folder from fake-signed backup packages (what some PS4 dump tools write):
rem   windows\install-pkg.cmd game.pkg [update.pkg] [destination, default roms]
rem The game ends up in <destination>\CUSA03173 with the update copied over it.
call "%~dp0msys.cmd" bash tools/pkg_extract/install.sh %*
