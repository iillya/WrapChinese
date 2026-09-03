#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QThread>
#include <QtGui/QFont>
#include <QtGui/QFontMetrics>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>

#include <cmath>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: font_metric_hook_host <hook-dll>\n";
        return 2;
    }

    QMainWindow window;
    window.resize(1000, 500);
    QMenuBar* menuBar = window.menuBar();
    menuBar->setNativeMenuBar(false);
    menuBar->addAction(QStringLiteral("File"));
    menuBar->addAction(QStringLiteral("Edit"));
    menuBar->addAction(QStringLiteral("Help"));
    const QString captureProbe = QStringLiteral("Wrap Chinese Hotkey Capture Probe");
    const QString lazyCaptureProbe =
        QStringLiteral("Wrap Chinese Lazy Menu Capture Probe");
    QMenu* lazyMenu = menuBar->addMenu(QStringLiteral("Collapsed Probe Menu"));
    QObject::connect(lazyMenu, &QMenu::aboutToShow, lazyMenu,
                     [lazyMenu, lazyCaptureProbe] {
        if (!lazyMenu->findChild<QAction*>(QStringLiteral("lazy_capture_probe"),
                                           Qt::FindDirectChildrenOnly)) {
            QAction* action = lazyMenu->addAction(lazyCaptureProbe);
            action->setObjectName(QStringLiteral("lazy_capture_probe"));
        }
    });
    window.setCentralWidget(new QLabel(captureProbe, &window));
    window.show();

    QFontMetrics metrics(QFont(QStringLiteral("Arial"), 12));
    const QString source = QStringLiteral("File");
    const QString target = QStringLiteral("文件");
    const int before = metrics.horizontalAdvance(source);
    const int expectedAdvance = metrics.horizontalAdvance(target);
    const QRect expectedBounds = metrics.boundingRect(target);
    const QSize expectedSize = metrics.size(Qt::TextSingleLine, target);
    const QRect expectedTightBounds = metrics.tightBoundingRect(target);
    QFontMetricsF metricsF(QFont(QStringLiteral("Arial"), 12));
    const qreal expectedAdvanceF = metricsF.horizontalAdvance(target);
    const QRectF expectedBoundsF = metricsF.boundingRect(target);
    const QSizeF expectedSizeF = metricsF.size(Qt::TextSingleLine, target);
    const QRectF expectedTightBoundsF = metricsF.tightBoundingRect(target);

    const QFileInfo hookInfo(QString::fromLocal8Bit(argv[1]));
    const QString captureFile = hookInfo.absolutePath() +
                                QStringLiteral("/hotkey_capture_test.json");
    const QString configFile = hookInfo.absolutePath() +
                               QStringLiteral("/WrapChinese-test.ini");
    QFile::remove(captureFile);
    QFile::remove(configFile);
    qputenv("WRAP_CHINESE_CAPTURE_PATH", captureFile.toUtf8());
    qputenv("WRAP_CHINESE_CONFIG_PATH", configFile.toUtf8());
    qputenv("WRAP_CHINESE_QUIET_TEST", QByteArrayLiteral("1"));
    const QString nativePath = QDir::toNativeSeparators(hookInfo.absoluteFilePath());
    HMODULE hook = LoadLibraryW(
        reinterpret_cast<const wchar_t*>(nativePath.utf16()));
    if (!hook) {
        std::cerr << "LoadLibraryW failed: " << GetLastError() << "\n";
        return 3;
    }
    using InitializeHook = DWORD (WINAPI*)();
    const FARPROC initializeAddress =
        GetProcAddress(hook, "WrapChineseInitialize");
    InitializeHook initialize = nullptr;
    static_assert(sizeof(initialize) == sizeof(initializeAddress),
                  "function pointer sizes must match");
    std::memcpy(&initialize, &initializeAddress, sizeof(initialize));
    if (!initialize || !initialize()) {
        std::cerr << "WrapChineseInitialize failed\n";
        return 3;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }

    const int after = metrics.horizontalAdvance(source);
    const QRect actualBounds = metrics.boundingRect(source);
    const QSize actualSize = metrics.size(Qt::TextSingleLine, source);
    const QRect actualTightBounds = metrics.tightBoundingRect(source);
    const QString actualElided = metrics.elidedText(source, Qt::ElideRight, 1000);
    const qreal actualAdvanceF = metricsF.horizontalAdvance(source);
    const QRectF actualBoundsF = metricsF.boundingRect(source);
    const QSizeF actualSizeF = metricsF.size(Qt::TextSingleLine, source);
    const QRectF actualTightBoundsF = metricsF.tightBoundingRect(source);
    const QString actualElidedF = metricsF.elidedText(
        source, Qt::ElideRight, 1000.0);
    const QString dynamicSource = QStringLiteral(
        "File 模型.obj does not exist!");
    const QString dynamicTarget = QStringLiteral("文件 模型.obj 不存在！");
    const bool cjkPlaceholderHooked =
        metrics.horizontalAdvance(dynamicSource) ==
        metrics.horizontalAdvance(dynamicTarget);
    const bool advanceHooked = after == expectedAdvance;
    const bool boundsHooked = actualBounds == expectedBounds;
    const bool sizeHooked = actualSize == expectedSize;
    const bool tightBoundsHooked = actualTightBounds == expectedTightBounds;
    const bool elideHooked = actualElided == target;
    const bool advanceFHooked =
        std::abs(actualAdvanceF - expectedAdvanceF) < 0.001;
    const bool boundsFHooked = actualBoundsF == expectedBoundsF;
    const bool sizeFHooked = actualSizeF == expectedSizeF;
    const bool tightBoundsFHooked = actualTightBoundsF == expectedTightBoundsF;
    const bool elideFHooked = actualElidedF == target;

    QKeyEvent togglePress(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier,
                          QString(), false, 1);
    QApplication::sendEvent(&window, &togglePress);
    const bool toggledOnPress = metrics.horizontalAdvance(source) == before;
    QKeyEvent toggleRepeat(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier,
                           QString(), true, 2);
    QApplication::sendEvent(&window, &toggleRepeat);
    const bool repeatIgnored = metrics.horizontalAdvance(source) == before;
    QKeyEvent toggleRelease(QEvent::KeyRelease, Qt::Key_F3, Qt::NoModifier);
    QApplication::sendEvent(&window, &toggleRelease);
    const bool releaseIgnored = metrics.horizontalAdvance(source) == before;
    QKeyEvent toggleRestore(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier,
                            QString(), false, 1);
    QApplication::sendEvent(&window, &toggleRestore);
    const bool toggleRestored =
        metrics.horizontalAdvance(source) == expectedAdvance;

    QAction* authorAction = menuBar->findChild<QAction*>(
        QStringLiteral("wrap_chinese_author"));
    QAction* githubAction = menuBar->findChild<QAction*>(
        QStringLiteral("wrap_chinese_github"));
    QAction* languageAction = menuBar->findChild<QAction*>(
        QStringLiteral("wrap_chinese_language"));
    const bool linksInstalled = authorAction && githubAction && languageAction;
    bool pointingHandHooked = false;
    bool cursorRestoreHooked = false;
    if (linksInstalled) {
        const QPoint linkPoint = menuBar->actionGeometry(authorAction).center();
        QMouseEvent moveOverLink(QEvent::MouseMove, linkPoint, Qt::NoButton,
                                 Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(menuBar, &moveOverLink);
        pointingHandHooked =
            menuBar->cursor().shape() == Qt::PointingHandCursor;
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(menuBar, &leave);
        cursorRestoreHooked =
            menuBar->cursor().shape() != Qt::PointingHandCursor;
    }

    bool hotkeyDialogOpened = false;
    bool hotkeyDialogStyled = false;
    bool dialogControlsSafe = false;
    if (languageAction) {
        const QPoint languagePoint =
            menuBar->actionGeometry(languageAction).center();
        QMouseEvent rightClick(QEvent::MouseButtonPress, languagePoint,
                               Qt::RightButton, Qt::RightButton,
                               Qt::NoModifier);
        QApplication::sendEvent(menuBar, &rightClick);
        QCoreApplication::processEvents();
        QDialog* hotkeyDialog = window.findChild<QDialog*>(
            QStringLiteral("wrap_chinese_hotkey_settings"));
        QPushButton* captureButton = hotkeyDialog
            ? hotkeyDialog->findChild<QPushButton*>(
                  QStringLiteral("hotkeyCapture"))
            : nullptr;
        QPushButton* cancelButton = nullptr;
        if (hotkeyDialog) {
            for (QPushButton* button :
                 hotkeyDialog->findChildren<QPushButton*>()) {
                if (button->text() == QStringLiteral("取消")) {
                    cancelButton = button;
                    break;
                }
            }
        }
        hotkeyDialogOpened = hotkeyDialog && captureButton && cancelButton;
        hotkeyDialogStyled = hotkeyDialogOpened && hotkeyDialog->width() == 440 &&
                             captureButton->font().pixelSize() == 14;
        if (hotkeyDialogOpened) {
            captureButton->click();
            cancelButton->click();
            QCoreApplication::processEvents();
            dialogControlsSafe =
                metrics.horizontalAdvance(source) == expectedAdvance;
        }
    }

    auto triggerCapture = [&window](bool autoRepeat = false) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_AsciiTilde,
                        Qt::ShiftModifier, QStringLiteral("~"), autoRepeat, 1);
        QApplication::sendEvent(&window, &event);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_AsciiTilde,
                          Qt::ShiftModifier, QStringLiteral("~"));
        QApplication::sendEvent(&window, &release);
    };
    triggerCapture();
    QCoreApplication::processEvents();
    QFile capture(captureFile);
    bool captureHotkeyHooked = false;
    bool lazyMenuCaptureHooked = false;
    if (capture.open(QIODevice::ReadOnly)) {
        const QJsonObject translations = QJsonDocument::fromJson(capture.readAll())
            .object().value(QStringLiteral("translations")).toObject();
        captureHotkeyHooked = translations.contains(captureProbe);
        lazyMenuCaptureHooked = translations.contains(lazyCaptureProbe);
        capture.close();
    }
    QFile::remove(captureFile);

    // A directory at the requested filename reliably reproduces a destination
    // that cannot be overwritten. The hook must preserve the capture in a
    // uniquely named sibling file instead of losing it.
    const QString blockedCapturePath = hookInfo.absolutePath() +
        QStringLiteral("/capture_fallback_test.json");
    QDir(blockedCapturePath).removeRecursively();
    const QString fallbackPattern = QStringLiteral("capture_fallback_test_*.json");
    for (const QString& file : QDir(hookInfo.absolutePath()).entryList(
             QStringList{fallbackPattern}, QDir::Files))
        QFile::remove(hookInfo.absolutePath() + QLatin1Char('/') + file);
    QDir().mkpath(blockedCapturePath);
    qputenv("WRAP_CHINESE_CAPTURE_PATH", blockedCapturePath.toUtf8());
    triggerCapture();
    QCoreApplication::processEvents();
    const QStringList fallbacks = QDir(hookInfo.absolutePath()).entryList(
        QStringList{fallbackPattern},
        QDir::Files, QDir::Time);
    bool captureFallbackHooked = false;
    if (!fallbacks.isEmpty()) {
        QFile fallback(hookInfo.absolutePath() + QLatin1Char('/') + fallbacks.first());
        if (fallback.open(QIODevice::ReadOnly)) {
            const QJsonObject translations =
                QJsonDocument::fromJson(fallback.readAll())
                    .object().value(QStringLiteral("translations")).toObject();
            captureFallbackHooked = translations.contains(captureProbe) &&
                                    translations.contains(lazyCaptureProbe);
        }
        for (const QString& file : fallbacks)
            QFile::remove(hookInfo.absolutePath() + QLatin1Char('/') + file);
    }
    QDir(blockedCapturePath).removeRecursively();

    const QString corruptCapturePath = hookInfo.absolutePath() +
        QStringLiteral("/capture_corrupt_test.json");
    const QByteArray corruptBytes("{ definitely-not-json\n");
    {
        QFile corrupt(corruptCapturePath);
        if (!corrupt.open(QIODevice::WriteOnly) ||
            corrupt.write(corruptBytes) != corruptBytes.size()) return 5;
    }
    const QString corruptFallbackPattern =
        QStringLiteral("capture_corrupt_test_*.json");
    for (const QString& file : QDir(hookInfo.absolutePath()).entryList(
             QStringList{corruptFallbackPattern}, QDir::Files))
        QFile::remove(hookInfo.absolutePath() + QLatin1Char('/') + file);
    qputenv("WRAP_CHINESE_CAPTURE_PATH", corruptCapturePath.toUtf8());
    triggerCapture();
    QCoreApplication::processEvents();
    QFile corruptAfter(corruptCapturePath);
    const bool corruptOriginalPreserved = corruptAfter.open(QIODevice::ReadOnly) &&
        corruptAfter.readAll() == corruptBytes;
    const QStringList corruptFallbacks = QDir(hookInfo.absolutePath()).entryList(
        QStringList{corruptFallbackPattern}, QDir::Files, QDir::Time);
    const bool corruptFallbackWritten = !corruptFallbacks.isEmpty();
    QFile::remove(corruptCapturePath);
    for (const QString& file : corruptFallbacks)
        QFile::remove(hookInfo.absolutePath() + QLatin1Char('/') + file);

    // Let the result window render once and assert that the custom Wrap dialog
    // stays dark even when the host/platform palette itself is light.
    const QString dialogCaptureFile = hookInfo.absolutePath() +
                                      QStringLiteral("/dialog_style_test.json");
    QFile::remove(dialogCaptureFile);
    qputenv("WRAP_CHINESE_CAPTURE_PATH", dialogCaptureFile.toUtf8());
    qunsetenv("WRAP_CHINESE_QUIET_TEST");
    triggerCapture();
    QCoreApplication::processEvents();
    QDialog* resultDialog = window.findChild<QDialog*>(
        QStringLiteral("wrap_chinese_capture_result"));
    QLabel* resultLabel = resultDialog ? resultDialog->findChild<QLabel*>() : nullptr;
    QPushButton* resultButton =
        resultDialog ? resultDialog->findChild<QPushButton*>() : nullptr;
    const bool captureDialogStyled = resultDialog &&
        resultDialog->palette().color(QPalette::Window).lightness() <= 150 &&
        resultLabel &&
        resultLabel->palette().color(QPalette::WindowText) == QColor("#f2f2f2") &&
        resultButton &&
        resultButton->palette().color(QPalette::ButtonText) == QColor("#f2f2f2");
    if (resultDialog) resultDialog->close();
    QCoreApplication::processEvents();
    QFile::remove(dialogCaptureFile);
    QFile::remove(configFile);

    std::cout << "advance_before=" << before
              << " advance_after=" << after
              << " advance_expected=" << expectedAdvance << "\n"
              << "advance_hooked=" << advanceHooked
              << " bounds_hooked=" << boundsHooked
              << " size_hooked=" << sizeHooked
              << " tight_bounds_hooked=" << tightBoundsHooked
              << " elide_hooked=" << elideHooked << "\n"
              << "advance_f_hooked=" << advanceFHooked
              << " bounds_f_hooked=" << boundsFHooked
              << " size_f_hooked=" << sizeFHooked
              << " tight_bounds_f_hooked=" << tightBoundsFHooked
              << " elide_f_hooked=" << elideFHooked
              << " cjk_placeholder_hooked=" << cjkPlaceholderHooked << "\n"
              << "toggled_on_press=" << toggledOnPress
              << " repeat_ignored=" << repeatIgnored
              << " release_ignored=" << releaseIgnored
              << " toggle_restored=" << toggleRestored << "\n"
              << "links_installed=" << linksInstalled
              << " pointing_hand_hooked=" << pointingHandHooked
              << " cursor_restore_hooked=" << cursorRestoreHooked
              << " hotkey_dialog_opened=" << hotkeyDialogOpened
              << " hotkey_dialog_styled=" << hotkeyDialogStyled
              << " dialog_controls_safe=" << dialogControlsSafe << "\n"
              << "capture_hotkey_hooked=" << captureHotkeyHooked
              << " lazy_menu_capture_hooked=" << lazyMenuCaptureHooked
              << " capture_fallback_hooked=" << captureFallbackHooked
              << " corrupt_original_preserved=" << corruptOriginalPreserved
              << " corrupt_fallback_written=" << corruptFallbackWritten
              << " capture_dialog_styled=" << captureDialogStyled << "\n";
    return advanceHooked && boundsHooked && sizeHooked &&
        tightBoundsHooked && elideHooked && cjkPlaceholderHooked && advanceFHooked &&
        boundsFHooked && sizeFHooked && tightBoundsFHooked &&
        elideFHooked && linksInstalled && pointingHandHooked &&
        cursorRestoreHooked && hotkeyDialogOpened && hotkeyDialogStyled &&
        dialogControlsSafe && toggledOnPress && repeatIgnored &&
        releaseIgnored && toggleRestored && captureHotkeyHooked &&
        lazyMenuCaptureHooked && captureFallbackHooked &&
        corruptOriginalPreserved && corruptFallbackWritten &&
        captureDialogStyled
        ? 0 : 4;
}
