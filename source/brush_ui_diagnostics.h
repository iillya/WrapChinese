#pragma once

#include <windows.h>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtGui/QFontMetrics>
#include <QtGui/QScreen>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAbstractItemDelegate>
#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <typeinfo>
#include <vector>

// Invoked only by the explicit capture command, never by painting or polling.
// Do not read label text, model contents, node names or project/window titles.
namespace BrushUiDiagnostics {
inline QJsonArray size(const QSize& value) { return {value.width(), value.height()}; }
inline QJsonArray rect(const QRect& value) {
    return {value.x(), value.y(), value.width(), value.height()};
}

inline QJsonObject module(const wchar_t* name) {
    HMODULE handle = GetModuleHandleW(name);
    QJsonObject result{{QStringLiteral("name"), QString::fromWCharArray(name)},
                       {QStringLiteral("loaded"), handle != nullptr}};
    if (!handle) return result;
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(handle, path.data(), DWORD(path.size()));
    if (!length || length >= path.size()) return result;
    QFile file(QString::fromWCharArray(path.data(), int(length)));
    if (file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= 64 * 1024 * 1024)
        result.insert(QStringLiteral("sha256"), QString::fromLatin1(
            QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()));
    return result;
}

inline QJsonObject snapshot(QJsonObject context) {
    context.insert(QStringLiteral("schema"), QStringLiteral("wrap-window-diagnostics-v1"));
    context.insert(QStringLiteral("qt_runtime"), QString::fromLatin1(qVersion()));
    context.insert(QStringLiteral("application_stylesheet"), !qApp->styleSheet().isEmpty());
    if (const auto* style = qApp->style()) {
        context.insert(QStringLiteral("style_class"), QString::fromLatin1(style->metaObject()->className()));
        context.insert(QStringLiteral("style_cpp_type"), QString::fromLatin1(typeid(*style).name()));
        context.insert(QStringLiteral("style_key"), style->objectName());
    }
    QJsonArray modules;
    for (const wchar_t* name : {L"Wrap.exe", L"Qt5Core.dll", L"Qt5Gui.dll",
                               L"Qt5Widgets.dll", L"libstdc++-6.dll", L"libgcc_s_seh-1.dll",
                               L"WrapChineseHook.dll"})
        modules.append(module(name));
    context.insert(QStringLiteral("modules"), modules);
    QJsonArray screens;
    for (const auto* screen : QGuiApplication::screens())
        screens.append(QJsonObject{
            {QStringLiteral("geometry"), rect(screen->geometry())},
            {QStringLiteral("available_geometry"), rect(screen->availableGeometry())},
            {QStringLiteral("dpr"), screen->devicePixelRatio()},
            {QStringLiteral("logical_dpi"), screen->logicalDotsPerInch()},
        });
    context.insert(QStringLiteral("screens"), screens);

    const auto all = QApplication::allWidgets();
    QSet<const QWidget*> relevant;
    int brushCount = 0;
    for (const QWidget* widget : all) {
        if (!widget || !(widget->inherits("Wrap::Brush::UI::ToolBarButton") ||
                        widget->inherits("Wrap::Brush::UI::BrushToolBar") ||
                        widget->inherits("Wrap::WVisualEditorBrush::Editor") ||
                        widget->inherits("Wrap::WNodeEditorBrush"))) continue;
        ++brushCount;
        for (int depth = 0; widget && depth < 12; ++depth, widget = widget->parentWidget())
            relevant.insert(widget);
    }
    QJsonArray widgets;
    for (const QWidget* widget : all) {
        if (!relevant.contains(widget)) continue;
        QJsonArray chain;
        for (const QMetaObject* meta = widget->metaObject(); meta; meta = meta->superClass())
            chain.append(QString::fromLatin1(meta->className()));
        const auto* parent = widget->parentWidget();
        QJsonObject item{
            {QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className())},
            {QStringLiteral("class_chain"), chain},
            {QStringLiteral("parent_class"), parent ? QString::fromLatin1(parent->metaObject()->className()) : QString()},
            {QStringLiteral("geometry"), rect(widget->geometry())},
            {QStringLiteral("minimum"), size(widget->minimumSize())},
            {QStringLiteral("maximum"), size(widget->maximumSize())},
            {QStringLiteral("size_hint"), size(widget->sizeHint())},
            {QStringLiteral("visible"), widget->isVisible()},
            {QStringLiteral("explicitly_hidden"), widget->isHidden()},
            {QStringLiteral("enabled"), widget->isEnabled()},
            {QStringLiteral("dpr"), widget->devicePixelRatioF()},
            {QStringLiteral("font_family"), widget->font().family()},
            {QStringLiteral("font_points"), widget->font().pointSizeF()},
            {QStringLiteral("font_has_chinese"), QFontMetrics(widget->font()).inFontUcs4(0x677e)},
            {QStringLiteral("own_stylesheet"), !widget->styleSheet().isEmpty()},
        };
        const QPalette::ColorGroup group = !widget->isEnabled() ? QPalette::Disabled :
            (widget->isActiveWindow() ? QPalette::Active : QPalette::Inactive);
        item.insert(QStringLiteral("foreground"), widget->palette().color(group,
            widget->foregroundRole()).name(QColor::HexArgb));
        item.insert(QStringLiteral("background"), widget->palette().color(group,
            widget->backgroundRole()).name(QColor::HexArgb));
        if (widget->isWindow()) item.insert(QStringLiteral("frame_geometry"), rect(widget->frameGeometry()));
        if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
            item.insert(QStringLiteral("icon_null"), button->icon().isNull());
            item.insert(QStringLiteral("caption_empty"), button->text().isEmpty());
        }
        if (const auto* button = qobject_cast<const QToolButton*>(widget)) {
            item.insert(QStringLiteral("tool_button_style"), int(button->toolButtonStyle()));
            item.insert(QStringLiteral("icon_size"), size(button->iconSize()));
        }
        if (const auto* view = qobject_cast<const QAbstractItemView*>(widget)) {
            const auto* delegate = view->itemDelegate();
            item.insert(QStringLiteral("delegate_cpp_type"), delegate
                ? QString::fromLatin1(typeid(*delegate).name()) : QString());
        }
        if (const auto* splitter = qobject_cast<const QSplitter*>(widget)) {
            QJsonArray sizes;
            for (int value : splitter->sizes()) sizes.append(value);
            item.insert(QStringLiteral("splitter_sizes"), sizes);
        }
        if (const auto* scroll = qobject_cast<const QScrollArea*>(widget)) {
            item.insert(QStringLiteral("viewport_size"), size(scroll->viewport()->size()));
            item.insert(QStringLiteral("vertical_scroll"), QJsonArray{
                scroll->verticalScrollBar()->minimum(), scroll->verticalScrollBar()->maximum(),
                scroll->verticalScrollBar()->value()});
        }
        widgets.append(item);
    }
    context.insert(QStringLiteral("brush_widget_count"), brushCount);
    context.insert(QStringLiteral("widgets"), widgets);
    QJsonArray icons;
    const auto names = QDir(QStringLiteral(":/BrushToolBarButton/Images")).entryList(QDir::Files);
    for (const QString& name : names.mid(0, 128)) icons.append(name);
    context.insert(QStringLiteral("brush_icon_resources"), icons);
    return context;
}
} // namespace BrushUiDiagnostics
