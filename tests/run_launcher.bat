@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if not exist "%ROOT%\build\test" mkdir "%ROOT%\build\test"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
cl /nologo /O2 /MT /W4 /WX /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE "%~dp0launcher_resolver.cpp" /Fo"%ROOT%\build\test\launcher_resolver.obj" /Fe:"%ROOT%\build\test\launcher_resolver.exe" /link /SUBSYSTEM:CONSOLE user32.lib version.lib
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%ROOT%\build\test\launcher_resolver.exe"
exit /b %ERRORLEVEL%
