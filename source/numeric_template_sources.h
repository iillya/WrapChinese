#pragma once
// Reviewed Wrap interface/status messages. Names, paths, node titles and
// arbitrary string parameters are deliberately absent. Values come from the
// release dictionary; this list alone grants runtime numeric matching.
inline constexpr const char* kNumericTemplateSources[] = {
    "Camera Reproj. Error: %1 pxl",
    "Cameras not found, %1 nodes of other types found instead.",
    "Chosen frame index at %1 is out of range",
    "Clear %1 point(s)",
    "Current frame %1 has %2 vertices, but Neutral geom has %3",
    "Detection has %1 semantic points on inner %2 lip contour. But %3 required!",
    "Info: %1 pairs of points were used for fitting.",
    "Input ONNX controls number (%1) mismatches controls list number (%2)",
    "Input Rig controls number (%1) mismatches controls list number (%2)",
    "Number of files (%1) doesn't match the number of indices (%2)",
    "Right geom: %1 point(s) refer to non-existent triangles",
    "Specified object does not contain frame %1",
    "Stereo Reprojection Error: %1 pxl",
    "The following errors occured during frame %1 computation:",
    "Unsupported format version %1",
    "Version %1 is not supported",
};
