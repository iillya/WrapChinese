# Wrap 中文补丁安装与卸载

## 安装或更新

1. 保存工程，确认任务管理器中没有 `Wrap.exe`。
2. 运行 `WrapChineseInstaller.exe`。
3. 选择包含 `Wrap.exe` 的目录。默认目录为
   `C:\Program Files\Faceform\Wrap 2025.10.8`。
4. 安装后从“Wrap 中文版”快捷方式启动；直接运行原 `Wrap.exe` 不会加载补丁。

安装器不替换 Wrap 原文件，只管理所选目录下的 `ChineseLauncher`。更新时会先
为已存在的补丁文件创建恢复快照；文件被 Wrap 占用时不会强制结束进程。

## 工程关联

可选的工程关联仅覆盖当前用户的 `WrapProjectFile\shell\open\command`，并先
保存其原始值。它保留官方命令参数及注册表值类型，不修改扩展名默认 ProgID，
也不写受保护的 `UserChoice`。

如果系统中的 `.wrap` 当前由其他程序打开，Windows 的默认应用选择仍然优先。
不需要双击工程启动汉化时，可以取消工程关联选项。

## 卸载

保存工程并完全退出 Wrap，然后在 Windows“设置 → 应用 → 已安装的应用”中
卸载“Wrap 中文补丁”。卸载只恢复仍由本补丁拥有的打开命令；用户或其他程序
后来改过的值会保留。

Wrap、`.wrap` 工程和人工添加的文件不会被删除。补丁生成的快捷方式、安装记录、
升级快照和快捷键配置会被清理。安装器不会递归删除 `ChineseLauncher`；用户
另行添加的 `custom_zh.json` 等文件会保留，并可能使空壳目录继续存在。

## 快捷键与采集配置

统一配置位于 `%LOCALAPPDATA%\WrapChinese\WrapChinese.ini`。快捷键支持
F1–F12，或 `Ctrl` / `Alt` / `Win` 加普通按键；按下即触发，按住不会重复。
将 `Capture/enabled` 改为 `false` 可关闭 Shift+反引号（~）运行时采集。卸载器只
删除这一个插件配置文件，不会删除同目录外的用户数据。

## 故障排查

- 启动器提示缺少 DLL：确认安装目录中仍有 Wrap 自带的 Qt DLL，并重新运行安装器。
- 界面仍为英文：关闭全部 Wrap 进程，再从“Wrap 中文版”启动。
- 中文被裁切：确认使用的是包含字体宽度 Hook 的同一版本 DLL，并重启 Wrap。
- 安装器提示文件占用：处理 Wrap 的保存确认框并正常退出，不要强制覆盖正在加载的 DLL。
