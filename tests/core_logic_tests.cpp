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
    ok &= require(templates.addOrAssign(
        QStringLiteral("File %1 does not exist!"),
        QStringLiteral("文件 %1 不存在！"), &error), "basic placeholder accepted");
    ok &= require(templates.addOrAssign(
        QStringLiteral("Slots %10 / %2 / %L1 / %n"),
        QStringLiteral("项 %n；%L1；%2；%10"), &error),
        "two-digit, localized and plural placeholders accepted");
    ok &= require(templates.addOrAssign(
        QStringLiteral("Pair %1 / %1"), QStringLiteral("配对 %1 / %1"),
        &error), "repeated placeholder accepted");
    ok &= require(templates.addOrAssign(
        QStringLiteral("%1 then %2"), QStringLiteral("%2 后 %1"), &error),
        "prefixless and reordered placeholders accepted");
    ok &= require(!templates.addOrAssign(
        QStringLiteral("Bad %1%2"), QStringLiteral("错误 %1 %2"), &error),
        "adjacent source placeholders rejected");
    ok &= require(!templates.addOrAssign(
        QStringLiteral("Missing %1"), QStringLiteral("缺少 %2"), &error),
        "mismatched placeholders rejected");
    templates.finalize();
    ok &= require(templates.lookup(QStringLiteral("File 模型.obj does not exist!")) ==
                      QStringLiteral("文件 模型.obj 不存在！"),
                  "CJK placeholder value matched");
    ok &= require(templates.lookup(QStringLiteral("Slots ten / two / one / 4")) ==
                      QStringLiteral("项 4；one；two；ten"),
                  "placeholder variants and reordering matched");
    ok &= require(templates.lookup(QStringLiteral("Pair same / same")) ==
                      QStringLiteral("配对 same / same"),
                  "repeated placeholder equality matched");
    ok &= require(templates.lookup(QStringLiteral("Pair left / right")).isEmpty(),
                  "repeated placeholder mismatch rejected");
    ok &= require(templates.lookup(QStringLiteral("first then second")) ==
                      QStringLiteral("second 后 first"),
                  "prefixless template matched");
    ok &= require(templates.lookup(QStringLiteral("File  does not exist!")) ==
                      QStringLiteral("文件  不存在！"),
                  "empty delimited placeholder matched");
    ok &= require(templates.lookup(QStringLiteral("first then ")) ==
                      QStringLiteral(" 后 first"),
                  "empty trailing placeholder matched");

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

    std::cout << "placeholder_templates=" << templates.size()
              << " config_roundtrip=" << (loaded.toggleShortcut == ctrlShiftL)
              << " cache_resolutions=" << resolutions
              << " cache_misses=" << misses << '\n';
    return ok ? 0 : 1;
}
