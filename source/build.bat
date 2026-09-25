@echo off
setlocal

set "ROOT=%~dp0.."
set "CPP=%ROOT%\source"
set "OUT=%ROOT%\build\out"
set "OBJ=%ROOT%\build\obj"
set "QT=%ROOT%\..\_ThirdParty\Qt\5.15.2\mingw81_64"
set "MINGW=%ROOT%\..\_ThirdParty\QtTools\Tools\mingw810_64"
if not defined WRAP_ROOT set "WRAP_ROOT=C:\Program Files\Faceform\Wrap 2025.10.8"

if not exist "%QT%\lib\libQt5Widgets.a" (
  echo [ERROR] Qt 5.15.2 MinGW SDK not found: %QT%
  exit /b 1
)
if not exist "%MINGW%\bin\g++.exe" (
  echo [ERROR] MinGW 8.1 x64 toolchain not found: %MINGW%
  exit /b 1
)
python "%ROOT%\scripts\verify_wrap_qt_exports.py" "%WRAP_ROOT%\Qt5Gui.dll"
if errorlevel 1 exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

set "PATH=%MINGW%\bin;%PATH%"

echo === Compiling Wrap Qt display hook ===
"%MINGW%\bin\g++.exe" -std=c++17 -O2 -Wall -Wextra -Werror -shared ^
  -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%CPP%\hook.cpp" -o "%OUT%\WrapChineseHook.dll" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core ^
  -luser32 -lshell32 -static-libgcc -static-libstdc++ ^
  -Wl,--out-implib,"%OBJ%\libWrapChineseHook.a" ^
  -Wl,--enable-auto-import -Wl,--nxcompat -Wl,--dynamicbase
if errorlevel 1 exit /b 1

echo === Compiling launcher ===
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [ERROR] Visual Studio 2022 C++ Build Tools not found.
  exit /b 1
)
call "%VCVARS%" >nul
rc /nologo /fo "%OBJ%\app_icon.res" "%ROOT%\icon\app_icon.rc"
if errorlevel 1 exit /b 1
cl /nologo /O2 /W4 /WX /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE ^
  "%CPP%\launcher.cpp" /Fo"%OBJ%\launcher.obj" ^
  "%OBJ%\app_icon.res" ^
  /Fe:"%OUT%\WrapChineseLauncher.exe" ^
  /link /SUBSYSTEM:WINDOWS user32.lib version.lib
if errorlevel 1 exit /b 1

if not exist "%OUT%\translations" mkdir "%OUT%\translations"
copy /Y "%ROOT%\translations\dictionary_zh.json" "%OUT%\translations\dictionary_zh.json" >nul

echo === Verifying runtime dependencies ===
"%MINGW%\bin\objdump.exe" -p "%OUT%\WrapChineseHook.dll" > "%OBJ%\hook-imports.txt"
findstr /I /C:"DLL Name: Qt5Core.dll" "%OBJ%\hook-imports.txt" >nul || exit /b 1
findstr /I /C:"DLL Name: Qt5Gui.dll" "%OBJ%\hook-imports.txt" >nul || exit /b 1
findstr /I /C:"DLL Name: Qt5Widgets.dll" "%OBJ%\hook-imports.txt" >nul || exit /b 1
findstr /C:"WrapChineseInitialize" "%OBJ%\hook-imports.txt" >nul || exit /b 1
findstr /C:"WrapChineseVersion" "%OBJ%\hook-imports.txt" >nul || exit /b 1

if /I "%~1"=="--runtime-only" goto done
call "%CPP%\build_inno.bat" --package-only
if errorlevel 1 exit /b 1

:done
echo Build complete:
echo   %OUT%\WrapChineseHook.dll
echo   %OUT%\WrapChineseLauncher.exe
endlocal
