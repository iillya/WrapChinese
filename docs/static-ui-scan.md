# Wrap 2025.10.8 静态 UI 遗漏扫描

## 扫描范围

- `C:\Program Files\Faceform\Wrap 2025.10.8\Wrap.exe`
- `C:\Program Files\Faceform\Wrap 2025.10.8\WrapCmd.exe`
- `C:\Program Files\Faceform\Wrap 2025.10.8\WrapServer.exe`
- `C:\Program Files\Faceform\Wrap 2025.10.8\FbxSdkCWrapper.dll`
- `C:\Program Files\Faceform\Wrap 2025.10.8\Gallery\standardGallery.json`
- Gallery 内 59 个随附 `.wrap` 示例工程的可见节点名称

Qt、FFmpeg、OpenCV、Python 标准库及其他第三方依赖不属于 Wrap 自身 UI，未作为
翻译来源。`WrapCmd.exe` 和 `WrapServer.exe` 用作反向噪声样本，以排除桌面端与
命令行/服务端共有的底层诊断字符串。

## 结果

| 项目 | 数量 |
|---|---:|
| `Wrap.exe` 原始英文候选 | 20,822 |
| `WrapCmd.exe` 原始英文候选 | 15,479 |
| `WrapServer.exe` 原始英文候选 | 15,577 |
| 当前正式词典 | 608 |
| 运行时已确认 UI 锚点 | 146 |
| 静态扫描后保留的遗漏 UI 候选 | 559 |
| 示例工程中遗漏的可见资源名 | 32 |
| UI 与资源名候选合计 | **591** |
| Gallery 可见名称遗漏 | **0** |
| Gallery 节点类型遗漏 | **0** |
| DLL 返回界面待复核候选 | 78 |

591 条候选包括桌面版独有的高分字符串、已确认 UI 邻近资源、节点类型注册表
以及随附示例工程中的可见节点资源名。资源名只读取节点显示名称，不收集文件路径、
参数键或面部标记点编号。完整文本、二进制偏移、出现次数和入选原因保存在：

`translations/inventory/wrap_static_missing_ui.json`

## DLL 结论

安装目录根部的 DLL 绝大多数是 Qt、FFmpeg、OpenCV、Intel MKL 等第三方运行库。
唯一需要单独关注的是 `FbxSdkCWrapper.dll`。该 DLL 不链接 Qt，因此不会直接绘制
界面；但 Wrap 可能把它返回的 FBX 文件格式名、预设名和导入导出错误显示在窗口中。

扫描得到 9,476 条原始 SDK/解析器字符串。剔除代码、XML/Alembic 内部错误和普通
诊断后，保留 78 条“可能由 Wrap 显示”的低优先级复核项：

`translations/inventory/fbxsdkcwrapper_ui_surface_review.json`

`Wrap.exe` 的 PE 资源区仅包含图标和版本信息，没有遗漏的界面字符串。

## 已剔除内容

- 文件绝对路径和工程数据值
- C++ 符号、内部类名及序列化字段
- 着色器代码与 OpenGL 调试输出
- 布局文件解析诊断
- Qt、Python、编解码器及其他第三方库字符串
- 已由正式词典精确项、归一化项或动态模板覆盖的文本

## 主要遗漏类别

- 菜单和操作：如 `Auto Save`、`Paste Nodes Under Cursor`、`Export Annotation`
- 对话框和状态：如 `Project Has Unsaved Modifications`、`Failed to Compute`
- 参数和面板：如 `Camera Generator`、`Default Color Space`、`Projection Mask`
- 节点类型：如 `AlignGeomToCameras`、`AnatomicalRetarget`、`PathTracedRender`
- 相机标定、关键帧、图库、文件选择及视口提示

静态候选不会自动写入正式词典；翻译前仍需逐条检查占位符、大小写变体和同义项。
