@echo off
setlocal

set "ROOT=%~dp0.."
set "QT=%ROOT%\..\_ThirdParty\Qt\5.15.2\mingw81_64"
set "MINGW=%ROOT%\..\_ThirdParty\QtTools\Tools\mingw810_64"
if not defined WRAP_ROOT set "WRAP_ROOT=C:\Program Files\Faceform\Wrap 2025.11.14"
set "WRAP=%WRAP_ROOT%"
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
  echo [ERROR] Wrap Qt runtime not found: %WRAP%
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
python "%ROOT%\scripts\audit_shortcut_hints.py" "%WRAP%\Wrap.exe"
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

if /I "%~1"=="--display-only" goto display
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

:display
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

echo === Building cached shortcut-hint display host ===
"%QT%\bin\moc.exe" "%ROOT%\tests\shortcut_hint_display.cpp" -o "%STAGE%\shortcut_hint_display.moc"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
g++ -std=c++17 -O0 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%STAGE%" -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\tests\shortcut_hint_display.cpp" -o "%STAGE%\shortcut_hint_display.exe" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core -luser32 ^
  -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo === Running cached shortcut-hint display regression ===
set "WRAP_CHINESE_CONFIG_PATH=%STAGE%\shortcut_hint_test.ini"
"%STAGE%\shortcut_hint_display.exe" "%STAGE%\WrapChineseHook.dll" "%ROOT%\translations\review\shortcut_hints_20261016_zh.review.json"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Building brush UI compatibility regression ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\tests\brush_ui_compatibility.cpp" -o "%STAGE%\brush_ui_compatibility.exe" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core -luser32 -lshell32 ^
  -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo === Running brush UI DPI matrix ===
set "QT_SCALE_FACTOR=1"
"%STAGE%\brush_ui_compatibility.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "QT_SCALE_FACTOR=1.25"
"%STAGE%\brush_ui_compatibility.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "QT_SCALE_FACTOR=1.5"
"%STAGE%\brush_ui_compatibility.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "QT_SCALE_FACTOR=2"
"%STAGE%\brush_ui_compatibility.exe"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo === Running host stylesheet display regressions ===
set "QT_SCALE_FACTOR=1"
set "WRAP_CHINESE_TEST_STYLESHEET=1"
"%STAGE%\display_boundary_host.exe" "%STAGE%\WrapChineseHook.dll"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
"%STAGE%\shortcut_hint_display.exe" "%STAGE%\WrapChineseHook.dll" "%ROOT%\translations\review\shortcut_hints_20261016_zh.review.json"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
set "WRAP_CHINESE_TEST_STYLESHEET="

echo === Building audit regression ===
g++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DQT_NO_DEBUG ^
  -I"%QT%\include" -I"%QT%\include\QtCore" -I"%QT%\include\QtGui" -I"%QT%\include\QtWidgets" ^
  "%ROOT%\tests\audit_runtime.cpp" -o "%STAGE%\audit_runtime.exe" ^
  -L"%QT%\lib" -lQt5Widgets -lQt5Gui -lQt5Core -luser32 -lshell32 -lversion ^
  -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1
echo === Running audit regression ===
"%STAGE%\audit_runtime.exe" "%ROOT%"
if errorlevel 1 exit /b 1
if not errorlevel 0 exit /b 1

echo All tests passed.
endlocal
