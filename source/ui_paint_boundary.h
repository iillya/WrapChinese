#pragma once

#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

// An image is localizable only when a known widget callback owns that exact
// paint target. Other images drawn on the same thread remain export-safe.
namespace UiPaintBoundary {
inline const QPaintDevice*& displayCacheDevice() {
    static thread_local const QPaintDevice* device = nullptr;
    return device;
}

class WidgetPaintScope final {
public:
    WidgetPaintScope(const QPainter* painter, const QWidget* widget = nullptr)
        : previous_(displayCacheDevice()) {
        const auto* device = painter ? painter->device() : nullptr;
        if ((device && device->devType() == QInternal::Widget) ||
            (widget && widget->isVisible()))
            displayCacheDevice() = device;
        else if (device != previous_)
            displayCacheDevice() = nullptr;
    }
    ~WidgetPaintScope() { displayCacheDevice() = previous_; }
    WidgetPaintScope(const WidgetPaintScope&) = delete;
    WidgetPaintScope& operator=(const WidgetPaintScope&) = delete;
private:
    const QPaintDevice* previous_;
};

inline bool isDisplayDevice(const QPaintDevice* device) {
    if (!device) return false;
    const int type = device->devType();
    return type == QInternal::Widget || (device == displayCacheDevice() &&
        (type == QInternal::Image || type == QInternal::Pixmap));
}

inline bool isDisplayPainter(const QPainter* painter) {
    return painter && isDisplayDevice(painter->device());
}
} // namespace UiPaintBoundary
