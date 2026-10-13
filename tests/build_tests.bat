@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist out mkdir out
cl /nologo /O2 /W3 /EHsc /std:c++17 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX tallytest.cpp /Fo:out\ /Fe:out\tallytest.exe /link user32.lib kernel32.lib
if errorlevel 1 exit /b 1
if not exist out\steam mkdir out\steam
cl /nologo /O2 /W3 /EHsc /std:c++17 /MT fakesteam.cpp /Fo:out\ /LD /link /OUT:out\steam\steam_api64.dll /IMPLIB:out\steam\fakesteam.lib kernel32.lib
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc /std:c++17 /MT fakegame.cpp /Fo:out\ /Fe:out\fakegame.exe /link /MERGE:.patt=.text user32.lib kernel32.lib
exit /b %errorlevel%
