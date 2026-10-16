#include "../source/plugin_config.h"
#include "../source/qt_placeholder_templates.h"
#include "../source/translation_lookup_cache.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include <iostream>

namespace {

bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    bool ok = true;

    WrapChinesePlaceholders::Catalog templates;
    QString error;
    ok &= require(templates.addOrAssign(QStringLiteral("Count %1 / %2 / %1"),
        QStringLiteral("数量 %2；%1；%1"), &error), "numeric reordered/repeated fields accepted");
    ok &= require(!templates.addOrAssign(QStringLiteral("File %L1"),
        QStringLiteral("文件 %L1")), "localized fields rejected for numeric matching");
    ok &= require(!templates.addOrAssign(QStringLiteral("Bad %1%2"),
        QStringLiteral("错误 %1 %2")), "adjacent fields rejected");
    ok &= require(!templates.addOrAssign(QStringLiteral("%1 then %2"),
        QStringLiteral("%2 后 %1")), "prefixless pattern rejected");
    ok &= require(!templates.addOrAssign(QStringLiteral("Bad %1"),
        QStringLiteral("错误 %2")), "mismatched fields rejected");
    ok &= require(templates.lookup(QStringLiteral("Count -12.50 / +3 / -12.50")) ==
        QStringLiteral("数量 +3；-12.50；-12.50"), "numeric values preserved verbatim");
    for (const QString& bad : {QStringLiteral("Count file / 1 / file"),
            QStringLiteral("Count 1 / 2 / 3"), QStringLiteral("Count  / 2 / "),
            QStringLiteral("Count 1e5 / 2 / 1e5"), QStringLiteral("Count 1,000 / 2 / 1,000"),
            QStringLiteral("Count 12345678901234567 / 2 / 12345678901234567"),
            QStringLiteral("Count 1.123456789 / 2 / 1.123456789"),
            QStringLiteral("count 1 / 2 / 1"), QStringLiteral(" Count 1 / 2 / 1")})
        ok &= require(templates.lookup(bad).isEmpty(), "unsafe numeric input rejected");
    ok &= require(templates.addOrAssign(QStringLiteral("Count %2 / %1 / %2"),
        QStringLiteral("歧义 %1；%2")) == false, "field occurrence mismatch rejected");
    ok &= require(templates.addOrAssign(QStringLiteral("Count %2 / %1 / %2"),
        QStringLiteral("歧义 %1；%2；%2")), "overlapping template accepted for ambiguity check");
    ok &= require(templates.lookup(QStringLiteral("Count 1 / 2 / 1")).isEmpty(), "ambiguous full match rejected");
    ok &= require(WrapChinesePlaceholders::validatePair(QStringLiteral("File %1: %2%3"),
        QStringLiteral("文件 %1：%2%3")), "general exact placeholders validated separately");
    ok &= require(!WrapChinesePlaceholders::validatePair(QStringLiteral("File %1"),
        QStringLiteral("文件 %2")), "invalid exact placeholder pair rejected");

    ok &= require(WrapChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F1)),
                  "F1 supported");
    ok &= require(WrapChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F12)),
                  "F12 supported");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_F13)),
                  "F13 rejected");
    const QKeySequence ctrlShiftL(
        Qt::CTRL | Qt::SHIFT | Qt::Key_L);
    ok &= require(WrapChineseConfig::isSupportedShortcut(ctrlShiftL),
                  "modified ordinary key supported");
    ok &= require(WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::SHIFT | Qt::Key_L)),
                  "shift ordinary key supported");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::META | Qt::Key_L)),
                  "Win ordinary key rejected");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::META | Qt::Key_F3)),
                  "Win function key rejected");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::CTRL | Qt::META | Qt::Key_L)),
                  "Win rejected when mixed with supported modifiers");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(QKeySequence(Qt::Key_L)),
                  "bare ordinary key rejected");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::SHIFT | Qt::Key_QuoteLeft)),
                  "capture shortcut spelling with quote-left is reserved");
    ok &= require(!WrapChineseConfig::isSupportedShortcut(
                      QKeySequence(Qt::SHIFT | Qt::Key_AsciiTilde)),
                  "capture shortcut spelling with tilde is reserved");
    ok &= require(WrapChineseConfig::shouldActivateShortcut(
                      ctrlShiftL, Qt::CTRL | Qt::SHIFT | Qt::Key_L,
                      false, false), "matching key press activates");
    ok &= require(!WrapChineseConfig::shouldActivateShortcut(
                      ctrlShiftL, Qt::CTRL | Qt::SHIFT | Qt::Key_L,
                      true, false), "auto-repeat rejected");
    ok &= require(!WrapChineseConfig::shouldActivateShortcut(
                      ctrlShiftL, Qt::CTRL | Qt::SHIFT | Qt::Key_L,
                      false, true), "blocked input rejected");

    QTemporaryDir temporary;
    ok &= require(temporary.isValid(), "temporary config directory created");
    const QString configPath = temporary.filePath(QStringLiteral("WrapChinese.ini"));
    WrapChineseConfig::Values values;
    values.toggleShortcut = ctrlShiftL;
    values.captureEnabled = false;
    ok &= require(WrapChineseConfig::write(configPath, values, &error),
                  "configuration written");
    const WrapChineseConfig::Values loaded =
        WrapChineseConfig::load(configPath, &error);
    ok &= require(loaded.toggleShortcut == ctrlShiftL,
                  "shortcut configuration round-tripped");
    ok &= require(!loaded.captureEnabled,
                  "capture configuration round-tripped");

    WrapChineseLookupCache::DirectMapped<8> cache;
    int resolutions = 0;
    const auto resolve = [&] {
        ++resolutions;
        return QStringLiteral("文件");
    };
    ok &= require(cache.lookup(QStringLiteral("File"), resolve) ==
                      QStringLiteral("文件"), "cache stores hit");
    ok &= require(cache.lookup(QStringLiteral("File"), resolve) ==
                      QStringLiteral("文件") && resolutions == 1,
                  "cache reuses hit");
    int misses = 0;
    const auto miss = [&] { ++misses; return QString(); };
    cache.lookup(QStringLiteral("Unknown"), miss);
    cache.lookup(QStringLiteral("Unknown"), miss);
    ok &= require(misses == 1, "cache stores miss");

    int longResolutions = 0;
    const QString longSource(1000000, QLatin1Char('A'));
    cache.lookup(longSource, [&] { ++longResolutions; return QStringLiteral("bad"); });
    ok &= require(longResolutions == 0, "oversized strings are neither resolved nor cached");

    std::cout << "placeholder_templates=" << templates.size()
              << " config_roundtrip=" << (loaded.toggleShortcut == ctrlShiftL)
              << " cache_resolutions=" << resolutions
              << " cache_misses=" << misses << '\n';
    return ok ? 0 : 1;
}
