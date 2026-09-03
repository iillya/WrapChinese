# WrapChinese

WrapChinese 是面向 Windows 版 **Faceform Wrap 2025.10.8** 的中文显示层补丁。
它通过独立启动器在 Wrap 启动前加载汉化 DLL，不修改 `Wrap.exe`、Qt DLL、
`.wrap` 工程或用户输入的数据。

## 特性

- 菜单、面板、节点、参数、对话框和状态信息按 JSON 词典翻译。
- 同时 Hook `QPainter`、`QFontMetrics` 与 `QFontMetricsF`，绘制文字和布局测量
  使用同一份中文，避免中文截断、按钮宽度仍按英文计算等问题。
- 按 **F3** 或菜单栏的“中/英”切换显示语言；快捷键在按下时触发，长按不重复。
- 在“中/英”上点右键可修改切换键。
- 按 **Shift+反引号（~）** 可把当前界面的未翻译文本采集到桌面 JSON，供人工审核。
- 安装器只在 Wrap 目录下新增 `ChineseLauncher`，卸载时不删除 Wrap 和工程。

## 支持版本

- Faceform Wrap 2025.10.8 x64
- Wrap 随附的 Qt 5.15.3 MinGW x64 运行时

安装器会验证 `Wrap.exe` 的已知构建指纹、x64 体系结构以及 Qt
Core/Gui/Widgets 的精确版本。不满足条件时不会写入补丁。其他 Wrap 版本需要
重新验证导出符号和实际界面，不能直接视为已支持。

## 安装与使用

1. 保存工程并完全退出 Wrap。
2. 运行 `dist\WrapChineseInstaller.exe`，选择包含 `Wrap.exe` 的目录，默认是：

   ```text
   C:\Program Files\Faceform\Wrap 2025.10.8
   ```

3. 从桌面或开始菜单的“Wrap 中文版”启动。

安装后的结构为：

```text
Wrap 2025.10.8\
|-- Wrap.exe
`-- ChineseLauncher\
    |-- WrapChineseLauncher.exe
    |-- WrapChineseHook.dll
    `-- translations\
        `-- dictionary_zh.json
```

若安装时勾选工程关联，安装器只代理系统中已验证的官方
`WrapProjectFile` 打开命令，不写 `UserChoice`，也不强行改变默认应用。
完整安装、更新和卸载说明见 [安装指南](docs/installer.md)。

## 自定义词库

默认词库位于 `ChineseLauncher\translations\dictionary_zh.json`。需要追加或覆盖
词条时，建议另建 `custom_zh.json`，这样更新和卸载不会把自定义文件当成安装器
负载处理。所有 `*_zh.json` 使用相同格式：

```json
{
  "$schema": "sp-translation-v1",
  "language": "zh-CN",
  "translations": {
    "File": "文件",
    "Geometry": "几何体"
  }
}
```

保存词库后重启 Wrap 生效。翻译只用于显示和文字测量，不写回节点名、路径、
参数值或工程文件。

## 配置文件

首次启动会在 `%LOCALAPPDATA%\WrapChinese\WrapChinese.ini` 写入统一配置：

```ini
[Plugin]
version=1

[Hotkey]
shortcut=F3

[Capture]
enabled=true
```

快捷键支持 F1–F12，或 `Ctrl` / `Alt` / `Win` 加普通按键。普通字母、数字和
导航键不能在无修饰键时占用，避免影响 Wrap 输入。右键菜单设置会原子保存该
文件；`Capture/enabled=false` 可关闭 Shift+反引号（~）采集入口。

## 从源码构建

开发目录与 SpeedTreeChinese 的职责划分保持一致：

```text
source\                 Hook、启动器与 Inno Setup 源码
translations\           正式词典
translations\inventory\ 静态及运行时词条库存
scripts\                提取、审计与兼容性测试脚本
tests\                  MinGW ABI 和字体测量回归测试
docs\                   安装说明
icon\                   Wrap 启动器/安装器图标
build\                  本机构建中间文件（Git 忽略）
dist\                   最终安装器（纳入版本管理）
```

本机准备 Qt 5.15.2 MinGW 8.1 x64 SDK、Visual Studio 2022 C++ Build Tools 和
Inno Setup 7.1 后运行：

```bat
source\build.bat
tests\run.bat
```

`tests\run.bat` 不启动正式 Wrap 窗口；它使用 Wrap 的 Qt 5.15.3 运行时验证
占位符、配置、快捷键去重、中文宽度、包围框、尺寸、紧致包围框、省略文本、
采集保全、设置弹窗，以及工程关联的安装、回滚和卸载事务。真实启动链路可另运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\smoke_test.ps1
```

该测试会隐藏启动一次已验证的 Wrap，完成采集后正常退出；发现已有 Wrap 会话时
会拒绝运行。
