#pragma once

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/Qt>
#include <QtGui/QKeySequence>

namespace WrapChineseConfig {

constexpr int kVersion = 1;

inline QKeySequence defaultToggleShortcut() {
    return QKeySequence(Qt::Key_F3);
}

struct Values final {
    QKeySequence toggleShortcut = defaultToggleShortcut();
    bool captureEnabled = true;
};

inline bool isSupportedBaseKey(int key) noexcept {
    if ((key >= Qt::Key_F1 && key <= Qt::Key_F12) ||
        (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)) return true;
    switch (key) {
    case Qt::Key_Tab:
    case Qt::Key_Return:
    case Qt::Key_Backspace:
    case Qt::Key_Insert:
    case Qt::Key_Delete:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_Right:
    case Qt::Key_Down:
        return true;
    default:
        return false;
    }
}

inline bool isSupportedShortcut(const QKeySequence& shortcut) noexcept {
    if (shortcut.count() != 1) return false;
    const int combination = shortcut[0];
    const int key = combination & ~Qt::KeyboardModifierMask;
    if (!isSupportedBaseKey(key)) return false;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return true;
    const Qt::KeyboardModifiers modifiers(
        combination & Qt::KeyboardModifierMask);
    return modifiers.testFlag(Qt::ControlModifier) ||
           modifiers.testFlag(Qt::AltModifier) ||
           modifiers.testFlag(Qt::MetaModifier);
}

inline bool shouldActivateShortcut(const QKeySequence& configured,
                                   int pressed,
                                   bool autoRepeat,
                                   bool shortcutInputBlocked) noexcept {
    return !autoRepeat && !shortcutInputBlocked && configured.count() == 1 &&
           pressed == configured[0];
}

inline QString path(const QString& product) {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base.isEmpty()
        ? QString()
        : QDir(base).filePath(product + QStringLiteral("/WrapChinese.ini"));
}

inline void setError(QString* error, const QString& message) {
    if (error && error->isEmpty()) *error = message;
}

inline bool parseBoolean(const QVariant& value, bool& result) {
    const QString normalized = value.toString().trimmed().toLower();
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("on")) {
        result = true;
        return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0") ||
        normalized == QStringLiteral("off")) {
        result = false;
        return true;
    }
    return false;
}

inline bool write(const QString& configPath, const Values& values,
                  QString* error = nullptr) {
    if (error) error->clear();
    if (configPath.isEmpty() || !isSupportedShortcut(values.toggleShortcut)) {
        setError(error, QStringLiteral("无效的配置路径或快捷键。"));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(configPath).absolutePath())) {
        setError(error, QStringLiteral("无法创建配置目录。"));
        return false;
    }
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.clear();
    settings.setValue(QStringLiteral("Plugin/version"), kVersion);
    settings.setValue(QStringLiteral("Hotkey/shortcut"),
                      values.toggleShortcut.toString(QKeySequence::PortableText));
    settings.setValue(QStringLiteral("Capture/enabled"), values.captureEnabled);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("无法写入 WrapChinese.ini。"));
        return false;
    }
    return true;
}

inline Values load(const QString& configPath, QString* error = nullptr) {
    if (error) error->clear();
    Values result;
    if (configPath.isEmpty()) {
        setError(error, QStringLiteral("无法定位插件配置目录，使用默认设置。"));
        return result;
    }
    if (!QFileInfo::exists(configPath)) {
        write(configPath, result, error);
        return result;
    }
    QSettings settings(configPath, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    if (settings.status() != QSettings::NoError) {
        setError(error, QStringLiteral("配置文件无法读取，使用默认设置。"));
        return result;
    }
    bool versionOk = false;
    const int version = settings.value(QStringLiteral("Plugin/version"), -1)
                            .toInt(&versionOk);
    if (!versionOk || version != kVersion) {
        setError(error, QStringLiteral("配置版本无效，使用默认设置。"));
        return result;
    }
    const QKeySequence shortcut = QKeySequence::fromString(
        settings.value(QStringLiteral("Hotkey/shortcut")).toString(),
        QKeySequence::PortableText);
    if (isSupportedShortcut(shortcut)) result.toggleShortcut = shortcut;
    else setError(error, QStringLiteral("快捷键配置无效，使用 F3。"));

    bool capture = true;
    if (parseBoolean(settings.value(QStringLiteral("Capture/enabled"), true),
                     capture)) {
        result.captureEnabled = capture;
    } else {
        setError(error, QStringLiteral("采集配置无效，保持开启。"));
    }
    return result;
}

inline bool saveToggleShortcut(const QString& configPath,
                               const QKeySequence& shortcut,
                               QString* error = nullptr) {
    if (!isSupportedShortcut(shortcut)) {
        if (error) *error = QStringLiteral("无效的快捷键。");
        return false;
    }
    Values values = load(configPath, error);
    values.toggleShortcut = shortcut;
    return write(configPath, values, error);
}

} // namespace WrapChineseConfig
