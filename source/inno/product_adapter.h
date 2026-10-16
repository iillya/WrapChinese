// Wrap uses the same read-only Qt ABI validation in launcher and installer.
#include "product_config.h"
inline bool productQt(const std::wstring& root) {
    return WrapChineseCompatibility::supportsQt5Directory(root);
}
