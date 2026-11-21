@echo off
setlocal
set "E2STUDIO=C:\Renesas\RA\e2studio_v2025-12_fsp_v6.4.0\eclipse\e2studio.exe"
set "WORKSPACE=%~dp0."
start "" "%E2STUDIO%" -data "%WORKSPACE%"
endlocal
