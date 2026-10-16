#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QRegularExpression>
#include <QtCore/QThread>
#include <QtGui/QPainter>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QLabel>
#include <iostream>
#include <cstring>

class Canvas : public QWidget {
public:
    using QWidget::QWidget;
    QPixmap content;
    bool rectOverload = false;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(73, 73, 73));
        if (rectOverload)
            painter.drawPixmap(QRectF(content.rect()), content, QRectF(content.rect()));
        else
            painter.drawPixmap(QPointF(0, 0), content);
    }
};
namespace Wrap {
class WContextHelpHintBase final : public Canvas {
    Q_OBJECT
public:
    using Canvas::Canvas;
};
}

static QString html(const QString& line, bool images) {
    QString result;
    int position = 0;
    const QRegularExpression key(QStringLiteral("\\*([^*]+)\\*"));
    auto matches = key.globalMatch(line);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += line.mid(position, match.capturedStart() - position).toHtmlEscaped();
        const QString token = match.captured(1);
        result += images ? QStringLiteral("<img src=\"cap:%1\" width=\"22\" height=\"20\"/>")
                           .arg(token.toHtmlEscaped())
                         : QStringLiteral("<b>%1</b>").arg(token.toHtmlEscaped());
        position = match.capturedEnd();
    }
    return result + line.mid(position).toHtmlEscaped();
}

static void setup(QTextDocument& document, const QString& markup, qreal dpr = 1) {
    Q_UNUSED(dpr);
    document.setDocumentMargin(0);
    document.setDefaultFont(QFont(QStringLiteral("Arial"), 12));
    QTextOption option;
    option.setWrapMode(QTextOption::NoWrap);
    document.setDefaultTextOption(option);
    // Distinct resource pixels make missing or replaced keycaps observable.
    const QString keys = QStringLiteral("LMB,RMB,MMB,SHIFT,CTRL,ALT,SPACE,ESC,ENTER,A,Z,D,X,C");
    for (const QString& key : keys.split(QLatin1Char(','))) {
        QImage cap(22, 20, QImage::Format_ARGB32_Premultiplied);
        cap.fill(QColor(35 + int(qHash(key) % 120), 140, 210));
        document.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("cap:") + key), cap);
    }
    document.setHtml(markup);
    document.setTextWidth(1100);
}

static QPixmap raster(QTextDocument& document, qreal dpr = 1, int height = 130) {
    QPixmap pixmap(QSize(1100, height) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(Qt::white);
    const QTransform transform = painter.worldTransform();
    document.drawContents(&painter, QRectF(0, 0, 1100, height));
    if (painter.worldTransform() != transform) std::abort();
    return pixmap;
}

static QImage displayed(Canvas& widget, const QPixmap& pixmap, bool rect = false) {
    widget.content = pixmap;
    widget.rectOverload = rect;
    widget.repaint();
    return widget.grab().toImage();
}

static bool require(bool condition, const char* label) {
    if (!condition) std::cerr << "FAIL: " << label << '\n';
    return condition;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    if (qEnvironmentVariableIntValue("WRAP_CHINESE_TEST_STYLESHEET") == 1)
        application.setStyleSheet(QStringLiteral("QWidget { color: #dddddd; background-color: #494949; }"));
    if (argc != 3) return 2;
    QFile review(QString::fromLocal8Bit(argv[2]));
    if (!review.open(QIODevice::ReadOnly)) return 2;
    const QJsonObject reviewed = QJsonDocument::fromJson(review.readAll()).object();
    const QJsonArray hints = reviewed.value(QStringLiteral("hints")).toArray();
    QMainWindow window;
    window.menuBar()->addAction(QStringLiteral("File"));
    window.menuBar()->addAction(QStringLiteral("Help"));
    Wrap::WContextHelpHintBase help;
    Canvas reference;
    help.resize(1100, 130);
    reference.resize(help.size());
    window.show(); help.show(); reference.show();

    HMODULE hook = LoadLibraryW(reinterpret_cast<const wchar_t*>(QString::fromLocal8Bit(argv[1]).utf16()));
    if (!hook) return 3;
    const FARPROC address = GetProcAddress(hook, "WrapChineseInitialize");
    DWORD (WINAPI* initialize)() = nullptr;
    std::memcpy(&initialize, &address, sizeof(initialize));
    if (!initialize || !initialize()) return 3;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 1500) {
        application.processEvents();
        QThread::msleep(5);
    }
    QAction* language = window.menuBar()->findChild<QAction*>(QStringLiteral("qt_chinese_language"));
    if (!language) return 3;
    bool ok = true;
    int passed = 0;
    const QJsonObject terms = reviewed.value(QStringLiteral("supplementary_terms")).toObject();
    for (auto it = terms.constBegin(); it != terms.constEnd(); ++it) {
        QLabel original(it.key()), expected(it.value().toString());
        original.setFont(QFont(QStringLiteral("Arial"), 12));
        expected.setFont(original.font());
        original.resize(500, 50); expected.resize(original.size());
        original.show(); expected.show();
        const bool termCorrect = original.grab().toImage() == expected.grab().toImage();
        if (!termCorrect) std::cerr << "term=" << it.key().toUtf8().constData() << '\n';
        ok &= require(termCorrect && original.text() == it.key(),
                      "supplementary UI caption translated without changing QLabel text");
    }
    QMap<QString, QString> segments;
    QPixmap firstEnglish;
    QImage firstExpected;
    for (const auto& value : hints) {
        const auto hint = value.toObject();
        const QString source = hint.value(QStringLiteral("source")).toString();
        const QString target = hint.value(QStringLiteral("target")).toString();
        const QRegularExpression keyMarkup(QStringLiteral("\\*[^*]+\\*"));
        const QStringList prose = source.split(keyMarkup);
        const QStringList chineseProse = target.split(keyMarkup);
        if (prose.size() != chineseProse.size()) return 4;
        for (int index = 0; index < prose.size(); ++index) {
            const QString segment = prose.at(index).trimmed();
            const QString translatedSegment = chineseProse.at(index).trimmed();
            if (!segment.isEmpty() && segment != translatedSegment) {
                if (segments.contains(segment) && segments.value(segment) != translatedSegment)
                    return 4;
                segments.insert(segment, translatedSegment);
            }
        }
        QTextDocument original, translated;
        setup(original, html(source, false));
        setup(translated, html(target, false));
        const QString originalHtml = original.toHtml();
        const int revision = original.revision();
        bool sourceChanged = false;
        QObject::connect(&original, &QTextDocument::contentsChanged, [&] { sourceChanged = true; });
        const QPixmap english = raster(original), chinese = raster(translated);
        const QImage before = english.toImage();
        const QImage expected = displayed(reference, chinese);
        if (firstEnglish.isNull()) { firstEnglish = english; firstExpected = expected; }
        const bool translatedCorrectly = displayed(help, english) == expected &&
            displayed(help, english, true) == expected;
        if (!translatedCorrectly) std::cerr << "source=" << source.toUtf8().constData() << '\n';
        ok &= require(translatedCorrectly, "native IAT path matches independently rendered Chinese");
        ok &= require(original.toHtml() == originalHtml && original.revision() == revision &&
                      !sourceChanged && english.toImage() == before, "host document and cached pixmap unchanged");
        ok &= require(displayed(reference, english) != expected, "unrelated widget preserves English cache");
        if (translatedCorrectly) ++passed;
    }
    ok &= require(displayed(help, firstEnglish) == firstExpected,
                  "old host cache survives Chinese raster budget eviction");
    int segmentCount = 0;
    for (auto it = segments.constBegin(); it != segments.constEnd(); ++it) {
        QTextDocument original, translated;
        setup(original, it.key().toHtmlEscaped());
        setup(translated, it.value().toHtmlEscaped());
        // Exercise Wrap's actual Markdown rasterization entry as well as HTML.
        original.setMarkdown(it.key());
        translated.setMarkdown(it.value());
        const QPixmap english = raster(original), chinese = raster(translated);
        const bool equal = displayed(help, english) == displayed(reference, chinese);
        if (!equal) std::cerr << "segment=" << it.key().toUtf8().constData() << '\n';
        ok &= require(equal, "separately rasterized Markdown prose chunk");
        if (equal) ++segmentCount;
    }

    // Cached help created while translation is disabled must work when enabled.
    language->trigger();
    const QString source = QStringLiteral("*SHIFT* + *LMB* drag to relax<br>Hold *A* to hide the sculpted mesh");
    const QString target = QStringLiteral("*SHIFT* + *LMB* 拖动以松弛<br>按住 *A* 隐藏雕刻网格");
    QTextDocument imageSource, imageTarget;
    setup(imageSource, html(source.section(QStringLiteral("<br>"), 0, 0), true) +
        QStringLiteral("<br>") + html(source.section(QStringLiteral("<br>"), 1), true));
    setup(imageTarget, html(target.section(QStringLiteral("<br>"), 0, 0), true) +
        QStringLiteral("<br>") + html(target.section(QStringLiteral("<br>"), 1), true));
    const QPixmap englishImages = raster(imageSource, 1.5);
    const QPixmap chineseImages = raster(imageTarget, 1.5);
    const QImage englishExpected = displayed(reference, englishImages);
    const QImage chineseExpected = displayed(reference, chineseImages);
    ok &= require(displayed(help, englishImages) == englishExpected, "disabled passthrough");
    language->trigger();
    ok &= require(displayed(help, englishImages) == chineseExpected, "cached disabled-created hint, keycap images and DPR");
    language->trigger();
    ok &= require(displayed(help, englishImages) == englishExpected, "English restored by repaint without rebuilding cache");
    language->trigger();
    ok &= require(displayed(help, englishImages) == chineseExpected, "Chinese restored by repaint");
    // Exporting that very same help cache must still use the English pixels.
    QImage output(englishImages.size(), QImage::Format_ARGB32_Premultiplied);
    output.setDevicePixelRatio(englishImages.devicePixelRatio());
    output.fill(Qt::transparent);
    { QPainter painter(&output); painter.drawPixmap(QPointF(0, 0), englishImages); }
    ok &= require(output == englishImages.toImage(), "offscreen image export unchanged");
    QTextDocument unknown;
    setup(unknown, html(QStringLiteral("*LMB* and drag to sculpt"), false) +
          QStringLiteral("<br>UserProjectName"));
    const QPixmap unknownCache = raster(unknown);
    ok &= require(displayed(help, unknownCache) == displayed(reference, unknownCache),
                  "mixed document with business content rejected");

    const QStringList brush = {
        QStringLiteral("*LMB* and drag to sculpt"),
        QStringLiteral("*SHIFT* + *LMB* drag to relax"),
        QStringLiteral("*CTRL* + *LMB* drag to invert brush effect"),
        QStringLiteral("*RMB* and drag to change the brush size"),
        QStringLiteral("*MMB* and drag to change the brush strength"),
        QStringLiteral("*SHIFT* + *RMB* and drag to change the brush falloff"),
        QStringLiteral("*SHIFT* + *SPACE* to switch between cameras"),
        QStringLiteral("*SPACE* to return to default camera"),
        QStringLiteral("*D* to toggle geometry opacity"),
        QStringLiteral("*[* or *]* changes the brush size"),
        QStringLiteral("*X* to toggle geometry wireframe"),
        QStringLiteral("*C* to toggle display mode color/texture"),
        QStringLiteral("Hold *A* to hide the sculpted mesh"),
        QStringLiteral("Hold *Z* to show the source mesh")};
    QStringList brushSource, brushTarget;
    for (const QString& line : brush) {
        brushSource.append(html(line, false));
        for (const auto& hint : hints)
            if (hint.toObject().value(QStringLiteral("source")).toString() == line)
                brushTarget.append(html(hint.toObject().value(QStringLiteral("target")).toString(), false));
    }
    QTextDocument fullSource, fullTarget;
    setup(fullSource, brushSource.join(QStringLiteral("<br>")));
    setup(fullTarget, brushTarget.join(QStringLiteral("<br>")));
    const QPixmap brushCache = raster(fullSource, 1, 420);
    const QPixmap brushChinese = raster(fullTarget, 1, 420);
    help.resize(1100, 420); reference.resize(help.size());
    const QImage fullChinese = displayed(help, brushCache);
    ok &= require(fullChinese == displayed(reference, brushChinese), "all 14 Brush rows render together");
    const QString artifacts = QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath();
    fullChinese.save(artifacts + QStringLiteral("/shortcut_hint_chinese.png"));
    displayed(reference, brushCache).save(artifacts + QStringLiteral("/shortcut_hint_english.png"));
    const FARPROC shutdownAddress = GetProcAddress(hook, "WrapChineseTestShutdownHintHooks");
    void (*shutdownHints)() = nullptr;
    std::memcpy(&shutdownHints, &shutdownAddress, sizeof(shutdownHints));
    ok &= require(shutdownHints != nullptr, "hint hook shutdown entry");
    if (shutdownHints) {
        shutdownHints(); shutdownHints();
        ok &= require(displayed(help, brushCache) == displayed(reference, brushCache),
                      "recorded IAT slots restored; repeated shutdown is safe");
    }
    std::cout << "shortcut_hints=" << passed << '/' << hints.size()
              << " prose_chunks=" << segmentCount << '/' << segments.size()
              << " images_toggle_dpr_exports=" << ok << '\n';
    return ok && passed == 94 ? 0 : 4;
}

#include "shortcut_hint_display.moc"
