# WrapChinese

WrapChinese 是面向 Windows x64 版 **Faceform Wrap（Qt 5）** 的中文显示层补丁。
它通过独立启动器在 Wrap 启动前加载汉化 DLL，不修改 `Wrap.exe`、Qt DLL、
`.wrap` 工程或用户输入的数据。

## 特性

- 菜单、面板、节点、参数、对话框和状态信息按 JSON 词典翻译。
- 补齐 94 条上下文快捷键/鼠标提示，覆盖笔刷、点选、多边形、样条、相机、视口与时间轴；按键图示和按键名保持原样。
- 在界面控件的 `QPainter` 绘制、Qt 样式、列表显示委托和普通富文本标签绘制中替换译文；宿主控件原文、
  模型数据和内部查找标识保持原样；纯显示的字体、宽度和缓存可以随译文变化。
- 按 **F3** 或菜单栏的“中/英”切换显示语言；快捷键在按下时触发，长按不重复。
- 在“中/英”上点右键可修改切换键。
- 按 **Shift+反引号（~）** 可把当前界面的未翻译文本采集到桌面 JSON，供人工审核。
- 1.0.2 保留无法重建的宿主样式和自定义绘制委托；主动采集同时生成 `Wrap_window_diagnostics.json`，便于排查按钮空白、DPI、布局与资源问题。详见[笔刷界面排查](docs/brush-ui-investigation.md)。
- 1.0.2 收紧动态模板和输入显示边界，移出发行版中的开发自动化，减少重复扫描与富文本布局开销。详见[代码审核与性能验证](docs/code-audit-20261016.md)。
- 安装器只在 Wrap 目录下新增 `ChineseLauncher`，卸载时不删除 Wrap 和工程。

## 支持版本

- Windows x64 版 Faceform Wrap
- Wrap 随附的完整 Qt 5 MinGW x64 运行时

安装器不再核对具体 Wrap 版本或程序哈希，而是验证 `Wrap.exe` 架构以及 Qt
Core/Gui/Widgets 的架构、ABI 大类和组件一致性。DLL 加载时继续验证实际导入
能力，非核心绘制能力缺失时独立降级。当前实测版本为 Wrap 2025.10.8。
2026-10-16 新增对本机 Wrap 2025.11.14 的静态词条核对及随附 Qt 5.15.3
运行时的无界面显示回归；本轮未启动 Wrap 做真实界面验收。

## 安装与使用

1. 保存工程并完全退出 Wrap。
2. 运行 `dist\WrapChineseInstaller.exe`，选择包含 `Wrap.exe` 的目录，默认是：

   ```text
   C:\Program Files\Faceform\Wrap 2025.10.8
   ```

3. 从桌面或开始菜单的“Wrap 中文版”启动。

安装后的结构为：

```text
Wrap <版本>\
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
词条时，建议另建 `zz_custom_zh.json`，这样更新和卸载不会把自定义文件当成安装器
负载处理。词库按文件名排序，后加载的词条覆盖先加载的词条；`custom_zh.json`
排在默认词库之前，不能用于覆盖默认译文。所有 `*_zh.json` 使用相同格式：

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

保存词库后重启 Wrap 生效。翻译只用于显示，不写回节点名、路径、
参数值或工程文件。带框线、缩进、可选中、链接或图片的富文本标签会保留宿主原有绘制；
Qt Widgets 内部的字体宽度、边界、尺寸和省略文本调用按译文计算，切换语言时刷新布局缓存；当前实现不拦截宿主通用测量，这属于实现范围，不是禁止纯显示测量或官方 `QTranslator`。标签、按钮、复选框、分组框、横竖标签页、下拉框、菜单和列表的尺寸已通过实际 Wrap Qt 运行时回归测试。

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

快捷键支持 F1–F12，或 `Ctrl` / `Shift` / `Alt` 加普通按键。普通字母、数字和
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

`WRAP_ROOT` 可指定用于构建和测试的 Wrap 安装目录，默认使用
`C:\Program Files\Faceform\Wrap 2025.11.14`。只验证显示层且跳过工程关联注册表
测试时运行 `tests\run.bat --display-only`。新增快捷键回归逐条比较 94 条提示、
分段 Markdown 缓存及整组笔刷提示，验证图示保留、语言切换、旧缓存复用和离屏
导出保护；测试使用 `offscreen` 平台，不操作桌面。

快捷键提示使用独立显示缓存：宿主 `QTextDocument` 和缓存图片始终为英文，
只在 `Wrap::WContextHelpHintBase` 提示控件绘制时换用插件自己的中文图片。
按键文字、图片资源与宿主文档保持原样。中文图片缓存上限为 32 MiB，旧图片
淘汰后仍保留有界绘制配方；切回中文时可重新生成。源码与验证记录见
[快捷键显示修复](docs/shortcut-hints.md)。

`tests\run.bat` 不启动正式 Wrap 窗口；它使用 Wrap 的 Qt 5.15.3 运行时验证
占位符、配置、快捷键去重、富文本标签动态绘制、标签页横向和纵向尺寸及语言切换恢复、中文控件尺寸适配、控件样式缓存翻译及无界面上下文的离屏输出不变、
采集保全、设置弹窗，以及工程关联的安装、回滚和卸载事务。真实启动链路可另运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\smoke_test.ps1
```

该测试需要先运行 `source\build.bat --developer-tools`，使用独立的开发输出目录。
它会隐藏启动一次已验证的 Wrap，完成采集后正常退出；发现已有 Wrap 会话时会
拒绝运行。普通发行版不包含自动采集/退出入口。

## 节点参数运行时采集

以下命令从 `WrapCmd --nodes` 读取当前版本的正式节点清单，只启动一个空工程，
然后在同一节点图中依次展开添加菜单并选择所有节点。它不移动系统鼠标，也不
手工拼接易随版本变化的节点 JSON：

```powershell
source\build.bat --developer-tools
python scripts\generate_node_probe_projects.py
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run_node_probe.ps1
python scripts\aggregate_node_probe.py
```

每个节点创建后都会检查场景项确实增加，再采集当前参数面板。汇总结果写入
`translations\inventory\node_parameter_runtime_probe.json`；已有 Wrap 会话时脚本
会拒绝启动，避免碰到正在编辑的工程。
