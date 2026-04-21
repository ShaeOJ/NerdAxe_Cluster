@echo off
cd /d C:\Users\onawa\esp\esp-idf
call export.bat
if %errorlevel% neq 0 (
    echo ERROR: export.bat failed
    exit /b 1
)
cd /d "G:\Bitaxe Project\NerdAxe_Cluster"
set BOARD=NERDQAXEPLUS2
echo === Cleaning old build cache ===
idf.py fullclean
echo === Building NerdQAxe++ ===
idf.py build
echo BUILD_EXIT_CODE=%errorlevel%
pause
