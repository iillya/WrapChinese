#!/usr/bin/env python3
"""Promote reviewed runtime node UI strings into the release dictionary."""

from __future__ import annotations

import json
import re
from pathlib import Path

from audit_dict import normalized_key


ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "translations" / "inventory" / "node_parameter_runtime_probe.json"
DICTIONARY = ROOT / "translations" / "dictionary_zh.json"


UI_TRANSLATIONS = {
    "+ Material": "＋材质",
    "+ Mirror": "＋镜像",
    "+ Mirror: Alt+Shift+M": "＋镜像：Alt+Shift+M",
    "+ Polygroup": "＋多边形组",
    "+ UDIM": "添加 UDIM",
    "- Material": "－材质",
    "- Polygroup": "－多边形组",
    "- UDIM": "移除 UDIM",
    "16 Bit Floating": "16 位浮点",
    "16 Bit Integer": "16 位整数",
    "2D Reference Closing Falloff": "二维参考闭合衰减",
    "2D Reference Closing Threshold": "二维参考闭合阈值",
    "2D Reference Distance (cm)": "二维参考距离（厘米）",
    "2D Reference Falloff (cm)": "二维参考衰减（厘米）",
    "32 Bit Floating": "32 位浮点",
    "32 Bit Integer": "32 位整数",
    "3D-point": "三维点",
    "3D-Points": "三维点",
    "8 Bit Integer": "8 位整数",
    "90 Clockwise": "顺时针 90°",
    "90 Counterclockwise": "逆时针 90°",
    "Accumulation": "累积",
    "Alpha A": "Alpha A",
    "Alpha B": "Alpha B",
    "Alphabetical Order": "按字母顺序",
    "Ambient Occlusion": "环境光遮蔽",
    "AmbientOcclusion": "环境光遮蔽",
    "Apply Rotation": "应用旋转",
    "Apply Scale": "应用缩放",
    "Apply Self Transform": "应用自身变换",
    "Apply Translation": "应用平移",
    "Backward": "向后",
    "Base Name": "基础名称",
    "Blue A": "蓝色 A",
    "Blue B": "蓝色 B",
    "Both": "两者",
    "Brightness": "亮度",
    "Clamp by Low and High": "按上下限钳制",
    "Clear All": "全部清除",
    "Clear Positions": "清除位置",
    "Clone": "克隆",
    "CloneProject": "投影克隆",
    "Closest Point on Surface": "表面最近点",
    "Coef. K1": "系数 K1",
    "Coef. K2": "系数 K2",
    "Coef. K3": "系数 K3",
    "Coef. P1": "系数 P1",
    "Coef. P2": "系数 P2",
    "Color Space": "色彩空间",
    "Combine with Self Transform": "与自身变换合并",
    "Contrast": "对比度",
    "Distance (cm)": "距离（厘米）",
    "Distortion": "畸变",
    "Drop Alpha": "丢弃 Alpha",
    "Enable Transparency": "启用透明度",
    "Erase": "擦除",
    "EXR Compression": "EXR 压缩",
    "Extrapolate": "外推",
    "FBX Type": "FBX 类型",
    "Field of View": "视场角",
    "Forward": "向前",
    "Gain": "增益",
    "Gamma": "伽马",
    "Green A": "绿色 A",
    "Green B": "绿色 B",
    "Grow": "扩展",
    "Grow: Alt+G": "扩展：Alt+G",
    "Hide Selected: Alt+H": "隐藏所选：Alt+H",
    "High": "高",
    "Hue A": "色相 A",
    "Hue B": "色相 B",
    "If Has Duplicate Names": "存在重名时",
    "If No Pair": "没有配对时",
    "Ignore Missing UV": "忽略缺失的 UV",
    "import transform": "导入变换",
    "Index": "索引",
    "Inflate": "膨胀",
    "Invert Source Transform": "反转源变换",
    "Invert: Alt+I": "反转：Alt+I",
    "JPEG Quality": "JPEG 质量",
    "Lift": "提升",
    "Lightness A": "明度 A",
    "Lightness B": "明度 B",
    "Limit Dist": "限制距离",
    "Lock Cameras": "锁定相机",
    "Loop: Alt+L": "循环：Alt+L",
    "Low": "低",
    "Luminosity": "明度合成",
    "Match by": "匹配依据",
    "Matrix": "矩阵",
    "Max Angle": "最大角度",
    "Max Clamp": "钳制上限",
    "Max Dist": "最大距离",
    "Maximum": "最大值",
    "Midtones": "中间调",
    "Min Clamp": "钳制下限",
    "Minimum": "最小值",
    "Mirror: Alt+M": "镜像：Alt+M",
    "MirrorClone": "镜像克隆",
    "Mirrored": "已镜像",
    "Missing input Vertex Mask": "缺少输入“顶点遮罩”",
    "Missing Left Geometry": "缺少左侧几何体",
    "Move": "移动",
    "MoveProj": "投影移动",
    "MoveSurface": "表面移动",
    "Named 3D-Points": "命名三维点",
    "Named Point On Triangle": "三角形上的命名点",
    "Named Splines of 3D-Points": "命名三维点样条",
    "Named Splines of Points on Triangles": "命名三角形点样条",
    "Neighbours from": "邻域来源",
    "No Mask": "无遮罩",
    "Non-Occluded Value": "未遮挡值",
    "Non-Selected": "未选中",
    "Normalize by number of samples": "按采样数归一化",
    "Occluded Value": "遮挡值",
    "Offset (cm)": "偏移（厘米）",
    "Params": "参数",
    "PNG Quality": "PNG 质量",
    "Polygon Edges": "多边形边",
    "Post-Divide": "后除法",
    "Predivide and Postmultiply": "预除并后乘",
    "Preserve Volume": "保持体积",
    "Principal Point X": "主点 X",
    "Principal Point Y": "主点 Y",
    "Project": "工程",
    "Push-Pull": "推拉",
    "Ray Tracing": "光线追踪",
    "RC Internal/External Camera Parameters": "RC 内部/外部相机参数",
    "Red A": "红色 A",
    "Red B": "红色 B",
    "Relax": "松弛",
    "RelaxProj": "投影松弛",
    "RelaxSurface": "表面松弛",
    "Rename All Splines": "重命名所有样条",
    "Reverse": "反向",
    "Samples": "采样数",
    "Saturation A": "饱和度 A",
    "Saturation B": "饱和度 B",
    "Scale, Rotation(Euler Angles), Translation": "缩放、旋转（欧拉角）、平移",
    "Scale, Rotation(Quaternion), Translation": "缩放、旋转（四元数）、平移",
    "Script": "脚本",
    "Sculpt": "雕刻",
    "Seamless Fitting": "无缝拟合",
    "Select if at least one sample": "至少一个采样命中时选择",
    "Selection Threshold": "选择阈值",
    "Set Positions": "设置位置",
    "Show Excluded Polygons": "显示排除的多边形",
    "Shrink: Alt+S": "收缩：Alt+S",
    "Single Spline as Control 3D-Points": "单条样条作为控制三维点",
    "Single Spline as Control Points on Triangles": "单条样条作为三角形上的控制点",
    "Source Vertex Mask": "源顶点遮罩",
    "Splines as Named Control 3D-Points": "样条作为命名控制三维点",
    "Splines as Named Control Points on Triangles": "样条作为三角形上的命名控制点",
    "Splines of 3D-Points": "三维点样条",
    "Splines of Points on Triangles": "三角形点样条",
    "Surface Color": "表面颜色",
    "Swap Splines": "交换样条",
    "Swap Up": "向上交换",
    "Target Vertex Mask": "目标顶点遮罩",
    "TIFF Compression": "TIFF 压缩",
    "Topological Mirror": "拓扑镜像",
    "Trace Direction": "追踪方向",
    "Triangle Edges": "三角形边",
    "Triangle Fan": "三角扇",
    "Triangle Fan with Extra Point": "带附加点的三角扇",
    "Triangle Strip": "三角带",
    "Unchanged": "保持不变",
    "Unhide all": "全部取消隐藏",
    "Unhide all: Alt+U": "全部取消隐藏：Alt+U",
    "Use Format's Default": "使用格式默认值",
    "Use Only Co-Directional Polygons": "仅使用同向多边形",
    "Vertex Mask Color": "顶点遮罩颜色",
    "Vertex Order": "顶点顺序",
}


IDENTITY_VALUES = {
    "Ascii", "B44", "B44A", "Binary", "Ctrl+Return", "DWA-A", "DWA-B",
    "HDF5", "LZW", "PackBits", "PIZ", "PXR24", "RLE", "Shift+F2",
    "ZIP", "ZIPS",
}
NODE_STATUS = re.compile(
    r"^Node .+ (?:doens't have (?:a geom|an audio) output|has a computation error)$"
)
DYNAMIC_VALUE = re.compile(r"^(?:Camera|Press)\d*$|^Press \d+$")


def load(path: Path) -> dict[str, object]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def main() -> int:
    inventory = load(INVENTORY)
    dictionary = load(DICTIONARY)
    candidates = inventory.get("unique_candidates")
    translations = dictionary.get("translations")
    nodes = inventory.get("nodes")
    if not isinstance(candidates, list) or not isinstance(translations, dict) or not isinstance(nodes, dict):
        raise ValueError("invalid dictionary or node inventory")

    additions = dict(UI_TRANSLATIONS)
    additions["Clear %1 point(s)"] = "清除 %1 个点"
    additions["Press %1"] = "按压 %1"
    for source in candidates:
        if not isinstance(source, str) or not (source.startswith("<b>") and source.endswith("</b>")):
            continue
        node_type = source[3:-4]
        target = translations.get(node_type)
        if isinstance(target, str) and target:
            additions[source] = f"<b>{target}</b>"

    text_image = nodes.get("TextImage", {})
    text_image_candidates = text_image.get("candidates", []) if isinstance(text_image, dict) else []
    font_names = {
        value for value in text_image_candidates
        if isinstance(value, str) and value not in translations and value not in additions
        and not value.startswith("<b>") and not NODE_STATUS.fullmatch(value)
    }

    normalized_existing = {normalized_key(key) for key in translations}
    unclassified: list[str] = []
    excluded = 0
    for source in candidates:
        if not isinstance(source, str) or source in translations or source in additions:
            continue
        if normalized_key(source) in normalized_existing:
            excluded += 1
            continue
        if (source in font_names or source in IDENTITY_VALUES or
                NODE_STATUS.fullmatch(source) or DYNAMIC_VALUE.fullmatch(source) or
                source == "Clear 0 point(s)"):
            excluded += 1
            continue
        unclassified.append(source)
    if unclassified:
        raise ValueError("unclassified runtime candidates: " + repr(unclassified))

    for source, target in additions.items():
        translations[source] = target
    # Updating an existing key preserves its position; new reviewed keys are
    # appended without reordering thousands of unrelated release entries.
    dictionary["translations"] = translations
    temporary = DICTIONARY.with_suffix(DICTIONARY.suffix + ".tmp")
    temporary.write_text(json.dumps(dictionary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(DICTIONARY)
    print(f"promoted={len(additions)} excluded_or_covered={excluded} total={len(translations)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
