#pragma once
#include <windows.h>
#include "import_hook.h"
#include "ui_paint_boundary.h"
#include <QtGui/QFontMetricsF>
// MinGW x64 ABI wrappers. Only Qt5Widgets imports are redirected; host
// measurements, models, export code and Qt5Gui remain in their source language.
namespace WidgetDisplayMetrics {
using WrapImportHooks::resolveFunction;
using WrapImportHooks::functionAddress;
using WrapImportHooks::replaceImports;
static QString (*lookup)(const QString&) = nullptr;
static DWORD guiThread = 0;
static unsigned patchedSlots = 0;
static bool sizeHooked = false;
static bool g_fontMetricHooksInstalled = false;
inline std::vector<WrapImportHooks::PatchRecord> patches;
using FmAdvance = int (*)(const QFontMetrics*, const QString&, int);
using FmWidth = int (*)(const QFontMetrics*, const QString&, int);
using FmWidthFlags = int (*)(const QFontMetrics*, const QString&, int, int);
using FmBoundingRect = QRect (*)(const QFontMetrics*, const QString&);
using FmBoundingRectArea = QRect (*)(const QFontMetrics*, const QRect&, int,
                                     const QString&, int, int*);
using FmSize = QSize (*)(const QFontMetrics*, int, const QString&, int, int*);
using FmTightBoundingRect = QRect (*)(const QFontMetrics*, const QString&);
using FmElidedText = QString (*)(const QFontMetrics*, const QString&,
                                 Qt::TextElideMode, int, int);

using FmfAdvance = qreal (*)(const QFontMetricsF*, const QString&, int);
using FmfWidth = qreal (*)(const QFontMetricsF*, const QString&);
using FmfBoundingRect = QRectF (*)(const QFontMetricsF*, const QString&);
using FmfBoundingRectArea = QRectF (*)(const QFontMetricsF*, const QRectF&, int,
                                       const QString&, int, int*);
using FmfSize = QSizeF (*)(const QFontMetricsF*, int, const QString&, int, int*);
using FmfTightBoundingRect = QRectF (*)(const QFontMetricsF*, const QString&);
using FmfElidedText = QString (*)(const QFontMetricsF*, const QString&,
                                  Qt::TextElideMode, qreal, int);

FmAdvance g_fmAdvance = nullptr;
FmWidth g_fmWidth = nullptr;
FmWidthFlags g_fmWidthFlags = nullptr;
FmBoundingRect g_fmBoundingRect = nullptr;
FmBoundingRectArea g_fmBoundingRectArea = nullptr;
FmSize g_fmSize = nullptr;
FmTightBoundingRect g_fmTightBoundingRect = nullptr;
FmElidedText g_fmElidedText = nullptr;
FmfAdvance g_fmfAdvance = nullptr;
FmfWidth g_fmfWidth = nullptr;
FmfBoundingRect g_fmfBoundingRect = nullptr;
FmfBoundingRectArea g_fmfBoundingRectArea = nullptr;
FmfSize g_fmfSize = nullptr;
FmfTightBoundingRect g_fmfTightBoundingRect = nullptr;
FmfElidedText g_fmfElidedText = nullptr;
thread_local bool g_inFontMetricHook = false;

class FontMetricGuard {
public:
    FontMetricGuard() : previous_(g_inFontMetricHook) { g_inFontMetricHook = true; }
    ~FontMetricGuard() { g_inFontMetricHook = previous_; }
    bool reentered() const { return previous_; }
private:
    bool previous_;
};

QString translateMeasured(const QString& text, bool reentered) {
    if (reentered || GetCurrentThreadId() != guiThread || !lookup || text.size() < 2 ||
        UiPaintBoundary::isEditor(UiPaintBoundary::displayWidget())) return {};
    return lookup(text);
}

int hookedFmAdvance(const QFontMetrics* self, const QString& text, int length) {
    if (!g_fmAdvance) return 0;
    FontMetricGuard guard;
    const QString value = (length < 0 || length >= text.size()) ? translateMeasured(text, guard.reentered()) : QString();
    return g_fmAdvance(self, value.isEmpty() ? text : value,
                       value.isEmpty() ? length : -1);
}

int hookedFmWidth(const QFontMetrics* self, const QString& text, int length) {
    if (!g_fmWidth) return 0;
    FontMetricGuard guard;
    const QString value = (length < 0 || length >= text.size()) ? translateMeasured(text, guard.reentered()) : QString();
    return g_fmWidth(self, value.isEmpty() ? text : value,
                     value.isEmpty() ? length : -1);
}

int hookedFmWidthFlags(const QFontMetrics* self, const QString& text,
                       int length, int flags) {
    if (!g_fmWidthFlags) return 0;
    FontMetricGuard guard;
    const QString value = (length < 0 || length >= text.size()) ? translateMeasured(text, guard.reentered()) : QString();
    return g_fmWidthFlags(self, value.isEmpty() ? text : value,
                          value.isEmpty() ? length : -1, flags);
}

QRect hookedFmBoundingRect(const QFontMetrics* self, const QString& text) {
    if (!g_fmBoundingRect) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmBoundingRect(self, value.isEmpty() ? text : value);
}

QRect hookedFmBoundingRectArea(const QFontMetrics* self, const QRect& rect,
                               int flags, const QString& text,
                               int tabStops, int* tabArray) {
    if (!g_fmBoundingRectArea) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmBoundingRectArea(self, rect, flags,
                                value.isEmpty() ? text : value,
                                tabStops, tabArray);
}

QSize hookedFmSize(const QFontMetrics* self, int flags, const QString& text,
                   int tabStops, int* tabArray) {
    if (!g_fmSize) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmSize(self, flags, value.isEmpty() ? text : value,
                    tabStops, tabArray);
}

QRect hookedFmTightBoundingRect(const QFontMetrics* self,
                                const QString& text) {
    if (!g_fmTightBoundingRect) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmTightBoundingRect(self, value.isEmpty() ? text : value);
}

QString hookedFmElidedText(const QFontMetrics* self, const QString& text,
                           Qt::TextElideMode mode, int width, int flags) {
    if (!g_fmElidedText) return text;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmElidedText(self, value.isEmpty() ? text : value,
                          mode, width, flags);
}

qreal hookedFmfAdvance(const QFontMetricsF* self, const QString& text,
                       int length) {
    if (!g_fmfAdvance) return 0.0;
    FontMetricGuard guard;
    const QString value = (length < 0 || length >= text.size()) ? translateMeasured(text, guard.reentered()) : QString();
    return g_fmfAdvance(self, value.isEmpty() ? text : value,
                        value.isEmpty() ? length : -1);
}

qreal hookedFmfWidth(const QFontMetricsF* self, const QString& text) {
    if (!g_fmfWidth) return 0.0;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfWidth(self, value.isEmpty() ? text : value);
}

QRectF hookedFmfBoundingRect(const QFontMetricsF* self, const QString& text) {
    if (!g_fmfBoundingRect) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfBoundingRect(self, value.isEmpty() ? text : value);
}

QRectF hookedFmfBoundingRectArea(const QFontMetricsF* self, const QRectF& rect,
                                 int flags, const QString& text,
                                 int tabStops, int* tabArray) {
    if (!g_fmfBoundingRectArea) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfBoundingRectArea(self, rect, flags,
                                 value.isEmpty() ? text : value,
                                 tabStops, tabArray);
}

QSizeF hookedFmfSize(const QFontMetricsF* self, int flags,
                     const QString& text, int tabStops, int* tabArray) {
    if (!g_fmfSize) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfSize(self, flags, value.isEmpty() ? text : value,
                     tabStops, tabArray);
}

QRectF hookedFmfTightBoundingRect(const QFontMetricsF* self,
                                  const QString& text) {
    if (!g_fmfTightBoundingRect) return {};
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfTightBoundingRect(self, value.isEmpty() ? text : value);
}

QString hookedFmfElidedText(const QFontMetricsF* self, const QString& text,
                            Qt::TextElideMode mode, qreal width, int flags) {
    if (!g_fmfElidedText) return text;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmfElidedText(self, value.isEmpty() ? text : value,
                           mode, width, flags);
}

bool patchFontMetricModules() {
    const WrapImportHooks::Replacement symbols[] = {
        {functionAddress(g_fmAdvance), functionAddress(&hookedFmAdvance)},
        {functionAddress(g_fmWidth), functionAddress(&hookedFmWidth)},
        {functionAddress(g_fmWidthFlags), functionAddress(&hookedFmWidthFlags)},
        {functionAddress(g_fmBoundingRect), functionAddress(&hookedFmBoundingRect)},
        {functionAddress(g_fmBoundingRectArea), functionAddress(&hookedFmBoundingRectArea)},
        {functionAddress(g_fmSize), functionAddress(&hookedFmSize)},
        {functionAddress(g_fmTightBoundingRect), functionAddress(&hookedFmTightBoundingRect)},
        {functionAddress(g_fmElidedText), functionAddress(&hookedFmElidedText)},
        {functionAddress(g_fmfAdvance), functionAddress(&hookedFmfAdvance)},
        {functionAddress(g_fmfWidth), functionAddress(&hookedFmfWidth)},
        {functionAddress(g_fmfBoundingRect), functionAddress(&hookedFmfBoundingRect)},
        {functionAddress(g_fmfBoundingRectArea), functionAddress(&hookedFmfBoundingRectArea)},
        {functionAddress(g_fmfSize), functionAddress(&hookedFmfSize)},
        {functionAddress(g_fmfTightBoundingRect), functionAddress(&hookedFmfTightBoundingRect)},
        {functionAddress(g_fmfElidedText), functionAddress(&hookedFmfElidedText)},
    };
    HMODULE widgets = GetModuleHandleW(L"Qt5Widgets.dll");
    if (!widgets) return false;
    replaceImports(widgets, symbols, sizeof(symbols) / sizeof(symbols[0]), &patches);
    patchedSlots = unsigned(patches.size());
    for (const auto& patch : patches)
        if (patch.original == functionAddress(g_fmSize)) sizeHooked = true;
    return patchedSlots != 0;
}

inline void shutdown() {
    WrapImportHooks::restoreImports(patches);
    patchedSlots = 0;
    sizeHooked = false;
    g_fontMetricHooksInstalled = false;
    lookup = nullptr;
}

void install(QString (*translate)(const QString&)) {
    lookup = translate;
    guiThread = GetCurrentThreadId();
    if (g_fontMetricHooksInstalled) return;
    HMODULE gui = GetModuleHandleW(L"Qt5Gui.dll");
    if (!gui) return;

    g_fmAdvance = resolveFunction<FmAdvance>(
        gui, "_ZNK12QFontMetrics17horizontalAdvanceERK7QStringi");
    g_fmWidth = resolveFunction<FmWidth>(
        gui, "_ZNK12QFontMetrics5widthERK7QStringi");
    g_fmWidthFlags = resolveFunction<FmWidthFlags>(
        gui, "_ZNK12QFontMetrics5widthERK7QStringii");
    g_fmBoundingRect = resolveFunction<FmBoundingRect>(
        gui, "_ZNK12QFontMetrics12boundingRectERK7QString");
    g_fmBoundingRectArea = resolveFunction<FmBoundingRectArea>(
        gui, "_ZNK12QFontMetrics12boundingRectERK5QRectiRK7QStringiPi");
    g_fmSize = resolveFunction<FmSize>(
        gui, "_ZNK12QFontMetrics4sizeEiRK7QStringiPi");
    g_fmTightBoundingRect = resolveFunction<FmTightBoundingRect>(
        gui, "_ZNK12QFontMetrics17tightBoundingRectERK7QString");
    g_fmElidedText = resolveFunction<FmElidedText>(
        gui, "_ZNK12QFontMetrics10elidedTextERK7QStringN2Qt13TextElideModeEii");

    g_fmfAdvance = resolveFunction<FmfAdvance>(
        gui, "_ZNK13QFontMetricsF17horizontalAdvanceERK7QStringi");
    g_fmfWidth = resolveFunction<FmfWidth>(
        gui, "_ZNK13QFontMetricsF5widthERK7QString");
    g_fmfBoundingRect = resolveFunction<FmfBoundingRect>(
        gui, "_ZNK13QFontMetricsF12boundingRectERK7QString");
    g_fmfBoundingRectArea = resolveFunction<FmfBoundingRectArea>(
        gui, "_ZNK13QFontMetricsF12boundingRectERK6QRectFiRK7QStringiPi");
    g_fmfSize = resolveFunction<FmfSize>(
        gui, "_ZNK13QFontMetricsF4sizeEiRK7QStringiPi");
    g_fmfTightBoundingRect = resolveFunction<FmfTightBoundingRect>(
        gui, "_ZNK13QFontMetricsF17tightBoundingRectERK7QString");
    g_fmfElidedText = resolveFunction<FmfElidedText>(
        gui, "_ZNK13QFontMetricsF10elidedTextERK7QStringN2Qt13TextElideModeEdi");

    g_fontMetricHooksInstalled =
        g_fmAdvance || g_fmWidth || g_fmWidthFlags || g_fmBoundingRect ||
        g_fmBoundingRectArea || g_fmSize || g_fmTightBoundingRect ||
        g_fmElidedText || g_fmfAdvance || g_fmfWidth || g_fmfBoundingRect ||
        g_fmfBoundingRectArea || g_fmfSize || g_fmfTightBoundingRect ||
        g_fmfElidedText;
    if (g_fontMetricHooksInstalled) patchFontMetricModules();
}

}
