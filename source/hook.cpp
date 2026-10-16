// Wrap Qt 5 Chinese display-layer localizer.
//
// The host executable and project data remain untouched. A launcher injects
// this MinGW/Qt 5 DLL before Wrap starts. Translation is applied only at
// style, item-delegate, rich-label and QPainter display boundaries. Host text
// properties and serialized data stay unchanged; display measurements may adapt.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "hook_lifecycle.h"
#include "hotkey_settings.h"
#include "qt_placeholder_templates.h"
#include "runtime_compatibility.h"
#include "numeric_template_sources.h"
#include "translation_lookup_cache.h"
#include "shortcut_hint_display.h"
#include "brush_ui_diagnostics.h"

#include <QtCore/QAbstractItemModel>
#include <QtCore/QCache>
#include <QtCore/QElapsedTimer>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QMetaObject>
#ifdef WRAP_CHINESE_DEVELOPER_TOOLS
#include <QtCore/QMetaMethod>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QShortcut>
#endif
#include <QtCore/QPointer>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QFontMetrics>
#include "widget_display_metrics.h"
#include "ui_paint_boundary.h"
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QTextDocument>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QTextOption>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QToolBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using WrapImportHooks::resolveFunction;
using WrapImportHooks::functionAddress;
using WrapImportHooks::replaceImports;

constexpr wchar_t kHookPrefix[] = L"WrapChineseHook";
constexpr wchar_t kGuiModule[] = L"Qt5Gui.dll";
constexpr wchar_t kCoreModule[] = L"Qt5Core.dll";
constexpr char kEngineVersion[] = "1.0.2";
constexpr qint64 kMaximumDictionaryBytes = 32LL * 1024 * 1024;
constexpr int kMaximumDictionaryFiles = 32;
constexpr std::size_t kMaximumDictionaryEntries = 200000;
constexpr int kMaximumDictionarySourceLength = 4096;
constexpr int kMaximumDictionaryTargetLength = 8192;

HMODULE g_selfModule = nullptr;
QString g_pluginDir;
std::atomic_bool g_enabled{true};
std::atomic_bool g_initializationStarted{false};
bool g_drawHooksInstalled = false;
bool g_proxyStyleInstalled = false;
bool g_shuttingDown = false;
QPointer<QObject> g_applicationFilter;
QPointer<QObject> g_menuFilter;
QPointer<QTimer> g_maintenanceTimer;
QElapsedTimer g_moduleScanAge;
bool g_modulesDirty = false;
QString g_proxyStyleDecision = QStringLiteral("not-attempted");
QString g_originalStyleKey;
std::unique_ptr<QPalette> g_originalPalette;
QPointer<QStyle> g_translationStyle;
#ifdef WRAP_CHINESE_TEST_HOOKS
std::atomic_uint32_t g_translatedRichLabelDraws{0};
#endif
QKeySequence g_toggleShortcut = WrapChineseConfig::defaultToggleShortcut();
bool g_captureEnabled = true;
QString g_configPath;
bool g_hotkeyDialogOpen = false;
QPointer<QMenuBar> g_menuBar;
QPointer<QAction> g_languageAction;
QPointer<QAction> g_authorAction;
QPointer<QAction> g_githubAction;
QPointer<QDialog> g_captureDialog;
QPointer<WrapChineseHotkeyDialog> g_hotkeyDialog;
QSet<int> g_swallowedShortcutKeys;

void signalHookReady() {
    wchar_t eventName[96] = {};
    swprintf_s(eventName, L"Local\\WrapChineseHookReady_%lu",
               GetCurrentProcessId());
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (!event) return;
    SetEvent(event);
    CloseHandle(event);
}

QHash<QString, QString> g_translations;
QHash<QString, QString> g_normalized;
QJsonArray g_dictionaryConflicts;

WrapChinesePlaceholders::Catalog g_templates;
thread_local WrapChineseLookupCache::DirectMapped<> g_measurementCache;
thread_local WrapChineseLookupCache::DirectMapped<> g_lookupCache;

QString selfDirectory() {
    std::vector<wchar_t> buffer(1024);
    while (buffer.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(
            g_selfModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1) {
            const QString path = QString::fromWCharArray(buffer.data(), int(length));
            return QFileInfo(path).absolutePath();
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

bool containsCjk(const QString& text) {
    for (int index = 0; index < text.size(); ++index) {
        uint value = text[index].unicode();
        if (QChar::isHighSurrogate(value) && index + 1 < text.size() &&
            QChar::isLowSurrogate(text[index + 1].unicode())) {
            const QChar high = text[index];
            const QChar low = text[index + 1];
            ++index;
            value = QChar::surrogateToUcs4(high, low);
        }
        if ((value >= 0x1100 && value <= 0x11ff) ||
            (value >= 0x2e80 && value <= 0x303f) ||
            (value >= 0x3040 && value <= 0x30ff) ||
            (value >= 0x3130 && value <= 0x318f) ||
            (value >= 0x31f0 && value <= 0x31ff) ||
            (value >= 0x3400 && value <= 0x4dbf) ||
            (value >= 0x4e00 && value <= 0x9fff) ||
            (value >= 0xa960 && value <= 0xa97f) ||
            (value >= 0xac00 && value <= 0xd7af) ||
            (value >= 0xd7b0 && value <= 0xd7ff) ||
            (value >= 0xf900 && value <= 0xfaff) ||
            (value >= 0xff66 && value <= 0xffdc) ||
            (value >= 0x20000 && value <= 0x323af) ||
            (value >= 0x2f800 && value <= 0x2fa1f)) return true;
    }
    return false;
}

QString normalize(QString text) {
    text.replace(QChar(0x00a0), QLatin1Char(' '));
    text.replace(QChar(0x3000), QLatin1Char(' '));
    text.remove(QChar(0x200b));
    text.remove(QChar(0x200d));
    text.remove(QChar(0xfeff));
    text.remove(QChar(0x200c));
    text.remove(QChar(0x2060));
    text.remove(QChar(0x00ad));
    for (int i = 0; i < text.size(); ++i) {
        ushort ch = text.at(i).unicode();
        if (ch >= 0xff01 && ch <= 0xff5e) ch -= 0xfee0;
        if (ch == '_') ch = ' ';
        if (ch >= 0x2018 && ch <= 0x201b) ch = '\'';
        if (ch == 0x201c || ch == 0x201d) ch = '"';
        if ((ch >= 0x2010 && ch <= 0x2015) || ch == 0x2212) ch = '-';
        text[i] = QChar(ch);
    }
    text = text.simplified();
    while (text.endsWith(QChar(0x2026))) text.chop(1);
    while (text.endsWith(QLatin1String("..."))) text.chop(3);
    if (text.endsWith(QLatin1Char('*'))) text.chop(1);
    QString mnemonicFree;
    mnemonicFree.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        if (text[index] != QLatin1Char('&')) {
            mnemonicFree += text[index];
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == QLatin1Char('&')) {
            mnemonicFree += QLatin1Char('&');
            ++index;
        }
    }
    text = mnemonicFree.toCaseFolded().normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.category() == QChar::Mark_NonSpacing || ch.category() == QChar::Mark_SpacingCombining ||
            ch.category() == QChar::Mark_Enclosing) continue;
        if (ch.isSpace()) {
            if (!result.isEmpty() && !result.endsWith(QLatin1Char(' ')))
                result += QLatin1Char(' ');
            continue;
        }
        result.append(ch);
    }
    return result.trimmed().normalized(QString::NormalizationForm_C);
}

void loadDictionaries() {
    // Called once, on the GUI thread, before any display hook is published.
    g_translations.clear();
    g_normalized.clear();
    g_dictionaryConflicts = QJsonArray();
    g_templates.clear();
    g_lookupCache.clear();
    g_measurementCache.clear();
    QDir directory(g_pluginDir + QStringLiteral("/translations"));
    const QStringList files = directory.entryList(
        {QStringLiteral("*_zh.json")}, QDir::Files, QDir::Name).mid(0, kMaximumDictionaryFiles);
    QHash<QString, QString> normalizedSources;
    for (const QString& name : files) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
            file.size() > kMaximumDictionaryBytes) continue;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject root = document.object();
        const QJsonValue translationValue = root.value(QStringLiteral("translations"));
        if (root.value(QStringLiteral("$schema")).toString() !=
                QStringLiteral("sp-translation-v1") ||
            root.value(QStringLiteral("language")).toString() != QStringLiteral("zh-CN") ||
            !translationValue.isObject()) continue;
        const QJsonObject translations = translationValue.toObject();
        for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
            if (!it.value().isString() || it.key().trimmed().isEmpty() ||
                it.key().size() > kMaximumDictionarySourceLength) continue;
            const QString target = it.value().toString();
            if (target.trimmed().isEmpty() || target.size() > kMaximumDictionaryTargetLength ||
                !WrapChinesePlaceholders::validatePair(it.key(), target)) continue;
            if (g_translations.size() >= int(kMaximumDictionaryEntries) &&
                !g_translations.contains(it.key())) continue;
            g_translations.insert(it.key(), target);
            const QString folded = normalize(it.key());
            if (!folded.isEmpty()) {
                const auto prior = g_normalized.constFind(folded);
                if (prior != g_normalized.constEnd() && *prior != target &&
                    g_dictionaryConflicts.size() < 256)
                    g_dictionaryConflicts.append(QJsonObject{
                        {QStringLiteral("file"), name},
                        {QStringLiteral("previous_source"), normalizedSources.value(folded)},
                        {QStringLiteral("source"), it.key()}});
                // Entry and file iteration order is stable; later input wins.
                g_normalized.insert(folded, target);
                normalizedSources.insert(folded, it.key());
            }
        }
    }
    for (const char* sourceUtf8 : kNumericTemplateSources) {
        const QString source = QString::fromUtf8(sourceUtf8);
        const auto target = g_translations.constFind(source);
        if (target != g_translations.constEnd()) g_templates.addOrAssign(source, *target);
    }
}

QString withoutMnemonics(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        if (text[index] != QLatin1Char('&')) {
            result += text[index];
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == QLatin1Char('&')) {
            result += QLatin1Char('&');
            ++index;
        }
    }
    return result;
}

QString exactTranslation(const QString& source) {
    return g_translations.value(source);
}

bool isUpperCamelIdentifier(const QString& text) {
    if (text.isEmpty() || !text.front().isUpper()) return false;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_')) continue;
        return false;
    }
    return true;
}

QString translatedRichIdentifiers(const QString& source) {
    if (!source.contains(QLatin1Char('<')) || !source.contains(QLatin1Char('>')))
        return {};
    QString result;
    QString token;
    bool inTag = false;
    bool inEntity = false;
    bool nodeType = false;
    QString tag;
    QChar tagQuote;
    bool changed = false;
    const auto flushToken = [&] {
        if (token.isEmpty()) return;
        const QString target = nodeType && isUpperCamelIdentifier(token)
            ? exactTranslation(token) : QString();
        if (!target.isEmpty()) {
            result += target;
            changed = true;
        } else {
            result += token;
        }
        token.clear();
    };
    for (const QChar ch : source) {
        if (!inTag && !inEntity && ch == QLatin1Char('<')) {
            flushToken();
            inTag = true;
            tag = QLatin1Char('<');
            tagQuote = QChar();
            result += ch;
        } else if (inTag && tagQuote.isNull() &&
                   (ch == QLatin1Char('\'') || ch == QLatin1Char('"'))) {
            tagQuote = ch;
            tag += ch;
            result += ch;
        } else if (inTag && !tagQuote.isNull() && ch == tagQuote) {
            tagQuote = QChar();
            tag += ch;
            result += ch;
        } else if (inTag && tagQuote.isNull() && ch == QLatin1Char('>')) {
            inTag = false;
            tag += ch;
            // Wrap titles bold the node type; the following instance name is
            // project data, even if it happens to occur in the dictionary.
            if (tag.compare(QLatin1String("<b>"), Qt::CaseInsensitive) == 0)
                nodeType = true;
            else if (tag.compare(QLatin1String("</b>"), Qt::CaseInsensitive) == 0)
                nodeType = false;
            result += ch;
        } else if (!inTag && !inEntity && ch == QLatin1Char('&')) {
            flushToken();
            inEntity = true;
            result += ch;
        } else if (inEntity && ch == QLatin1Char(';')) {
            inEntity = false;
            result += ch;
        } else if (inTag || inEntity) {
            if (inTag) tag += ch;
            result += ch;
        } else if (ch.isLetterOrNumber() || ch == QLatin1Char('_')) {
            token += ch;
        } else {
            flushToken();
            result += ch;
        }
    }
    flushToken();
    if (inTag || inEntity || !tagQuote.isNull() || !changed) return {};
    return result;
}

QString translatedUncached(const QString& raw, bool allowNumeric = true) {
    if (raw.size() > kMaximumDictionarySourceLength) return {};
    // Content paths and URLs do not become interface copy through normalization.
    if (raw.startsWith(QLatin1Char('/')) || raw.startsWith(QLatin1Char('\\')) ||
        raw.startsWith(QLatin1String(":/")) || raw.contains(QLatin1String("://")) ||
        (raw.size() >= 3 && raw.at(0).isLetter() && raw.at(1) == QLatin1Char(':') &&
         (raw.at(2) == QLatin1Char('/') || raw.at(2) == QLatin1Char('\\')))) return {};
    int first = 0;
    while (first < raw.size() && raw[first].isSpace()) ++first;
    int last = raw.size();
    while (last > first && raw[last - 1].isSpace()) --last;
    QString key = raw.mid(first, last - first);
    if (key.isEmpty()) return {};
    const QString leading = raw.left(first);
    const QString trailing = raw.mid(last);
    const auto decorate = [&](const QString& value) {
        return value.isEmpty() ? QString() : leading + value + trailing;
    };
    const auto exact = g_translations.constFind(key);
    if (exact != g_translations.constEnd()) return decorate(*exact);
    const QString rich = translatedRichIdentifiers(key);
    if (!rich.isEmpty()) return decorate(rich);

    QString suffix;
    if (key.endsWith(QLatin1Char('*'))) {
        key.chop(1);
        key = key.trimmed();
        suffix = QStringLiteral("*");
    }
    const QString mnemonic = withoutMnemonics(key);
    const auto clean = g_translations.constFind(mnemonic);
    if (clean != g_translations.constEnd()) return decorate(*clean + suffix);

    if (containsCjk(mnemonic)) return {};
    const auto fuzzy = g_normalized.constFind(normalize(mnemonic));
    if (fuzzy != g_normalized.constEnd()) return decorate(*fuzzy + suffix);
    // Templates match the full, case-sensitive source, without trimming,
    // mnemonic removal, suffix stripping or normalization.
    return allowNumeric ? g_templates.lookup(raw) : QString();
}

QString translated(const QString& raw) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        raw.size() > kMaximumDictionarySourceLength) return {};
    return g_lookupCache.lookup(raw, [&] { return translatedUncached(raw); });
}

QString translatedForMeasurement(const QString& source) {
    if (!g_enabled.load(std::memory_order_acquire) ||
        source.size() > kMaximumDictionarySourceLength) return {};
    return g_measurementCache.lookup(source, [&] { return translatedUncached(source, false); });
}

bool isDisplayPaintDevice(const QPainter* painter) {
    return UiPaintBoundary::isDisplayPainter(painter);
}

class ChineseProxyStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope scope(painter, widget);
        if (element == CE_MenuBarItem) {
            const auto* bar = qobject_cast<const QMenuBar*>(widget);
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            const auto* action = bar && item ? bar->actionAt(item->rect.center()) : nullptr;
            if (action && (action == g_authorAction || action == g_githubAction) &&
                (item->state & State_Enabled)) {
                QStyleOptionMenuItem display(*item);
                const QColor blue(0x66, 0xaa, 0xff);
                for (auto role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text})
                    display.palette.setColor(role, blue);
                QProxyStyle::drawControl(element, &display, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope scope(painter, widget);
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget = nullptr) const override {
        UiPaintBoundary::WidgetPaintScope scope(painter, widget);
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }



    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& contents, const QWidget* widget = nullptr) const override {
        if (type == CT_TabBarTab && !WidgetDisplayMetrics::sizeHooked) {
            if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option)) {
                const QString text = translated(tab->text);
                if (!text.isEmpty()) {
                    QStyleOptionTab display(*tab);
                    display.text = text;
                    const int delta = tab->fontMetrics.size(Qt::TextShowMnemonic, text).width() -
                        tab->fontMetrics.size(Qt::TextShowMnemonic, tab->text).width();
                    QSize measured(contents);
                    const bool vertical = tab->shape == QTabBar::RoundedWest ||
                        tab->shape == QTabBar::RoundedEast || tab->shape == QTabBar::TriangularWest ||
                        tab->shape == QTabBar::TriangularEast;
                    if (vertical) measured.setHeight(qMax(0, measured.height() + delta));
                    else measured.setWidth(qMax(0, measured.width() + delta));
                    return QProxyStyle::sizeFromContents(type, &display, measured, widget);
                }
            }
        }
        return QProxyStyle::sizeFromContents(type, option, contents, widget);
    }


    void drawItemText(QPainter* painter, const QRect& rect, int flags,
                      const QPalette& palette, bool enabled, const QString& text,
                      QPalette::ColorRole role = QPalette::NoRole) const override {
        const QString value = isDisplayPaintDevice(painter)
            ? translated(text) : QString();
        const QString draw = value.isEmpty() ? text : value;
        QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, draw, role);
    }
    QRect itemTextRect(const QFontMetrics& metrics, const QRect& rect,
                       int flags, bool enabled, const QString& text) const override {
        const QString value = translated(text);
        return QProxyStyle::itemTextRect(metrics, rect, flags, enabled,
                                         value.isEmpty() ? text : value);
    }
};

class TranslateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant& value, const QLocale& locale) const override {
        const QString source = QStyledItemDelegate::displayText(value, locale);
        const QString target = translated(source);
        return target.isEmpty() ? source : target;
    }
};

struct DelegateAttachment {
    QPointer<QAbstractItemView> view;
    QPointer<QAbstractItemDelegate> original, display;
};
std::vector<DelegateAttachment> g_delegates;

void applyWidget(QWidget* widget) {
    if (!widget) return;
    auto* view = qobject_cast<QAbstractItemView*>(widget);
    if (!view) return;
    auto* delegate = view->itemDelegate();
    if (dynamic_cast<TranslateDelegate*>(delegate)) return;
    // Subclasses without Q_OBJECT inherit the base metaObject name. Replacing
    // one of those removes the host's paint(), sizeHint() and editor behavior.
    if (!delegate || typeid(*delegate) != typeid(QStyledItemDelegate)) return;
    auto* display = new TranslateDelegate(view);
    display->setItemEditorFactory(
        static_cast<QStyledItemDelegate*>(delegate)->itemEditorFactory());
    g_delegates.erase(std::remove_if(g_delegates.begin(), g_delegates.end(),
        [](const auto& item) { return !item.view || !item.display; }), g_delegates.end());
    g_delegates.push_back({view, delegate, display});
    view->setItemDelegate(display);
}

void repaintAll() {
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget) continue;
        if (widget->isVisible()) widget->update();
    }
}

void toggleLanguage() {
    g_enabled.store(!g_enabled.load(std::memory_order_relaxed),
                    std::memory_order_release);
    std::vector<QPointer<QWidget>> widgets;
    for (QWidget* widget : QApplication::allWidgets()) widgets.emplace_back(widget);
    for (const auto& widget : widgets) {
        if (!widget) continue;
        const bool styleCache = qobject_cast<QTabBar*>(widget.data()) ||
            qobject_cast<QMenuBar*>(widget.data()) || qobject_cast<QMenu*>(widget.data());
        QEvent event(styleCache ? QEvent::StyleChange : QEvent::FontChange);
        QCoreApplication::sendEvent(widget, &event);
        if (widget) widget->updateGeometry();
    }
    repaintAll();
}

bool isCandidate(QString text) {
    text = text.trimmed();
    if (text.size() < 2 || text.size() > 1200 || containsCjk(text)) return false;
    // Runtime paths and document values are content, not interface copy. They
    // must never enter the translation dictionary merely because a directory
    // or filename contains Latin letters.
    const QString nativePath = QDir::fromNativeSeparators(text);
    if (QDir::isAbsolutePath(nativePath) || nativePath.startsWith(QLatin1String("//")))
        return false;
    if (text.startsWith(QLatin1String("http://")) ||
        text.startsWith(QLatin1String("https://")) ||
        text.startsWith(QLatin1String("qrc:/")) ||
        text.startsWith(QLatin1String(":/"))) return false;
    for (const QChar ch : text)
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))) return true;
    return false;
}

void collectModel(QAbstractItemModel* model, const QModelIndex& parent,
                  const std::function<void(const QString&)>& add,
                  int depth, int& budget) {
    if (!model || depth > 48 || budget <= 0) return;
    // Capture is read-only: never fetch lazy project/model content.
    const int rows = model->rowCount(parent);
    const int columns = std::min(model->columnCount(parent), 256);
    for (int row = 0; row < rows && budget > 0; ++row) {
        const QModelIndex branch = model->index(row, 0, parent);
        if (columns == 0) --budget;
        for (int column = 0; column < columns && budget > 0; ++column) {
            const QVariant value = model->index(row, column, parent).data(Qt::DisplayRole);
            if (value.isValid()) add(value.toString());
            --budget;
        }
        collectModel(model, branch, add, depth + 1, budget);
    }
}

QString capturePath() {
    const QString requested = qEnvironmentVariable("WRAP_CHINESE_CAPTURE_PATH");
    if (!requested.isEmpty()) return QDir::fromNativeSeparators(requested);
    const QString desktop =
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return desktop.isEmpty()
        ? g_pluginDir + QStringLiteral("/Wrap_untranslated_zh.json")
        : desktop + QStringLiteral("/Wrap_untranslated_zh.json");
}

bool writeCaptureFile(const QString& path, const QByteArray& bytes,
                      QString& error) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    if (output.write(bytes) != bytes.size()) {
        error = output.errorString();
        if (error.isEmpty()) error = QStringLiteral("写入不完整");
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        error = output.errorString();
        return false;
    }
    return true;
}

void captureUntranslated();
#ifdef WRAP_CHINESE_DEVELOPER_TOOLS
#include "developer_probe.h"
#endif

QString fallbackCapturePath(const QString& preferredPath) {
    const QFileInfo info(preferredPath);
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString suffix = info.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + info.suffix();
    return info.dir().filePath(info.completeBaseName() + QStringLiteral("_") +
                               stamp + suffix);
}

void showCaptureResult(int addedCount, int totalCount,
                       const QString& requestedPath,
                       const QString& savedPath, bool saved,
                       bool usedFallback, const QString& saveError,
                       const QString& diagnosticsPath) {
    if (g_captureDialog) g_captureDialog->close();

    QWidget* parent = g_menuBar ? g_menuBar->window()
                                : QApplication::activeWindow();
    auto* dialog = new QDialog(parent);
    g_captureDialog = dialog;
    dialog->setObjectName(QStringLiteral("wrap_chinese_capture_result"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("采集未翻译词条"));
    dialog->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog->setFixedWidth(400);
    if (parent) {
        dialog->setFont(parent->font());
        dialog->setWindowIcon(parent->windowIcon());
    }

    QString message;
    if (saved) {
        message = QStringLiteral("本次新增 %1 条，当前共 %2 条。\n\n保存位置：\n%3")
                      .arg(addedCount)
                      .arg(totalCount)
                      .arg(QDir::toNativeSeparators(savedPath));
        if (!diagnosticsPath.isEmpty())
            message += QStringLiteral("\n\n界面诊断：\n%1").arg(QDir::toNativeSeparators(diagnosticsPath));
        if (usedFallback) {
            message += QStringLiteral(
                "\n\n原文件无法覆盖，已自动保存为备用文件。\n"
                "原文件位置：\n%1")
                           .arg(QDir::toNativeSeparators(requestedPath));
        }
    } else {
        message = QStringLiteral(
                      "采集完成，但文件保存失败。\n\n"
                      "目标位置：\n%1\n\n错误信息：\n%2")
                      .arg(QDir::toNativeSeparators(requestedPath), saveError);
    }

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);
    auto* label = new QLabel(message, dialog);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* accept = new QPushButton(QStringLiteral("确定"), dialog);
    accept->setDefault(true);
    accept->setAutoDefault(true);
    accept->setMinimumWidth(84);
    QObject::connect(accept, &QPushButton::clicked, dialog, &QDialog::accept);
    buttons->addWidget(accept);
    layout->addLayout(buttons);
    dialog->show();
}

void captureUntranslated() {
    QMap<QString, QString> entries;
    const QString path = capturePath();
    bool preserveExisting = false;
    QString existingError;
    const QFileInfo existingInfo(path);
    if (existingInfo.exists()) {
        QFile existing(path);
        if (!existingInfo.isFile() || existingInfo.isSymLink() ||
            existingInfo.size() > kMaximumDictionaryBytes ||
            !existing.open(QIODevice::ReadOnly)) {
            preserveExisting = true;
            existingError = QStringLiteral("原采集目标不是可读取的普通文件");
        } else {
            QJsonParseError parse{};
            const QJsonDocument document =
                QJsonDocument::fromJson(existing.readAll(), &parse);
            const QJsonObject root = document.object();
            const QJsonValue oldValue = root.value(QStringLiteral("translations"));
            if (parse.error != QJsonParseError::NoError || !document.isObject() ||
                root.value(QStringLiteral("$schema")).toString() !=
                    QStringLiteral("sp-translation-v1") ||
                !oldValue.isObject()) {
                preserveExisting = true;
                existingError = QStringLiteral("原采集文件格式无效，已保留原文件");
            } else {
                const QJsonObject old = oldValue.toObject();
                for (auto it = old.constBegin(); it != old.constEnd(); ++it) {
                    if (it.value().isString() && !it.key().trimmed().isEmpty())
                        entries.insert(it.key(), it.value().toString());
                }
            }
        }
    }
    const int previousCount = entries.size();
    const bool includeTranslated = qEnvironmentVariableIntValue(
        "WRAP_CHINESE_CAPTURE_INCLUDE_TRANSLATED") == 1;
    auto add = [&](const QString& source) {
        const QString value = source.trimmed();
        if (!isCandidate(value) ||
            (!includeTranslated && !translated(value).isEmpty())) return;
        if (!entries.contains(value)) entries.insert(value, QString());
    };

    int modelBudget = 200000;
    QSet<QAbstractItemModel*> models;
    QSet<QAction*> visitedActions;
    QSet<QMenu*> visitedMenus;
    std::function<void(QAction*)> collectAction;
    std::function<void(QMenu*)> collectMenu;
    collectAction = [&](QAction* action) {
        if (!action || visitedActions.contains(action)) return;
        visitedActions.insert(action);
        add(action->text());
        add(action->toolTip());
        add(action->statusTip());
        add(action->whatsThis());
        if (QMenu* submenu = action->menu()) collectMenu(submenu);
    };
    collectMenu = [&](QMenu* menu) {
        if (!menu || visitedMenus.contains(menu)) return;
        visitedMenus.insert(menu);
        add(menu->title());

        // Inspect existing actions only; emitting aboutToShow/aboutToHide can
        // run arbitrary host slots and mutate state during a read-only capture.
        for (QAction* action : menu->actions()) collectAction(action);
    };

    std::vector<QPointer<QWidget>> captureWidgets;
    for (auto* widget : QApplication::allWidgets()) captureWidgets.emplace_back(widget);
    for (const auto& guardedWidget : captureWidgets) {
        QWidget* widget = guardedWidget.data();
        if (!widget) continue;
        add(widget->windowTitle());
        add(widget->toolTip());
        add(widget->statusTip());
        add(widget->whatsThis());
        if (auto* label = qobject_cast<QLabel*>(widget)) add(label->text());
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) add(button->text());
        if (auto* edit = qobject_cast<QLineEdit*>(widget))
            add(edit->placeholderText());
        if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            add(combo->currentText());
            for (int index = 0; index < combo->count(); ++index)
                add(combo->itemText(index));
        }
        if (auto* tabs = qobject_cast<QTabBar*>(widget))
            for (int index = 0; index < tabs->count(); ++index) add(tabs->tabText(index));
        if (auto* toolbox = qobject_cast<QToolBox*>(widget))
            for (int index = 0; index < toolbox->count(); ++index) {
                add(toolbox->itemText(index));
                add(toolbox->itemToolTip(index));
            }
        if (auto* group = qobject_cast<QGroupBox*>(widget)) add(group->title());
        if (auto* dock = qobject_cast<QDockWidget*>(widget)) add(dock->windowTitle());
        if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
            QAbstractItemModel* model = view->model();
            if (model && !models.contains(model)) {
                models.insert(model);
                const int columns = model->columnCount();
                for (int column = 0; column < columns; ++column)
                    add(model->headerData(column, Qt::Horizontal,
                                          Qt::DisplayRole).toString());
                collectModel(model, QModelIndex(), add, 0, modelBudget);
            }
        }
        if (auto* menu = qobject_cast<QMenu*>(widget)) collectMenu(menu);
        for (QAction* action : widget->actions()) collectAction(action);
    }
    for (QWidget* topLevel : QApplication::topLevelWidgets())
        for (QAction* action : topLevel->findChildren<QAction*>())
            collectAction(action);

    QJsonObject translations;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
        translations.insert(it.key(), it.value());
    QJsonObject root{
        {QStringLiteral("$schema"), QStringLiteral("sp-translation-v1")},
        {QStringLiteral("id"), QStringLiteral("wrap-runtime-untranslated")},
        {QStringLiteral("language"), QStringLiteral("zh-CN")},
        {QStringLiteral("description"),
         QStringLiteral("Wrap 运行时增量采集的待审词条")},
        {QStringLiteral("translations"), translations},
    };
    const int addedCount = entries.size() - previousCount;
    bool saved = true;
    bool usedFallback = false;
    QString saveError;
    QString savedPath = path;
    // Create a valid empty capture file on the first run too. Reporting a
    // successful save for a path that was never created is misleading and
    // breaks callers that wait for the capture artifact to appear.
    if (addedCount > 0 || !existingInfo.exists()) {
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        QString firstError;
        saved = !preserveExisting && writeCaptureFile(path, bytes, firstError);
        if (preserveExisting) firstError = existingError;
        if (!saved) {
            savedPath = fallbackCapturePath(path);
            QString fallbackError;
            saved = writeCaptureFile(savedPath, bytes, fallbackError);
            usedFallback = saved;
            if (!saved) {
                saveError = QStringLiteral("原文件：%1\n备用文件：%2")
                                .arg(firstError, fallbackError);
                savedPath.clear();
            }
        }
    } else if (preserveExisting) {
        saved = false;
        savedPath.clear();
        saveError = existingError;
    }

    QString diagnosticsPath;
    if (saved && !savedPath.isEmpty()) {
        const QString candidate = QFileInfo(savedPath).dir().filePath(
            QStringLiteral("Wrap_window_diagnostics.json"));
        const QJsonObject diagnostics = BrushUiDiagnostics::snapshot(QJsonObject{
            {QStringLiteral("engine_version"), QString::fromLatin1(kEngineVersion)},
            {QStringLiteral("translation_enabled"), g_enabled.load(std::memory_order_acquire)},
            {QStringLiteral("proxy_style_decision"), g_proxyStyleDecision},
            {QStringLiteral("dictionary_conflicts"), g_dictionaryConflicts},
            {QStringLiteral("original_style_key"), g_originalStyleKey},
            {QStringLiteral("proxy_style_live"), g_translationStyle && QApplication::style() == g_translationStyle.data()},
            {QStringLiteral("text_hooks_installed"), g_drawHooksInstalled},
            {QStringLiteral("hint_hooks_available"), ShortcutHintDisplay::originalContents &&
                ShortcutHintDisplay::originalPoint && ShortcutHintDisplay::originalRect},
            {QStringLiteral("hint_recipe_count"), int(ShortcutHintDisplay::caches.size())},
        });
        QString diagnosticError;
        if (writeCaptureFile(candidate,
                QJsonDocument(diagnostics).toJson(QJsonDocument::Indented), diagnosticError))
            diagnosticsPath = candidate;
    }
    if (qEnvironmentVariableIntValue("WRAP_CHINESE_QUIET_TEST") == 1) return;
    const int totalCount = entries.size();
    QTimer::singleShot(
        0, QApplication::instance(),
        [addedCount, totalCount, path, savedPath, saved, usedFallback,
         saveError, diagnosticsPath] {
            showCaptureResult(addedCount, totalCount, path, savedPath, saved,
                              usedFallback, saveError, diagnosticsPath);
        });
}

void openHotkeyDialog() {
    if (g_hotkeyDialog) {
        g_hotkeyDialog->raise();
        g_hotkeyDialog->activateWindow();
        return;
    }
    QWidget* parent = g_menuBar ? g_menuBar->window() : nullptr;
    auto* dialog = new WrapChineseHotkeyDialog(
        parent, g_toggleShortcut, g_configPath);
    g_hotkeyDialog = dialog;
    g_hotkeyDialogOpen = true;
    QObject::connect(dialog, &QDialog::finished, [dialog] {
        if (g_hotkeyDialog == dialog) g_hotkeyDialog = nullptr;
        g_hotkeyDialogOpen = false;
    });
    QObject::connect(dialog, &QObject::destroyed, [] {
        g_hotkeyDialogOpen = false;
    });
    dialog->show();
}

class MenuMouseFilter final : public QObject {
public:
    explicit MenuMouseFilter(QMenuBar* bar)
        : QObject(bar), bar_(bar), tracking_(bar->hasMouseTracking()) {
        bar->setMouseTracking(true);
    }
    ~MenuMouseFilter() override {
        if (bar_) { bar_->unsetCursor(); bar_->setMouseTracking(tracking_); }
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        auto* bar = qobject_cast<QMenuBar*>(object);
        if (!bar) return false;
        if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const bool overLink =
                (g_authorAction &&
                 bar->actionGeometry(g_authorAction).contains(mouse->pos())) ||
                (g_githubAction &&
                 bar->actionGeometry(g_githubAction).contains(mouse->pos()));
            if (overLink) bar->setCursor(Qt::PointingHandCursor);
            else bar->unsetCursor();
            return false;
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            bar->unsetCursor();
            return false;
        }
        if (!g_languageAction) return false;
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (bar->actionGeometry(g_languageAction).contains(mouse->pos()) &&
                mouse->button() == Qt::RightButton) {
                openHotkeyDialog();
                return true;
            }
        }
        return false;
    }
private:
    QPointer<QMenuBar> bar_;
    bool tracking_;

};

void patchDrawTextModules();

void installMenuActions() {
    if (g_shuttingDown) return;
    QMenuBar* bar = g_menuBar;
    if (!bar)
    for (QWidget* widget : QApplication::allWidgets()) {
        auto* candidate = qobject_cast<QMenuBar*>(widget);
        bool hasFile = false;
        if (candidate) for (const auto* action : candidate->actions())
            if (withoutMnemonics(action->text()).trimmed() == QLatin1String("File")) hasFile = true;
        if (hasFile) {
            bar = candidate;
            break;
        }
    }
    if (!bar) return;
    QAction* help = nullptr;
    for (QAction* action : bar->actions()) {
        QString label = action->text();
        label.remove(QLatin1Char('&'));
        if (label.contains(QLatin1String("Help"), Qt::CaseInsensitive) ||
            label.contains(QStringLiteral("帮助"))) {
            help = action;
            break;
        }
    }
    if (!g_languageAction) {
        g_languageAction = new QAction(QStringLiteral("中/英"), bar);
        g_languageAction->setObjectName(QStringLiteral("qt_chinese_language"));
        QObject::connect(g_languageAction, &QAction::triggered, [] { toggleLanguage(); });
        bar->addAction(g_languageAction);
    }
    const QString authorUrl =
        QStringLiteral("https://space.bilibili.com/281243426?spm_id_from=333.1007.0.0");
    const QString githubUrl = QStringLiteral("https://github.com/iillya/WrapChinese");
    auto addLink = [&](const QString& label, const QString& url, const QString& name) {
        auto* action = new QAction(label, bar);
        action->setObjectName(name);
        QObject::connect(action, &QAction::triggered,
                         [url] { QDesktopServices::openUrl(QUrl(url)); });
        bar->addAction(action);
        return action;
    };
    if (!g_authorAction)
        g_authorAction = addLink(QStringLiteral("Bilibili 神说要凑数汉化"),
                                 authorUrl, QStringLiteral("qt_chinese_author"));
    if (!g_githubAction)
        g_githubAction = addLink(QStringLiteral("GitHub 仓库"),
                                 githubUrl, QStringLiteral("qt_chinese_github"));
    const auto actions = bar->actions();
    const int helpIndex = actions.indexOf(help);
    const int desired = helpIndex >= 0 ? helpIndex + 1 : actions.size() - 3;
    if (actions.indexOf(g_languageAction) != desired ||
        actions.indexOf(g_authorAction) != desired + 1 || actions.indexOf(g_githubAction) != desired + 2) {
        QAction* anchor = nullptr;
        if (helpIndex >= 0) for (int i = helpIndex + 1; i < actions.size(); ++i) {
            auto* action = actions.at(i);
            if (action != g_languageAction && action != g_authorAction && action != g_githubAction) {
                anchor = action; break;
            }
        }
        for (auto* action : {g_languageAction.data(), g_authorAction.data(), g_githubAction.data()})
            bar->insertAction(anchor, action);
    }
    if (!g_menuFilter) {
        g_menuFilter = new MenuMouseFilter(bar);
        bar->installEventFilter(g_menuFilter);
    }
    g_menuBar = bar;
}

void scheduleMaintenance(bool modules) {
    if (g_shuttingDown) return;
    g_modulesDirty = g_modulesDirty || modules;
    if (!g_maintenanceTimer) {
        g_maintenanceTimer = new QTimer(QCoreApplication::instance());
        g_maintenanceTimer->setSingleShot(true);
        QObject::connect(g_maintenanceTimer, &QTimer::timeout, [] {
            installMenuActions();
            if (g_modulesDirty) {
                if (!g_moduleScanAge.isValid() || g_moduleScanAge.elapsed() >=
                        WrapHookLifecycle::kDeferredScanIntervalMs) {
                    g_modulesDirty = false;
                    patchDrawTextModules();
                    g_moduleScanAge.restart();
                } else {
                    g_maintenanceTimer->start(WrapHookLifecycle::kDeferredScanIntervalMs -
                                             int(g_moduleScanAge.elapsed()));
                }
            }
        });
    }
    if (!g_maintenanceTimer->isActive()) g_maintenanceTimer->start(0);
}

struct RichLabelDocument {
    QString source, target;
    QFont font;
    Qt::Alignment alignment;
    Qt::LayoutDirection direction;
    bool wordWrap;
    int width;
    QTextDocument document;
};
// Keys are never dereferenced; a reused address is checked against every layout
// input. Cost is in KiB and capped, so old/destroyed widgets retain no objects.
QCache<const QLabel*, RichLabelDocument> g_richDocuments(512);
#ifdef WRAP_CHINESE_TEST_HOOKS
unsigned g_richDocumentBuilds = 0;
#endif

bool paintTranslatedRichLabel(QLabel* label) {
    if (!g_enabled.load(std::memory_order_acquire) || !label || typeid(*label) != typeid(QLabel) ||
        label->frameShape() != QFrame::NoFrame ||
        label->indent() > 0 ||
        (label->textInteractionFlags() &
         (Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard)) ||
        label->buddy()) return false;
    const QString source = label->text();
    if (source.contains(QLatin1String("<a"), Qt::CaseInsensitive) ||
        source.contains(QLatin1String("<img"), Qt::CaseInsensitive)) return false;
    if (!(label->textFormat() == Qt::RichText ||
          (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(source))))
        return false;
    const QString target = translated(source);
    if (target.isEmpty() || target == source) return false;

    // Paint a private document into this plain label's widget. The host's
    // QLabel::text(), backing document and layouts retain the source string.
    QRect content = label->contentsRect().adjusted(
        label->margin(), label->margin(),
        -label->margin(), -label->margin());
    if (content.isEmpty()) return false;
    RichLabelDocument* cached = g_richDocuments.object(label);
    if (!cached || cached->source != source || cached->target != target ||
        cached->font != label->font() || cached->alignment != label->alignment() ||
        cached->direction != label->layoutDirection() || cached->wordWrap != label->wordWrap() ||
        cached->width != content.width()) {
        auto* fresh = new RichLabelDocument;
        fresh->source = source; fresh->target = target; fresh->font = label->font();
        fresh->alignment = label->alignment(); fresh->direction = label->layoutDirection();
        fresh->wordWrap = label->wordWrap(); fresh->width = content.width();
        fresh->document.setDocumentMargin(0);
        fresh->document.setDefaultFont(fresh->font);
        QTextOption option;
        option.setAlignment(fresh->alignment);
        option.setTextDirection(fresh->direction);
        option.setWrapMode(fresh->wordWrap ? QTextOption::WordWrap : QTextOption::NoWrap);
        fresh->document.setDefaultTextOption(option);
        fresh->document.setHtml(target);
        fresh->document.setTextWidth(fresh->width);
        const int cost = qMax(1, (source.size() * 2 + target.size() * 2 + 4095) / 1024);
        if (!g_richDocuments.insert(label, fresh, cost)) return false;
        cached = fresh;
#ifdef WRAP_CHINESE_TEST_HOOKS
        ++g_richDocumentBuilds;
#endif
    }
    QTextDocument& display = cached->document;
    const qreal height = display.size().height();
    qreal top = content.top();
    if (label->alignment() & Qt::AlignVCenter)
        top += (content.height() - height) / 2;
    else if (label->alignment() & Qt::AlignBottom)
        top += content.height() - height;
    QPainter painter(label);
    painter.setClipRect(content);
    painter.translate(content.left(), top);
    // QTextDocument draws with its layout palette, not QPainter::pen().
    // Preserve the host foreground color so dark themes and disabled labels
    // remain legible after their private display document is translated.
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = label->palette();
    const QPalette::ColorGroup group = label->isEnabled()
        ? context.palette.currentColorGroup() : QPalette::Disabled;
    context.palette.setColor(QPalette::Text,
        context.palette.color(group, label->foregroundRole()));
    display.documentLayout()->draw(&painter, context);
#ifdef WRAP_CHINESE_TEST_HOOKS
    g_translatedRichLabelDraws.fetch_add(1, std::memory_order_relaxed);
#endif
    return true;
}

class ApplicationFilter final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::KeyRelease) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (g_swallowedShortcutKeys.remove(key->key())) return true;
        }
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) && !g_hotkeyDialogOpen &&
            !QApplication::activeModalWidget()) {
            auto* key = static_cast<QKeyEvent*>(event);
            const bool isTildeKey = key->key() == Qt::Key_AsciiTilde ||
                                    key->key() == Qt::Key_QuoteLeft;
            if (g_captureEnabled && isTildeKey &&
                key->modifiers() == Qt::ShiftModifier) {
                if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
                g_swallowedShortcutKeys.insert(key->key());
                if (!key->isAutoRepeat()) captureUntranslated();
                return true;
            }
            const int combination =
                key->key() | static_cast<int>(key->modifiers());
            if (g_toggleShortcut.count() == 1 &&
                combination == g_toggleShortcut[0]) {
                if (event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
                g_swallowedShortcutKeys.insert(key->key());
                if (WrapChineseConfig::shouldActivateShortcut(
                        g_toggleShortcut, combination, key->isAutoRepeat(),
                        false)) {
                    toggleLanguage();
                }
                return true;
            }
        }
        if (event->type() == QEvent::Show || event->type() == QEvent::Polish ||
            event->type() == QEvent::LayoutRequest || event->type() == QEvent::ActionAdded) {
            if (auto* widget = qobject_cast<QWidget*>(object)) {
                applyWidget(widget);
                if (event->type() == QEvent::Show || event->type() == QEvent::Polish)
                    scheduleMaintenance(true);
                else if (qobject_cast<QMenuBar*>(widget)) scheduleMaintenance(false);
            }
        }
        if (event->type() == QEvent::Paint) {
            if (auto* label = qobject_cast<QLabel*>(object))
                return paintTranslatedRichLabel(label);
        }
        return false;
    }
};

using DrawRect = void (*)(QPainter*, const QRect&, int, const QString&, QRect*);
using DrawRectFOption = void (*)(QPainter*, const QRectF&, const QString&, const QTextOption&);
using DrawRectFAlign = void (*)(QPainter*, const QRectF&, int, const QString&, QRectF*);
using DrawPointF = void (*)(QPainter*, const QPointF&, const QString&);
using DrawPointF2 = void (*)(QPainter*, const QPointF&, const QString&, int, int);

DrawRect g_drawRect = nullptr;
DrawRectFOption g_drawRectFOption = nullptr;
DrawRectFAlign g_drawRectFAlign = nullptr;
DrawPointF g_drawPointF = nullptr;
DrawPointF2 g_drawPointF2 = nullptr;
thread_local bool g_inDrawHook = false;

QString translatePaint(QPainter* painter, const QString& text) {
    // Widget-owned image caches are display output. Unscoped images, pictures
    // and printers may contain exported data and keep their source text.
    if (g_inDrawHook || !g_enabled.load(std::memory_order_acquire) ||
        text.size() < 2 || !isDisplayPaintDevice(painter))
        return {};
    bool valueOnly = true;
    for (const QChar ch : text) {
        if (ch.isDigit() || ch.isSpace() || QStringLiteral(".,%+-eE:/\\").contains(ch))
            continue;
        valueOnly = false;
        break;
    }
    if (valueOnly) return {};
    const auto* widget = painter->device()->devType() == QInternal::Widget
        ? static_cast<const QWidget*>(painter->device()) : UiPaintBoundary::displayWidget();
    const bool truncated = text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1String("..."));
    if (truncated) if (const auto* label = qobject_cast<const QLabel*>(widget)) {
        const QString full = label->text();
        if (full != text && !full.isEmpty() && label->textFormat() != Qt::RichText &&
            (label->textFormat() == Qt::PlainText || !Qt::mightBeRichText(full))) {
            const QString target = translated(full);
            if (!target.isEmpty()) return target;
        }
    }
    // A truncated painter fragment cannot identify the original UI label.
    // Only the verified full QLabel source above may recover it.
    if (truncated) return {};
    return translated(text);
}

class DrawGuard {
public:
    DrawGuard() : previous_(g_inDrawHook) { g_inDrawHook = true; }
    ~DrawGuard() { g_inDrawHook = previous_; }
private:
    bool previous_;
};

void hookedDrawRect(QPainter* painter, const QRect& rect, int flags,
                    const QString& text, QRect* bounds) {
    const QString value = translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawRect) g_drawRect(painter, rect, flags, value.isEmpty() ? text : value, bounds);
}

void hookedDrawRectFOption(QPainter* painter, const QRectF& rect,
                           const QString& text, const QTextOption& option) {
    const QString value = translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawRectFOption)
        g_drawRectFOption(painter, rect, value.isEmpty() ? text : value, option);
}

void hookedDrawRectFAlign(QPainter* painter, const QRectF& rect, int flags,
                          const QString& text, QRectF* bounds) {
    const QString value = translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawRectFAlign)
        g_drawRectFAlign(painter, rect, flags, value.isEmpty() ? text : value, bounds);
}

void hookedDrawPointF(QPainter* painter, const QPointF& point, const QString& text) {
    const QString value = translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawPointF) g_drawPointF(painter, point, value.isEmpty() ? text : value);
}

void hookedDrawPointF2(QPainter* painter, const QPointF& point,
                       const QString& text, int textFlags, int justificationPadding) {
    // BypassShaping selects a single Common-script font engine in Qt 5 and
    // cannot guarantee Chinese font fallback. Preserve the English in that
    // specialized path rather than altering the host flags or losing glyphs.
    const QString value = (textFlags & Qt::TextBypassShaping)
        ? QString() : translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawPointF2)
        g_drawPointF2(painter, point, value.isEmpty() ? text : value,
                      textFlags, justificationPadding);
}

std::unordered_set<HMODULE> g_drawScannedModules;
std::vector<WrapImportHooks::PatchRecord> g_drawPatches;

void patchDrawTextModules() {
    const WrapImportHooks::Replacement symbols[] = {
        {functionAddress(g_drawRect), functionAddress(&hookedDrawRect)},
        {functionAddress(g_drawRectFOption), functionAddress(&hookedDrawRectFOption)},
        {functionAddress(g_drawRectFAlign), functionAddress(&hookedDrawRectFAlign)},
        {functionAddress(g_drawPointF), functionAddress(&hookedDrawPointF)},
        {functionAddress(g_drawPointF2), functionAddress(&hookedDrawPointF2)},
    };
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;
    std::unordered_set<HMODULE> currentModules;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsnicmp(entry.szModule, kHookPrefix, wcslen(kHookPrefix)) == 0 ||
                _wcsicmp(entry.szModule, kCoreModule) == 0) continue;
            HMODULE module = GetModuleHandleW(entry.szModule);
            if (!module) continue;
            currentModules.insert(module);
            if (!g_drawScannedModules.insert(module).second) continue;
            replaceImports(module, symbols, sizeof(symbols) / sizeof(symbols[0]), &g_drawPatches);
            ShortcutHintDisplay::patchModule(module);
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    for (auto it = g_drawScannedModules.begin();
         it != g_drawScannedModules.end();) {
        if (!currentModules.count(*it)) it = g_drawScannedModules.erase(it);
        else ++it;
    }
}

void installDrawTextHooks() {
    if (g_drawHooksInstalled) return;
    HMODULE gui = GetModuleHandleW(kGuiModule);
    if (!gui) return;
    ShortcutHintDisplay::resolveHooks(gui);
    g_drawRect = resolveFunction<DrawRect>(
        gui, "_ZN8QPainter8drawTextERK5QRectiRK7QStringPS0_");
    g_drawRectFOption = resolveFunction<DrawRectFOption>(
        gui, "_ZN8QPainter8drawTextERK6QRectFRK7QStringRK11QTextOption");
    g_drawRectFAlign = resolveFunction<DrawRectFAlign>(
        gui, "_ZN8QPainter8drawTextERK6QRectFiRK7QStringPS0_");
    g_drawPointF = resolveFunction<DrawPointF>(
        gui, "_ZN8QPainter8drawTextERK7QPointFRK7QString");
    g_drawPointF2 = resolveFunction<DrawPointF2>(
        gui, "_ZN8QPainter8drawTextERK7QPointFRK7QStringii");
    g_drawHooksInstalled = g_drawRect || g_drawRectFOption || g_drawRectFAlign ||
                           g_drawPointF || g_drawPointF2;
    if (g_drawHooksInstalled) { patchDrawTextModules(); g_moduleScanAge.start(); }
}

void installProxyStyle() {
    WidgetDisplayMetrics::install(translatedForMeasurement);
    if (g_proxyStyleInstalled || !QApplication::instance()) return;
    QStyle* current = QApplication::style();
    if (!current) {
        g_proxyStyleDecision = QStringLiteral("skipped-no-host-style");
        return;
    }
    // Never reconstruct a stylesheet wrapper or a custom product style as
    // Fusion. Painter/delegate/metric hooks still provide display translation.
    if (!qApp->styleSheet().isEmpty() || current->inherits("QStyleSheetStyle")) {
        g_proxyStyleDecision = QStringLiteral("skipped-host-stylesheet");
        return;
    }
    const QString key = current->objectName();
    if (key.isEmpty() || !QStyleFactory::keys().contains(key, Qt::CaseInsensitive)) {
        g_proxyStyleDecision = QStringLiteral("skipped-unrecreatable-host-style");
        return;
    }
    std::unique_ptr<QStyle> base(QStyleFactory::create(key));
    if (!base || typeid(*base) != typeid(*current)) {
        g_proxyStyleDecision = QStringLiteral("skipped-custom-host-style");
        return;
    }
    g_originalStyleKey = key;
    g_originalPalette = std::make_unique<QPalette>(QApplication::palette());
    auto* display = new ChineseProxyStyle(base.release());
    QApplication::setStyle(display);
    g_translationStyle = display;
    g_proxyStyleInstalled = true;
    g_proxyStyleDecision = QStringLiteral("installed-recreated-native-style");
}

void shutdownDisplay() {
    if (g_shuttingDown) return;
    g_shuttingDown = true;
    g_enabled.store(false, std::memory_order_release);
    if (g_maintenanceTimer) { g_maintenanceTimer->stop(); delete g_maintenanceTimer.data(); }
    if (g_applicationFilter && QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(g_applicationFilter);
    delete g_applicationFilter.data();
    if (g_menuBar && g_menuFilter) g_menuBar->removeEventFilter(g_menuFilter);
    for (auto* action : {g_languageAction.data(), g_authorAction.data(), g_githubAction.data()})
        delete action;
    if (g_hotkeyDialog) g_hotkeyDialog->close();
    if (g_captureDialog) g_captureDialog->close();
    for (const auto& item : g_delegates) {
        if (item.view && item.original && item.view->itemDelegate() == item.display) {
            item.view->setItemDelegate(item.original);
            delete item.display.data();
        }
    }
    g_delegates.clear();
    ShortcutHintDisplay::shutdown();
    WidgetDisplayMetrics::shutdown();
    WrapImportHooks::restoreImports(g_drawPatches);
    g_drawScannedModules.clear();
    g_drawHooksInstalled = false;
    if (g_translationStyle && QApplication::style() == g_translationStyle.data()) {
        if (auto* native = QStyleFactory::create(g_originalStyleKey)) {
            QApplication::setStyle(native);
            if (g_originalPalette) QApplication::setPalette(*g_originalPalette);
        }
    }
    g_translationStyle = nullptr;
    // Restoring QStyle sends StyleChange; Qt may reset mouse tracking there.
    // Restore our recorded menu state after that native style processing.
    delete g_menuFilter.data();
    g_proxyStyleInstalled = false;
    g_originalPalette.reset();
    g_richDocuments.clear();
    g_lookupCache.clear();
    g_measurementCache.clear();
    g_swallowedShortcutKeys.clear();
    repaintAll();
}

void initializeOnGuiThread(QApplication* application) {
    g_pluginDir = selfDirectory();
    if (g_pluginDir.isEmpty()) return;
    loadDictionaries();
    ShortcutHintDisplay::initialize(exactTranslation, &g_enabled);
    QObject::connect(application, &QCoreApplication::aboutToQuit, [] { shutdownDisplay(); });
    QObject::connect(application, &QObject::destroyed,
                     [] { ShortcutHintDisplay::clearCaches(); g_richDocuments.clear(); });
    installDrawTextHooks();
    QString error;
    g_configPath = qEnvironmentVariable("WRAP_CHINESE_CONFIG_PATH");
    if (g_configPath.isEmpty())
        g_configPath = WrapChineseConfig::path(QStringLiteral("WrapChinese"));
    const WrapChineseConfig::Values config =
        WrapChineseConfig::load(g_configPath, &error);
    g_toggleShortcut = config.toggleShortcut;
    g_captureEnabled = config.captureEnabled;
    installProxyStyle();
    for (QWidget* widget : QApplication::allWidgets()) applyWidget(widget);
    g_applicationFilter = new ApplicationFilter(application);
    application->installEventFilter(g_applicationFilter);
    installMenuActions();
    repaintAll();

#ifdef WRAP_CHINESE_DEVELOPER_TOOLS
    if (!qEnvironmentVariable("WRAP_CHINESE_WIDGET_INVENTORY_PATH").isEmpty())
        QTimer::singleShot(2500, application, [] { writeWidgetInventory(); });

#endif

    if (WrapHookLifecycle::kEnableDeferredPolling) {
        auto* timer = new QTimer(application);
        timer->setInterval(WrapHookLifecycle::kDeferredScanIntervalMs);
        QObject::connect(timer, &QTimer::timeout, [] {
            patchDrawTextModules();
            installMenuActions();
        });
        timer->start();
        QTimer::singleShot(WrapHookLifecycle::kDeferredScanWindowMs,
                           application, [timer] {
            timer->stop();
            timer->deleteLater();
        });
    }
    signalHookReady();
#ifdef WRAP_CHINESE_DEVELOPER_TOOLS
    const QString probeNodeType =
        qEnvironmentVariable("WRAP_CHINESE_PROBE_NODE_TYPE");
    const QString probePlanPath =
        qEnvironmentVariable("WRAP_CHINESE_PROBE_PLAN_PATH");
    if (!probePlanPath.isEmpty()) {
        startNodeProbePlan(application, probePlanPath);
    }
    else if (!probeNodeType.isEmpty()) {
        startNodeProbe(application, probeNodeType,
                       [application](bool) { application->quit(); });
    }
    else if (qEnvironmentVariableIntValue("WRAP_CHINESE_AUTO_CAPTURE") == 1) {
        QTimer::singleShot(3000, application, [] { captureUntranslated(); });
        QTimer::singleShot(8000, application, [] { captureUntranslated(); });
        if (qEnvironmentVariableIntValue("WRAP_CHINESE_SMOKE_TEST") == 1) {
            // Test-only, graceful exit. This makes compatibility verification
            // fully unattended without terminating an unrelated Wrap process.
            QTimer::singleShot(10000, application,
                               [application] { application->quit(); });
        }
    }
#endif
}

DWORD WINAPI worker(LPVOID) {
    if (!WrapChineseCompatibility::supportsRuntimeVersion(qVersion()) ||
        !GetModuleHandleW(L"Qt5Core.dll") ||
        !GetModuleHandleW(L"Qt5Gui.dll") ||
        !GetModuleHandleW(L"Qt5Widgets.dll")) {
        return 3;
    }
    QApplication* application = nullptr;
    for (int attempt = 0;
         attempt < WrapHookLifecycle::kApplicationWaitAttempts; ++attempt) {
        auto* instance = QCoreApplication::instance();
        if (instance) {
            application = qobject_cast<QApplication*>(instance);
            if (application) break;
        }
        Sleep(WrapHookLifecycle::kApplicationWaitIntervalMs);
    }
    if (!application) return 1;
    return QMetaObject::invokeMethod(
        application, [application] { initializeOnGuiThread(application); },
        Qt::QueuedConnection) ? 0 : 2;
}

} // namespace

extern "C" __declspec(dllexport) const char* WrapChineseVersion() {
    return kEngineVersion;
}

#ifdef WRAP_CHINESE_TEST_HOOKS
extern "C" __declspec(dllexport) unsigned int WrapChineseTestRichLabelDraws() {
    return g_translatedRichLabelDraws.load(std::memory_order_relaxed);
}
extern "C" __declspec(dllexport) unsigned int WrapChineseTestRichDocumentBuilds() {
    return g_richDocumentBuilds;
}
extern "C" __declspec(dllexport) void WrapChineseTestShutdown() { shutdownDisplay(); }
extern "C" __declspec(dllexport) void WrapChineseTestShutdownHintHooks() {
    ShortcutHintDisplay::shutdown();
}
#endif

extern "C" __declspec(dllexport) DWORD WINAPI WrapChineseInitialize() {
    bool expected = false;
    if (!g_initializationStarted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) return 1;
    // Patched imports and Qt callbacks live until process shutdown. The host
    // must not unload their code via a later FreeLibrary/plugin refresh.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(g_selfModule), &pinned)) {
        g_initializationStarted.store(false, std::memory_order_release);
        return 0;
    }
    HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (!thread) {
        g_initializationStarted.store(false, std::memory_order_release);
        return 0;
    }
    CloseHandle(thread);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = instance;
    }
    return TRUE;
}
