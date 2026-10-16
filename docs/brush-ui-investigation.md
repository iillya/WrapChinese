# 笔刷按钮图标/文字空白排查（2026-10-16）

用户报告的是部分电脑上按钮位置仍在、图标或文字空白。本机和用户提供的正常
截图没有复现该故障。以下区分已验证的补丁缺陷、已排查的路径和仍需故障现场
证据的部分，不能将合成测试通过当作其他用户的真实问题已被确认修复。

## 已验证并修正的问题

1. **全局样式被错误重建。** 旧实现读取当前 Style 的 `objectName()`，不能由
   `QStyleFactory` 创建时强制降级为 Fusion。宿主使用样式表包装器或自定义
   Style 时，原绘制和尺寸行为可能因此丢失。即使自定义 Style 沿用 `fusion`
   名称，也不等于能够用标准 Fusion 重建。修复后保留样式表包装器、无法重建
   的样式和不同 C++ 类型的样式；只对可验证重建的原生 Style 安装代理。
   原 Style key、原 Palette 和本插件代理指针另行记录，不接管原 Style 指针。
2. **自定义 Delegate 被误识别。** 仅比较 `metaObject()->className()` 无法
   识别没有 `Q_OBJECT` 的 `QStyledItemDelegate` 子类。旧版会替换其自定义
   `paint()`、`sizeHint()` 等行为。修复后检查实际 C++ 类型，保留所有子类，
   并保留默认 Delegate 上配置的 `QItemEditorFactory`。
3. **一个文字绘制重载的参数含义错误。** Qt 5 的
   `drawText(QPointF, QString, int, int)` 后两项是 `tf` 和
   `justificationPadding`，并非字符范围。旧实现翻译时改为 `0/-1`。现已原样
   透传；`TextBypassShaping` 路径保留英文，因为它不保证中文字体回退。

修改前的合成测试记录 `custom_delegate_preserved=0`、两种自定义样式
`preserved=0`、`stylesheet_style_preserved=0`；修改后相同检查全部为 1。
原始与修复后记录分别保存在 `build/brush-ui-before.log`、
`build/brush-ui-after.log`，完整回归保存在 `build/brush-ui-regression.log`。

## 与本机 Wrap 2025.11.14 笔刷的关系

只读静态分析确认，该版本 `Wrap::Brush::UI::ToolBarButton` 的 `sizeHint()`、
`minimumSizeHint()` 和 `paintEvent()` 使用 Qt 的 `QToolButton` 实现；
`BrushToolBar` 使用 QFrame 绘制。这使全局 Style、样式表、Palette、字体、
按钮显示模式和图标资源成为此次症状的直接检查对象。

该版本笔刷栏不是列表 Delegate 绘制，因此不能把第 2 项直接宣布为本机这一
版本笔刷栏空白的根因。它是此次检查确认的另一项会丢失自定义界面绘制的问题。

Qt5Widgets 在本机仅导入三个矩形 `drawText` 重载，不导入上述双整数点坐标
重载；第 3 项是确定的潜在兼容缺陷，也不能据此断言它触发了这次笔刷空白。
该重载的真实语义参考
[Qt 5.15.2 源码](https://github.com/qt/qtbase/blob/v5.15.2/src/gui/painting/qpainter.cpp#L5466)。

新增快捷键提示缓存 Hook 只在 `Wrap::WContextHelpHintBase` 及其子控件显示
私有中文图片，笔刷 ToolBarButton 不属于该范围。原文档和原图片始终保留。
安装器仅分发本插件 DLL、启动器和词典，不替换 Wrap 内置笔刷图片或 Qt DLL。

## 验证范围

全部检查使用本机 Wrap 随附的 Qt 5.15.3，在 `offscreen` 平台执行，没有启动
Wrap、控制桌面、安装补丁或修改用户工程。

- 100%、125%、150%、200% DPI，每种均覆盖 13 个“图标在上、文字在下”的
  QToolButton，确认图标像素、逻辑尺寸、滚动区域和英文源属性保留。
- 宿主样式表、自定义 Style、原生 Fusion、深色 Palette 和 12/18/26 点字体。
- 自定义 Delegate、编辑器工厂、故意隐藏的控件和中英切换。
- 绘制参数透传和 BypassShaping 原文降级。
- 原有 94 条完整提示、97 种分段文字、图片按键、中英切换、缓存预算与导出隔离。
- 应用全局样式表、跳过代理 Style 时，完整显示层与快捷键提示回归也全部通过。
- 主动采集生成诊断文件，包含引擎版本、样式决策等字段，不输出窗口标题、
  标签文字、Model 内容、节点实例名或工程内容。

这是兼容性证据，尚未重现其他用户的实际故障。尤其是“按钮图标或文字空白”
不能仅凭这一描述区分源图标为空、文字颜色融入背景、控件被裁剪、字体缺字、
状态禁用或某个样式绘制异常。

## 故障现场定位

1. 先记录对方实际 Wrap 版本、汉化包版本及问题截图；“只切换中/英”仍不恢复
   不能排除旧全局 Style 或 Delegate 的影响，因为语言开关不会卸载它们。
2. 若比较原版与补丁，需保存工作后分别通过原版 `Wrap.exe` 和中文启动器完整
   重启，并使用同一布局和工程。原版也异常时，优先区分资源、字体、布局与
   宿主自身状态；只有补丁启动异常时，再对比 Style 和绘制路径。
3. 在 1.0.2 的异常界面按 **Shift + ~**，会在采集文件旁生成
   `Wrap_window_diagnostics.json`。默认在桌面。它只读取显示状态并保存本地文件，
   不展开或改动笔刷界面，不发送文件到外部。

诊断重点：`engine_version` 与模块哈希是否匹配；是否真正创建笔刷控件；
`icon_null`、`caption_empty`、`tool_button_style`、前景/背景色、中文字体覆盖；
按钮与祖先尺寸、滚动区域和可见状态；资源目录是否含笔刷 PNG；以及实际 DPI。
有了故障电脑的这份快照，才能把上述兼容风险与具体用户的原因对应起来。
