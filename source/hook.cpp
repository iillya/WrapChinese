// Wrap Qt 5 Chinese display-layer localizer.
//
// The host executable and project data remain untouched. A launcher injects
// this MinGW/Qt 5 DLL before Wrap starts. Translation is applied at Qt's
// translator, style, item-delegate and QPainter boundaries.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "hook_lifecycle.h"
#include "hotkey_settings.h"
#include "qt_placeholder_templates.h"
#include "runtime_compatibility.h"
#include "translation_lookup_cache.h"

#include <QtCore/QAbstractItemModel>
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
#include <QtCore/QMetaMethod>
#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QTranslator>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QFontMetrics>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QTextOption>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QShortcut>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QToolBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kHookPrefix[] = L"WrapChineseHook";
constexpr wchar_t kGuiModule[] = L"Qt5Gui.dll";
constexpr wchar_t kCoreModule[] = L"Qt5Core.dll";
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
bool g_fontMetricHooksInstalled = false;
bool g_proxyStyleInstalled = false;
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

std::unordered_map<std::string, std::string> g_translations;
std::unordered_map<std::string, std::string> g_normalized;
std::vector<std::pair<std::string, std::string>> g_sortedKeys;

WrapChinesePlaceholders::Catalog g_templates;
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
            (value >= 0xff00 && value <= 0xffef) ||
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
    text = mnemonicFree.toCaseFolded().normalized(QString::NormalizationForm_C);
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isSpace()) {
            if (!result.isEmpty() && !result.endsWith(QLatin1Char(' ')))
                result += QLatin1Char(' ');
            continue;
        }
        result.append(ch);
    }
    return result.trimmed();
}

void loadDictionaries() {
    g_translations.clear();
    g_normalized.clear();
    g_sortedKeys.clear();
    g_templates.clear();
    g_lookupCache.clear();
    QDir directory(g_pluginDir + QStringLiteral("/translations"));
    QStringList files = directory.entryList(
        {QStringLiteral("*_zh.json")}, QDir::Files, QDir::Name);
    if (files.size() > kMaximumDictionaryFiles)
        files = files.mid(0, kMaximumDictionaryFiles);
    for (const QString& name : files) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
            file.size() > kMaximumDictionaryBytes) continue;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject root = document.object();
        const QJsonValue translationValue =
            root.value(QStringLiteral("translations"));
        if (root.value(QStringLiteral("$schema")).toString() !=
                QStringLiteral("sp-translation-v1") ||
            root.value(QStringLiteral("language")).toString() !=
                QStringLiteral("zh-CN") ||
            !translationValue.isObject()) continue;
        const QJsonObject translations = translationValue.toObject();
        for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
            if (g_translations.size() >= kMaximumDictionaryEntries) break;
            if (!it.value().isString() || it.key().trimmed().isEmpty() ||
                it.key().size() > kMaximumDictionarySourceLength) continue;
            const QString target = it.value().toString();
            if (target.trimmed().isEmpty() ||
                target.size() > kMaximumDictionaryTargetLength) continue;
            std::vector<WrapChinesePlaceholders::Token> templateTokens;
            QHash<QString, int> templateFields;
            if (WrapChinesePlaceholders::parsePattern(
                    it.key(), templateTokens, templateFields)) {
                WrapChinesePlaceholders::Catalog validator;
                if (!validator.addOrAssign(it.key(), target)) continue;
            }
            g_translations[it.key().toUtf8().toStdString()] =
                target.toUtf8().toStdString();
        }
    }
    std::vector<std::pair<std::string, std::string>> entries(
        g_translations.begin(), g_translations.end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    std::unordered_set<std::string> ambiguousNormalized;
    for (const auto& entry : entries) {
        const QString source = QString::fromUtf8(entry.first.data(), int(entry.first.size()));
        const QString target = QString::fromUtf8(entry.second.data(), int(entry.second.size()));
        const std::string folded = normalize(source).toUtf8().toStdString();
        if (!folded.empty()) {
            const auto found = g_normalized.find(folded);
            if (found == g_normalized.end()) g_normalized.emplace(folded, entry.second);
            else if (found->second != entry.second) ambiguousNormalized.insert(folded);
        }
        std::string lower = source.toLower().toUtf8().toStdString();
        g_sortedKeys.emplace_back(std::move(lower), entry.first);
        if (source.contains(QLatin1Char('%')))
            g_templates.addOrAssign(source, target);
    }
    for (const std::string& key : ambiguousNormalized) g_normalized.erase(key);
    g_templates.finalize();
    std::sort(g_sortedKeys.begin(), g_sortedKeys.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
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
    const QByteArray utf8 = source.toUtf8();
    const auto found = g_translations.find(utf8.toStdString());
    return found == g_translations.end()
        ? QString()
        : QString::fromUtf8(found->second.data(), int(found->second.size()));
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
    QChar tagQuote;
    bool changed = false;
    const auto flushToken = [&] {
        if (token.isEmpty()) return;
        const QString target = isUpperCamelIdentifier(token)
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
            tagQuote = QChar();
            result += ch;
        } else if (inTag && tagQuote.isNull() &&
                   (ch == QLatin1Char('\'') || ch == QLatin1Char('"'))) {
            tagQuote = ch;
            result += ch;
        } else if (inTag && !tagQuote.isNull() && ch == tagQuote) {
            tagQuote = QChar();
            result += ch;
        } else if (inTag && tagQuote.isNull() && ch == QLatin1Char('>')) {
            inTag = false;
            result += ch;
        } else if (!inTag && !inEntity && ch == QLatin1Char('&')) {
            flushToken();
            inEntity = true;
            result += ch;
        } else if (inEntity && ch == QLatin1Char(';')) {
            inEntity = false;
            result += ch;
        } else if (inTag || inEntity) {
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

QString translatedUncached(const QString& raw) {
    if (raw.size() > kMaximumDictionarySourceLength) return {};
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
    const auto exact = g_translations.find(key.toUtf8().toStdString());
    if (exact != g_translations.end())
        return decorate(QString::fromUtf8(exact->second.data(),
                                          int(exact->second.size())));
    const QString rich = translatedRichIdentifiers(key);
    if (!rich.isEmpty()) return decorate(rich);

    QString suffix;
    if (key.endsWith(QLatin1Char('*'))) {
        key.chop(1);
        key = key.trimmed();
        suffix = QStringLiteral("*");
    }
    const QString mnemonic = withoutMnemonics(key);
    const auto clean = g_translations.find(mnemonic.toUtf8().toStdString());
    if (clean != g_translations.end())
        return decorate(QString::fromUtf8(clean->second.data(),
                                          int(clean->second.size())) + suffix);

    const QString dynamic = g_templates.lookup(mnemonic);
    if (!dynamic.isEmpty()) return decorate(dynamic + suffix);
    if (containsCjk(mnemonic)) return {};

    const std::string folded = normalize(mnemonic).toUtf8().toStdString();
    const auto fuzzy = g_normalized.find(folded);
    if (fuzzy != g_normalized.end())
        return decorate(QString::fromUtf8(fuzzy->second.data(),
                                          int(fuzzy->second.size())) + suffix);
    return {};
}

QString translated(const QString& raw) {
    if (!g_enabled.load(std::memory_order_relaxed)) return {};
    return g_lookupCache.lookup(raw, [&] { return translatedUncached(raw); });
}

QString recoverElided(const QString& text) {
    if (!g_enabled.load(std::memory_order_relaxed)) return {};
    if (!(text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1String("..."))))
        return {};
    QString head = text;
    head.chop(text.endsWith(QChar(0x2026)) ? 1 : 3);
    head = head.trimmed().toLower();
    if (head.size() < 2) return {};
    const std::string prefix = head.toUtf8().toStdString();
    auto it = std::lower_bound(
        g_sortedKeys.begin(), g_sortedKeys.end(), prefix,
        [](const auto& entry, const std::string& value) { return entry.first < value; });
    QString result;
    for (; it != g_sortedKeys.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        const auto found = g_translations.find(it->second);
        if (found == g_translations.end()) continue;
        const QString candidate = QString::fromUtf8(
            found->second.data(), int(found->second.size()));
        if (result.isEmpty()) result = candidate;
        else if (result != candidate) return {};
    }
    return result;
}

class DictionaryTranslator final : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return g_translations.empty(); }
    QString translate(const char*, const char* sourceText,
                      const char*, int) const override {
        if (!sourceText) return {};
        return translated(QString::fromUtf8(sourceText));
    }
};

class ChineseProxyStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawItemText(QPainter* painter, const QRect& rect, int flags,
                      const QPalette& palette, bool enabled, const QString& text,
                      QPalette::ColorRole role = QPalette::NoRole) const override {
        const QString value = translated(text);
        const QString draw = value.isEmpty() ? text : value;
        if (text == QStringLiteral("Bilibili 神说要凑数汉化") ||
            text == QStringLiteral("GitHub 仓库")) {
            QPalette linkPalette = palette;
            const QColor blue(0x66, 0xaa, 0xff);
            linkPalette.setColor(QPalette::WindowText, blue);
            linkPalette.setColor(QPalette::ButtonText, blue);
            QProxyStyle::drawItemText(painter, rect, flags, linkPalette,
                                      enabled, draw, role);
            return;
        }
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

constexpr char kLabelSourceProperty[] = "wrapChineseLabelSource";
constexpr char kLabelAppliedProperty[] = "wrapChineseLabelApplied";

void applyLabelText(QLabel* label) {
    const QString current = label->text();
    const QVariant savedSource = label->property(kLabelSourceProperty);
    const QString lastApplied = label->property(kLabelAppliedProperty).toString();
    QString source;
    if (!savedSource.isValid() || current != lastApplied) {
        // The host changed the label since our previous pass. Treat the new
        // value as authoritative so dynamic node selections stay current.
        source = current;
        label->setProperty(kLabelSourceProperty, source);
    } else {
        source = savedSource.toString();
    }
    const QString target = translated(source);
    const QString display = target.isEmpty() ? source : target;
    if (lastApplied != display)
        label->setProperty(kLabelAppliedProperty, display);
    if (current != display) label->setText(display);
}

void applyWidget(QWidget* widget) {
    if (!widget) return;
    if (auto* label = qobject_cast<QLabel*>(widget)) applyLabelText(label);
    auto* view = qobject_cast<QAbstractItemView*>(widget);
    if (!view || view->property("wrapChineseDelegateInstalled").toBool()) return;
    auto* delegate = view->itemDelegate();
    if (!delegate || QString::fromLatin1(delegate->metaObject()->className()) !=
                         QLatin1String("QStyledItemDelegate")) return;
    view->setItemDelegate(new TranslateDelegate(view));
    view->setProperty("wrapChineseDelegateInstalled", true);
}

void repaintAll() {
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget) continue;
        widget->updateGeometry();
        if (widget->isVisible()) widget->update();
    }
}

void refreshMeasuredLayouts() {
    // Some Wrap controls cache their label width before the injected hook gets
    // its first GUI-thread turn. FontChange clears those private metric caches;
    // invalidating parent layouts then makes the translated width take effect
    // immediately instead of waiting for a later panel interaction.
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) applyWidget(widget);
    for (QWidget* widget : widgets) {
        if (!widget) continue;
        QEvent fontChange(QEvent::FontChange);
        QApplication::sendEvent(widget, &fontChange);
    }
    for (QWidget* widget : widgets) {
        if (!widget) continue;
        if (auto* menuBar = qobject_cast<QMenuBar*>(widget)) {
            QEvent styleChange(QEvent::StyleChange);
            QApplication::sendEvent(menuBar, &styleChange);
            continue;
        }
        if (qobject_cast<QMenu*>(widget)) continue;
        widget->updateGeometry();
        if (QLayout* layout = widget->layout()) layout->invalidate();
    }
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget || !widget->isVisible()) continue;
        if (QLayout* layout = widget->layout()) layout->activate();
    }
    repaintAll();
}

void toggleLanguage() {
    g_enabled.store(!g_enabled.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
    refreshMeasuredLayouts();
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
    // Some tree models do not create the children of a collapsed branch until
    // fetchMore() is requested. Hydrate each branch with a bounded loop so the
    // sniffer sees those labels without visibly opening the tree.
    for (int pass = 0; pass < 16 && model->canFetchMore(parent) && budget > 0;
         ++pass) {
        const int oldRows = model->rowCount(parent);
        model->fetchMore(parent);
        if (model->rowCount(parent) <= oldRows) break;
    }
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
QWidget* findGraphView();

void writeWidgetInventory() {
    const QString path = qEnvironmentVariable(
        "WRAP_CHINESE_WIDGET_INVENTORY_PATH");
    if (path.isEmpty()) return;
    QJsonArray widgets;
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget) continue;
        const QRect geometry = widget->geometry();
        QJsonObject item{
            {QStringLiteral("class"),
             QString::fromLatin1(widget->metaObject()->className())},
            {QStringLiteral("object_name"), widget->objectName()},
            {QStringLiteral("title"), widget->windowTitle()},
            {QStringLiteral("visible"), widget->isVisible()},
            {QStringLiteral("enabled"), widget->isEnabled()},
            {QStringLiteral("x"), geometry.x()},
            {QStringLiteral("y"), geometry.y()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
        };
        if (QWidget* parent = widget->parentWidget()) {
            item.insert(QStringLiteral("parent_class"),
                        QString::fromLatin1(parent->metaObject()->className()));
            item.insert(QStringLiteral("parent_object_name"),
                        parent->objectName());
        }
        widgets.append(item);
    }
    QJsonObject root{
        {QStringLiteral("schema"),
         QStringLiteral("wrap-widget-inventory-v1")},
        {QStringLiteral("widgets"), widgets},
    };
    if (QWidget* graph = findGraphView()) {
        QJsonArray metaObjects;
        for (QObject* object = graph; object; object = object->parent()) {
            const QMetaObject* meta = object->metaObject();
            QJsonArray methods;
            for (int index = meta->methodOffset(); index < meta->methodCount(); ++index)
                methods.append(QString::fromLatin1(
                    meta->method(index).methodSignature()));
            metaObjects.append(QJsonObject{
                {QStringLiteral("class"),
                 QString::fromLatin1(meta->className())},
                {QStringLiteral("methods"), methods},
            });
        }
        root.insert(QStringLiteral("graph_meta_objects"), metaObjects);

        QJsonArray shortcuts;
        for (QShortcut* shortcut : graph->window()->findChildren<QShortcut*>()) {
            shortcuts.append(QJsonObject{
                {QStringLiteral("key"), shortcut->key().toString()},
                {QStringLiteral("enabled"), shortcut->isEnabled()},
                {QStringLiteral("context"), static_cast<int>(shortcut->context())},
            });
        }
        root.insert(QStringLiteral("shortcuts"), shortcuts);

        QSet<QAction*> seenActions;
        QJsonArray actions;
        for (QWidget* widget : QApplication::allWidgets()) {
            if (!widget || widget->window() != graph->window()) continue;
            for (QAction* action : widget->actions()) {
                if (!action || seenActions.contains(action)) continue;
                seenActions.insert(action);
                QJsonArray menuActions;
                if (QMenu* menu = action->menu()) {
                    for (QAction* child : menu->actions())
                        menuActions.append(child ? child->text() : QString());
                }
                actions.append(QJsonObject{
                    {QStringLiteral("text"), action->text()},
                    {QStringLiteral("object_name"), action->objectName()},
                    {QStringLiteral("shortcut"), action->shortcut().toString()},
                    {QStringLiteral("enabled"), action->isEnabled()},
                    {QStringLiteral("owner_class"),
                     QString::fromLatin1(widget->metaObject()->className())},
                    {QStringLiteral("menu_actions"), menuActions},
                });
            }
        }
        root.insert(QStringLiteral("actions"), actions);
    }
    QString error;
    writeCaptureFile(path,
        QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

QWidget* findGraphView() {
    constexpr char kGraphViewClass[] =
        "Wrap::WGraphViewportItems::GraphicsView";
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget && widget->isVisible() &&
            std::strcmp(widget->metaObject()->className(), kGraphViewClass) == 0)
            return widget;
    }
    return nullptr;
}

bool openNodeMenu(QWidget* graphWidget) {
    if (!graphWidget) return false;
    auto* graph = qobject_cast<QGraphicsView*>(graphWidget);
    QWidget* target = graph ? graph->viewport() : graphWidget;
    QPoint local = target->rect().center();
    if (graph) {
        bool foundBlank = false;
        for (int y = 40; y < target->height() - 40 && !foundBlank; y += 55) {
            for (int x = 40; x < target->width() - 40; x += 70) {
                const QPoint candidate(x, y);
                if (graph->items(candidate).isEmpty()) {
                    local = candidate;
                    foundBlank = true;
                    break;
                }
            }
        }
        if (!foundBlank) {
            const QRectF occupied = graph->scene()->itemsBoundingRect();
            const QPointF blankScene(occupied.right() + 300.0,
                                     occupied.center().y());
            QRectF sceneRect = graph->sceneRect();
            sceneRect |= QRectF(blankScene.x() - target->width() / 2.0,
                                blankScene.y() - target->height() / 2.0,
                                target->width(), target->height());
            graph->setSceneRect(sceneRect);
            graph->centerOn(blankScene);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            local = target->rect().center();
        }
    }
    const QPoint global = target->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local),
                        QPointF(global), Qt::RightButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    const QPoint graphLocal = graphWidget->mapFromGlobal(global);
    QContextMenuEvent context(QContextMenuEvent::Mouse, graphLocal, global);
    return QApplication::sendEvent(graphWidget, &context);
}

QAction* findNodeMenuAction(QMenu* menu, const QString& nodeType) {
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (!action) continue;
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (!action->menu() &&
            text.trimmed().compare(nodeType, Qt::CaseInsensitive) == 0)
            return action;
        if (QAction* match = findNodeMenuAction(action->menu(), nodeType))
            return match;
    }
    return nullptr;
}

void writeProbeStatus(const QString& nodeType, bool success,
                      int beforeItems, int afterItems,
                      const QString& menuClass, const QString& error) {
    const QString path =
        qEnvironmentVariable("WRAP_CHINESE_PROBE_STATUS_PATH");
    if (path.isEmpty()) return;
    const QJsonObject root{
        {QStringLiteral("schema"), QStringLiteral("wrap-node-probe-status-v1")},
        {QStringLiteral("node_type"), nodeType},
        {QStringLiteral("success"), success},
        {QStringLiteral("scene_items_before"), beforeItems},
        {QStringLiteral("scene_items_after"), afterItems},
        {QStringLiteral("menu_class"), menuClass},
        {QStringLiteral("error"), error},
    };
    QString writeError;
    writeCaptureFile(path,
        QJsonDocument(root).toJson(QJsonDocument::Indented), writeError);
}

void startNodeProbe(QApplication* application, const QString& nodeType,
                    const std::function<void(bool)>& completion) {
    QTimer::singleShot(1000, application, [application, nodeType, completion] {
        QWidget* graphWidget = findGraphView();
        auto* graph = qobject_cast<QGraphicsView*>(graphWidget);
        if (!graphWidget || !graph || !graph->scene()) {
            writeProbeStatus(nodeType, false, -1, -1, QString(),
                             QStringLiteral("graph view was not found"));
            completion(false);
            return;
        }
        const int beforeItems = graph->scene()->items().size();
        graphWidget->setFocus(Qt::OtherFocusReason);
        // Keep the temporary node menu in Wrap's canonical English so its
        // leaf actions can be selected deterministically by node type.
        g_enabled.store(false, std::memory_order_relaxed);

        QTimer::singleShot(200, application,
                           [application, nodeType, graph, beforeItems,
                            completion] {
            auto* nodeMenu =
                qobject_cast<QMenu*>(QApplication::activePopupWidget());
            const QString menuClass = nodeMenu
                ? QString::fromLatin1(nodeMenu->metaObject()->className())
                : QString();
            QAction* nodeAction = findNodeMenuAction(nodeMenu, nodeType);
            if (!nodeAction) {
                g_enabled.store(true, std::memory_order_relaxed);
                writeWidgetInventory();
                writeProbeStatus(nodeType, false, beforeItems, beforeItems,
                                 menuClass,
                                 QStringLiteral("node action was not found in the expanded menu"));
                if (nodeMenu) nodeMenu->close();
                completion(false);
                return;
            }
            nodeAction->trigger();
            // Programmatic QAction activation does not automatically unwind
            // every parent popup the way a physical click does.
            for (QWidget* widget : QApplication::allWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(widget)) {
                    if (menu->isVisible()) menu->close();
                }
            }
            g_enabled.store(true, std::memory_order_relaxed);
            QTimer::singleShot(900, application,
                               [application, nodeType, graph, beforeItems,
                                menuClass, completion] {
                    const int afterItems = graph && graph->scene()
                        ? graph->scene()->items().size() : -1;
                    const bool success = afterItems > beforeItems;
                    writeProbeStatus(
                        nodeType, success, beforeItems, afterItems, menuClass,
                        success ? QString()
                                : QStringLiteral("node menu selection did not add a scene item"));
                    if (!success) {
                        completion(false);
                        return;
                    }
                    captureUntranslated();
                    QTimer::singleShot(700, application, [] {
                        captureUntranslated();
                    });
                    QTimer::singleShot(1000, application,
                                       [completion] { completion(true); });
            });
        });
        // The node menu runs a nested event loop.  Register selection first
        // so it executes while that menu is open.
        openNodeMenu(graphWidget);
    });
}

struct ProbePlanJob {
    QString nodeType;
    QString capturePath;
    QString statusPath;
};

void startNodeProbePlan(QApplication* application, const QString& planPath) {
    QFile input(planPath);
    if (!input.open(QIODevice::ReadOnly)) {
        application->quit();
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(input.readAll(), &parseError);
    const QJsonObject root = document.object();
    const QJsonArray nodeValues = root.value(QStringLiteral("nodes")).toArray();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        nodeValues.isEmpty()) {
        application->quit();
        return;
    }
    auto jobs = std::make_shared<std::vector<ProbePlanJob>>();
    jobs->reserve(static_cast<std::size_t>(nodeValues.size()));
    for (const QJsonValue& value : nodeValues) {
        const QJsonObject item = value.toObject();
        ProbePlanJob job{
            item.value(QStringLiteral("node_type")).toString(),
            item.value(QStringLiteral("capture")).toString(),
            item.value(QStringLiteral("status")).toString(),
        };
        if (job.nodeType.isEmpty() || job.capturePath.isEmpty() ||
            job.statusPath.isEmpty()) {
            application->quit();
            return;
        }
        jobs->push_back(std::move(job));
    }

    auto next = std::make_shared<std::function<void(std::size_t)>>();
    *next = [application, jobs, next](std::size_t index) {
        if (index >= jobs->size()) {
            application->quit();
            return;
        }
        const ProbePlanJob& job = (*jobs)[index];
        qputenv("WRAP_CHINESE_CAPTURE_PATH", job.capturePath.toUtf8());
        qputenv("WRAP_CHINESE_PROBE_STATUS_PATH", job.statusPath.toUtf8());
        startNodeProbe(application, job.nodeType,
                       [application, next, index](bool success) {
            if (!success) {
                application->quit();
                return;
            }
            (*next)(index + 1);
        });
    };

    const QString baseline =
        root.value(QStringLiteral("baseline_capture")).toString();
    QTimer::singleShot(1200, application, [baseline, next] {
        if (!baseline.isEmpty()) {
            qputenv("WRAP_CHINESE_CAPTURE_PATH", baseline.toUtf8());
            captureUntranslated();
        }
        (*next)(0);
    });
}

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
                       bool usedFallback, const QString& saveError) {
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

        // Wrap creates parts of its menu tree lazily. Trigger the same logical
        // expansion recursively without moving the pointer or showing menus.
        QPointer<QMenu> guard(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        if (!guard) return;
        const QList<QAction*> menuActions = menu->actions();
        for (QAction* action : menuActions) collectAction(action);
        if (guard)
            QMetaObject::invokeMethod(menu, "aboutToHide", Qt::DirectConnection);
    };

    for (QWidget* widget : QApplication::allWidgets()) {
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

    if (qEnvironmentVariableIntValue("WRAP_CHINESE_QUIET_TEST") == 1) return;
    const int totalCount = entries.size();
    QTimer::singleShot(
        0, QApplication::instance(),
        [addedCount, totalCount, path, savedPath, saved, usedFallback,
         saveError] {
            showCaptureResult(addedCount, totalCount, path, savedPath, saved,
                              usedFallback, saveError);
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
    using QObject::QObject;
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
        if (event->type() == QEvent::Leave) {
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
};

void installMenuActions() {
    if (g_menuBar) return;
    QMenuBar* bar = nullptr;
    for (QWidget* widget : QApplication::allWidgets()) {
        auto* candidate = qobject_cast<QMenuBar*>(widget);
        if (candidate && candidate->actions().size() >= 2) {
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
    g_languageAction = new QAction(QStringLiteral("中/英"), bar);
    g_languageAction->setObjectName(QStringLiteral("wrap_chinese_language"));
    QObject::connect(g_languageAction, &QAction::triggered, [] { toggleLanguage(); });
    QAction* insertionPoint = nullptr;
    const QList<QAction*> existing = bar->actions();
    const int helpIndex = existing.indexOf(help);
    if (helpIndex >= 0 && helpIndex + 1 < existing.size())
        insertionPoint = existing.at(helpIndex + 1);
    if (insertionPoint) bar->insertAction(insertionPoint, g_languageAction);
    else bar->addAction(g_languageAction);

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
    g_authorAction = addLink(QStringLiteral("Bilibili 神说要凑数汉化"),
                             authorUrl, QStringLiteral("wrap_chinese_author"));
    g_githubAction = addLink(QStringLiteral("GitHub 仓库"),
                             githubUrl, QStringLiteral("wrap_chinese_github"));
    bar->installEventFilter(new MenuMouseFilter(bar));
    bar->setMouseTracking(true);
    g_menuBar = bar;
}

class ApplicationFilter final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::KeyRelease) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (g_swallowedShortcutKeys.remove(key->key())) return true;
        }
        if (event->type() == QEvent::KeyPress && !g_hotkeyDialogOpen &&
            !QApplication::activeModalWidget()) {
            auto* key = static_cast<QKeyEvent*>(event);
            const bool isTildeKey = key->key() == Qt::Key_AsciiTilde ||
                                    key->key() == Qt::Key_QuoteLeft;
            if (g_captureEnabled && isTildeKey &&
                key->modifiers() == Qt::ShiftModifier) {
                g_swallowedShortcutKeys.insert(key->key());
                if (!key->isAutoRepeat()) captureUntranslated();
                return true;
            }
            const int combination =
                key->key() | static_cast<int>(key->modifiers());
            if (g_toggleShortcut.count() == 1 &&
                combination == g_toggleShortcut[0]) {
                g_swallowedShortcutKeys.insert(key->key());
                if (WrapChineseConfig::shouldActivateShortcut(
                        g_toggleShortcut, combination, key->isAutoRepeat(),
                        false)) {
                    toggleLanguage();
                }
                return true;
            }
        }
        if (event->type() == QEvent::Show) {
            if (auto* widget = qobject_cast<QWidget*>(object)) applyWidget(widget);
            if (!g_menuBar)
                QTimer::singleShot(0, [] { installMenuActions(); });
        }
        if (event->type() == QEvent::Paint) {
            if (auto* label = qobject_cast<QLabel*>(object)) applyLabelText(label);
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
    if (g_inDrawHook || text.size() < 2) return {};
    bool valueOnly = true;
    for (const QChar ch : text) {
        if (ch.isDigit() || ch.isSpace() || QStringLiteral(".,%+-eE:/\\").contains(ch))
            continue;
        valueOnly = false;
        break;
    }
    if (valueOnly) return {};
    if (painter && painter->device() &&
        painter->device()->devType() == QInternal::Widget) {
        auto* widget = static_cast<QWidget*>(painter->device());
        if (auto* label = qobject_cast<QLabel*>(widget)) {
            QString full = label->text();
            if (full != text && !full.isEmpty()) {
                const QString target = translated(full);
                if (!target.isEmpty()) return target;
            }
        }
    }
    const QString direct = translated(text);
    return direct.isEmpty() ? recoverElided(text) : direct;
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
                       const QString& text, int from, int length) {
    const QString value = translatePaint(painter, text);
    DrawGuard guard;
    if (g_drawPointF2)
        g_drawPointF2(painter, point, value.isEmpty() ? text : value,
                      value.isEmpty() ? from : 0,
                      value.isEmpty() ? length : -1);
}

// Wrap and its Qt runtime were built with MinGW. These free-function pointer
// declarations deliberately mirror the Itanium member-function ABI by making
// `this` explicit. tests/mingw_sret_probe.cpp verifies that aggregate-return
// functions use the same hidden return-pointer / self argument order.
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
    if (reentered || text.size() < 2) return {};
    return translated(text);
}

int hookedFmAdvance(const QFontMetrics* self, const QString& text, int length) {
    if (!g_fmAdvance) return 0;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmAdvance(self, value.isEmpty() ? text : value,
                       value.isEmpty() ? length : -1);
}

int hookedFmWidth(const QFontMetrics* self, const QString& text, int length) {
    if (!g_fmWidth) return 0;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
    return g_fmWidth(self, value.isEmpty() ? text : value,
                     value.isEmpty() ? length : -1);
}

int hookedFmWidthFlags(const QFontMetrics* self, const QString& text,
                       int length, int flags) {
    if (!g_fmWidthFlags) return 0;
    FontMetricGuard guard;
    const QString value = translateMeasured(text, guard.reentered());
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
    const QString value = translateMeasured(text, guard.reentered());
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

template <typename Function>
Function resolveFunction(HMODULE module, const char* symbol) {
    const FARPROC address = GetProcAddress(module, symbol);
    static_assert(sizeof(Function) == sizeof(address),
                  "Function pointer size does not match FARPROC");
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

template <typename Function>
void* functionAddress(Function function) {
    static_assert(sizeof(Function) == sizeof(void*),
                  "Function pointer size does not match data pointer");
    void* address = nullptr;
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

bool replaceImports(HMODULE module, void* original, void* replacement) {
    if (!module || !original || !replacement) return false;
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const DWORD importRva = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return false;
    bool changed = false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->FirstThunk) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            auto** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            if (*slot != original) continue;
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) continue;
            const bool replaced = InterlockedCompareExchangePointer(
                slot, replacement, original) == original;
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
            if (replaced) changed = true;
        }
    }
    return changed;
}

std::unordered_set<HMODULE> g_drawScannedModules;
std::unordered_set<HMODULE> g_fontMetricScannedModules;

void patchDrawTextModules() {
    struct Symbol { void* original; void* replacement; } symbols[] = {
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
            for (const Symbol& symbol : symbols)
                replaceImports(module, symbol.original, symbol.replacement);
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
    if (g_drawHooksInstalled) patchDrawTextModules();
}

bool patchFontMetricModules() {
    struct Symbol { void* original; void* replacement; } symbols[] = {
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
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    bool changed = false;
    std::unordered_set<HMODULE> currentModules;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsnicmp(entry.szModule, kHookPrefix, wcslen(kHookPrefix)) == 0 ||
                _wcsicmp(entry.szModule, kCoreModule) == 0 ||
                _wcsicmp(entry.szModule, kGuiModule) == 0) continue;
            HMODULE module = GetModuleHandleW(entry.szModule);
            if (!module) continue;
            currentModules.insert(module);
            if (!g_fontMetricScannedModules.insert(module).second) continue;
            for (const Symbol& symbol : symbols)
                changed = replaceImports(module, symbol.original,
                                         symbol.replacement) || changed;
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    for (auto it = g_fontMetricScannedModules.begin();
         it != g_fontMetricScannedModules.end();) {
        if (!currentModules.count(*it)) it = g_fontMetricScannedModules.erase(it);
        else ++it;
    }
    return changed;
}

void installFontMetricHooks() {
    if (g_fontMetricHooksInstalled) return;
    HMODULE gui = GetModuleHandleW(kGuiModule);
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

void installProxyStyle() {
    if (g_proxyStyleInstalled || !QApplication::instance()) return;
    const QString key = QApplication::style()
        ? QApplication::style()->objectName() : QStringLiteral("fusion");
    QStyle* base = QStyleFactory::create(key);
    if (!base) base = QStyleFactory::create(QStringLiteral("fusion"));
    if (!base) return;
    QApplication::setStyle(new ChineseProxyStyle(base));
    g_proxyStyleInstalled = true;
}

void initializeOnGuiThread(QApplication* application) {
    g_pluginDir = selfDirectory();
    if (g_pluginDir.isEmpty()) return;
    loadDictionaries();
    // Install measurement first. Existing and subsequently constructed Wrap
    // widgets must see Chinese metrics before they cache their size hints.
    installFontMetricHooks();
    installDrawTextHooks();
    QString error;
    g_configPath = qEnvironmentVariable("WRAP_CHINESE_CONFIG_PATH");
    if (g_configPath.isEmpty())
        g_configPath = WrapChineseConfig::path(QStringLiteral("WrapChinese"));
    const WrapChineseConfig::Values config =
        WrapChineseConfig::load(g_configPath, &error);
    g_toggleShortcut = config.toggleShortcut;
    g_captureEnabled = config.captureEnabled;
    auto* translator = new DictionaryTranslator(application);
    application->installTranslator(translator);
    installProxyStyle();
    for (QWidget* widget : QApplication::allWidgets()) applyWidget(widget);
    application->installEventFilter(new ApplicationFilter(application));
    installMenuActions();
    refreshMeasuredLayouts();
    for (const int delay : {0, 250, 1000, 3000})
        QTimer::singleShot(delay, application, [] { refreshMeasuredLayouts(); });

    if (!qEnvironmentVariable("WRAP_CHINESE_WIDGET_INVENTORY_PATH").isEmpty())
        QTimer::singleShot(2500, application, [] { writeWidgetInventory(); });

    if (WrapHookLifecycle::kEnableDeferredPolling) {
        auto* timer = new QTimer(application);
        timer->setInterval(WrapHookLifecycle::kDeferredScanIntervalMs);
        QObject::connect(timer, &QTimer::timeout, [] {
            patchDrawTextModules();
            if (patchFontMetricModules()) refreshMeasuredLayouts();
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
    return "1.0.0";
}

extern "C" __declspec(dllexport) DWORD WINAPI WrapChineseInitialize() {
    bool expected = false;
    if (!g_initializationStarted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) return 1;
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
