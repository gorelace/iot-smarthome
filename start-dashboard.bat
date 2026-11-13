@echo off
title Smart Home Dashboard - server (dong cua so nay de tat)
cd /d "%~dp0dashboard"
set "PORT=8000"
set "URL=http://localhost:%PORT%/index.html"

REM ---- 1. Tim Python that (bo qua ban gia "python" cua Microsoft Store)
set "PY="
py -3 --version >nul 2>nul && set "PY=py -3"
if not defined PY python --version >nul 2>nul && set "PY=python"
if not defined PY for /d %%D in ("%LOCALAPPDATA%\Programs\Python\Python3*") do if exist "%%D\python.exe" set PY="%%D\python.exe"
if not defined PY (
  echo [LOI] Khong tim thay Python. Cai tu https://www.python.org/downloads/ ^(tick "Add python.exe to PATH"^)
  pause
  exit /b 1
)
echo Python: %PY%

REM ---- 2. Chon trinh duyet cai duoc app: Chrome neu co, khong thi Edge
set "BROWSER=msedge"
if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" set "BROWSER=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if exist "%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe" set "BROWSER=%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"

REM ---- 3. Cong 8000 da bi chiem? (thuong la server cu van dang chay)
netstat -ano | findstr /c:":%PORT% " | findstr LISTENING >nul
if not errorlevel 1 (
  echo [!] Cong %PORT% dang duoc dung - co the server cu van chay. Mo trinh duyet luon...
  start "" "%BROWSER%" "%URL%"
  pause
  exit /b 0
)

REM ---- 4. Doi server san sang roi moi mo trinh duyet
start "" /b powershell -NoProfile -Command "for($i=0;$i -lt 30;$i++){try{(New-Object Net.Sockets.TcpClient('127.0.0.1',%PORT%)).Close();break}catch{Start-Sleep -Milliseconds 300}}; Start-Process -FilePath '%BROWSER%' -ArgumentList '%URL%'"

echo.
echo Dashboard: %URL%
echo Lan dau: bam icon "Cai dat ung dung" tren thanh dia chi de cai thanh app.
echo Dong cua so nay (hoac Ctrl+C) de tat server.
echo.
%PY% -m http.server %PORT% --bind 127.0.0.1
echo.
echo Server da dung. Neu co loi o tren, chup man hinh cua so nay gui lai nhe.
pause
