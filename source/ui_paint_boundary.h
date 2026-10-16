#pragma once

#include <QtGui/QPainter>
#include <QtWidgets/QWidget>
#include <QtWidgets/QComboBox>

// An image is localizable only when a known widget callback owns that exact
// paint target. Other images drawn on the same thread remain export-safe.
namespace UiPaintBoundary {
inline const QWidget*& displayWidget() {
    static thread_local const QWidget* widget = nullptr;
    return widget;
}

inline bool isEditor(const QWidget* widget) {
    for (int depth = 0; widget && depth < 16; ++depth, widget = widget->parentWidget()) {
        if (widget->inherits("QLineEdit") || widget->inherits("QTextEdit") ||
            widget->inherits("QPlainTextEdit") || widget->inherits("QAbstractSpinBox") ||
            widget->inherits("QKeySequenceEdit")) return true;
        if (const auto* combo = qobject_cast<const QComboBox*>(widget))
            if (combo->isEditable()) return true;
    }
    return false;
}
inline const QPaintDevice*& displayCacheDevice() {
    static thread_local const QPaintDevice* device = nullptr;
    return device;
}

class WidgetPaintScope final {
public:
    WidgetPaintScope(const QPainter* painter, const QWidget* widget = nullptr)
        : previous_(displayCacheDevice()), previousWidget_(displayWidget()) {
        const auto* device = painter ? painter->device() : nullptr;
        displayWidget() = widget;
        if ((device && device->devType() == QInternal::Widget) ||
            (widget && widget->isVisible()))
            displayCacheDevice() = device;
        else if (device != previous_)
            displayCacheDevice() = nullptr;
    }
    ~WidgetPaintScope() {
        displayCacheDevice() = previous_;
        displayWidget() = previousWidget_;
    }
    WidgetPaintScope(const WidgetPaintScope&) = delete;
    WidgetPaintScope& operator=(const WidgetPaintScope&) = delete;
private:
    const QPaintDevice* previous_;
    const QWidget* previousWidget_;
};

inline bool isDisplayDevice(const QPaintDevice* device) {
    if (!device) return false;
    const int type = device->devType();
    const auto* widget = type == QInternal::Widget
        ? static_cast<const QWidget*>(device) : displayWidget();
    if (isEditor(widget)) return false;
    return type == QInternal::Widget || (device == displayCacheDevice() &&
        (type == QInternal::Image || type == QInternal::Pixmap));
}

inline bool isDisplayPainter(const QPainter* painter) {
    return painter && isDisplayDevice(painter->device());
}
} // namespace UiPaintBoundary
