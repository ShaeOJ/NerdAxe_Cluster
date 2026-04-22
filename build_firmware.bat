@echo off
set IDF_PATH=C:\Users\onawa\esp\esp-idf
set IDF_TOOLS_PATH=C:\Users\onawa\.espressif
set BOARD=NERDAXE
set LOGFILE=G:\Bitaxe Project\NerdAxe_Cluster\build_output.log

cd /d "G:\Bitaxe Project\NerdAxe_Cluster"

echo === Calling IDF export.bat === > "%LOGFILE%" 2>&1
call "%IDF_PATH%\export.bat" >> "%LOGFILE%" 2>&1

echo === IDF_PATH=%IDF_PATH% === >> "%LOGFILE%" 2>&1
echo === BOARD=%BOARD% === >> "%LOGFILE%" 2>&1
echo === Starting build === >> "%LOGFILE%" 2>&1

"C:\Users\onawa\.espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" build >> "%LOGFILE%" 2>&1

echo === Build finished with exit code %ERRORLEVEL% === >> "%LOGFILE%" 2>&1
