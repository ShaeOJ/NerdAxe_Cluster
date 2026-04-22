@echo off
cd /d C:\Users\onawa\esp\esp-idf
call export.bat
if %errorlevel% neq 0 (
    echo ERROR: export.bat failed
    exit /b 1
)
cd /d "G:\Bitaxe Project\NerdAxe_Cluster"
set PYTHONUTF8=1
set BOARD=NERDAXE
echo === Building NerdAxe ===
idf.py build
