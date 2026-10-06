@echo off
setlocal

set "ROOT=%~dp0.."
set "QT=%ROOT%\..\_ThirdParty\Qt\5.15.2\mingw81_64"
set "MINGW=%ROOT%\..\_ThirdParty\QtTools\Tools\mingw810_64"
set "WRAP=C:\Program Files\Faceform\Wrap 2025.10.8"
set "STAGE=%ROOT%\build\test"

if not exist "%MINGW%\bin\g++.exe" (
  echo [ERROR] MinGW 8.1 x64 toolchain not found: %MINGW%
  exit /b 1
)
if not exist "%QT%\lib\libQt5Widgets.a" (
  echo [ERROR] Qt 5.15.2 SDK not found: %QT%
  exit /b 1
)
if not exist "%WRAP%\Qt5Gui.dll" (
  echo [ERROR] Wrap 2025.10.8 runtime not found: %WRAP%
  exit /b 1
)
if not exist "%STAGE%" mkdir "%STAGE%"
if not exist "%STAGE%\translations" mkdir "%STAGE%\translations"
set "PATH=%WRAP%;%MINGW%\bin;%PATH%"

echo === Auditing translation dictionary ===
python "%ROOT%\scripts\audit_dict.py"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
python "%ROOT%\scripts\verify_wrap_qt_exports.py" "%WRAP%\Qt5Gui.dll"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Building isolated hook ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror -shared ^
  -DUNICODE -D_UNICODE -DQT_NO_DEBUG -DWRAP_CHINESE_TEST_HOOKS ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\source\hook.cpp" -o "%STAGE%\WrapChineseHook.dll" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core ^
  -luser32 -lshell32 -static-libgcc -static-libstdc++ ^
  -Wl,--out-implib,"%STAGE%\libWrapChineseHook.a" ^
  -Wl,--enable-auto-import -Wl,--nxcompat -Wl,--dynamicbase
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
copy /Y "%ROOT%\translations\dictionary_zh.json" "%STAGE%\translations\dictionary_zh.json" >nul

echo === Building core logic tests ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror ^
  -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" ^
  "%ROOT%\tests\core_logic_tests.cpp" -o "%STAGE%\core_logic_tests.exe" ^
  -L"%QT%\lib" -lQt5Gui -lQt5Core -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Running core logic tests ===
"%STAGE%\core_logic_tests.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Building association transaction tests ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror ^
  -DUNICODE -D_UNICODE ^
  "%ROOT%\tests\association_proxy_tests.cpp" -o "%STAGE%\association_proxy_tests.exe" ^
  -ladvapi32 -lshlwapi -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Running association transaction tests ===
"%STAGE%\association_proxy_tests.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Building display-boundary host ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\tests\widget_metrics.cpp" -o "%STAGE%\widget_metrics.exe" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core -luser32 -lshell32 ^
  -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=%WINDIR%\Fonts"
set "QT_PLUGIN_PATH=%WRAP%\plugins"
"%STAGE%\widget_metrics.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

g++ -std=c++17 -O0 -Wall -Wextra -Werror ^
  -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\tests\display_boundary_host.cpp" -o "%STAGE%\display_boundary_host.exe" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core -luser32 ^
  -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Running display-boundary regression ===
set "QT_QPA_PLATFORM=offscreen"
set "QT_QPA_FONTDIR=%WINDIR%\Fonts"
set "QT_PLUGIN_PATH=%WRAP%\plugins"
"%STAGE%\display_boundary_host.exe" "%STAGE%\WrapChineseHook.dll"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo All tests passed.
endlocal
