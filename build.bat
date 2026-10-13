@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist out mkdir out
rem ui.inc: the page's script and markup (tables.inc is made by tools\gen_tables.py from the game's data)
python tools\gen_ui.py >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /W3 /EHsc /std:c++17 /MT /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   statspage.cpp /Fo:out\ /LD /link /OUT:out\statscompletiontracker.dll /IMPLIB:out\statscompletiontracker.lib user32.lib kernel32.lib
exit /b %errorlevel%
