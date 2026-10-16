#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../source/hook.cpp"
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QListWidget>
#include <QtCore/QThread>
#include <cstdio>
#include <QtGui/QImage>
#include <QtGui/QPicture>
#include <QtGui/QPixmap>

static bool enabled = true;
static QString lookup(const QString& text) {
    return enabled && text == QLatin1String("UV")
        ? QStringLiteral("纹理坐标与投影设置") : QString();
}

// Exercise the production style callbacks, including a temporary image cache.
static QString cacheDrawText;
static QString cacheStyleText;
static int recordedTextFlags = 0;
static int recordedJustification = 0;
static void capturePointArguments(QPainter*, const QPointF&, const QString& text,
                                  int flags, int padding) {
    cacheDrawText = text;
    recordedTextFlags = flags;
    recordedJustification = padding;
}
static bool unrelatedCacheRetainsSource = true;
static void captureCacheDraw(QPainter*, const QRect&, int, const QString& text, QRect*) {
    cacheDrawText = text;
}
class CacheRecordingStyle final : public QProxyStyle {
public:
    void drawControl(ControlElement, const QStyleOption*, QPainter* painter,
                     const QWidget*) const override {
        QImage unrelated(300, 40, QImage::Format_ARGB32);
        QPainter exportPainter(&unrelated);
        hookedDrawRect(&exportPainter, unrelated.rect(), Qt::AlignLeft,
                       QStringLiteral("UV"), nullptr);
        proxy()->drawItemText(&exportPainter, unrelated.rect(), Qt::AlignLeft,
                             QPalette(), true, QStringLiteral("UV"));
        unrelatedCacheRetainsSource = unrelatedCacheRetainsSource &&
            cacheDrawText == QStringLiteral("UV") && cacheStyleText == QStringLiteral("UV");
        hookedDrawRect(painter, QRect(0, 0, 300, 40), Qt::AlignLeft,
                       QStringLiteral("UV"), nullptr);
        proxy()->drawItemText(painter, QRect(0, 0, 300, 40), Qt::AlignLeft,
                             QPalette(), true, QStringLiteral("UV"));
    }
    void drawPrimitive(PrimitiveElement, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        drawControl(CE_PushButton, option, painter, widget);
    }
    void drawComplexControl(ComplexControl, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override {
        drawControl(CE_PushButton, option, painter, widget);
    }
    void drawItemText(QPainter*, const QRect&, int, const QPalette&, bool,
                      const QString& text, QPalette::ColorRole) const override {
        cacheStyleText = text;
    }
};
static bool checkDisplayCaches(const QString& target) {
    const auto previous = g_drawRect;
    const auto previousPoint = g_drawPointF2;
    g_drawRect = captureCacheDraw;
    g_drawPointF2 = capturePointArguments;
    ChineseProxyStyle style(new CacheRecordingStyle);
    QWidget owner;
    owner.setAttribute(Qt::WA_DontShowOnScreen);
    owner.show();
    QImage image(300, 40, QImage::Format_ARGB32);
    QPixmap pixmap(300, 40);
    QPicture picture;
    const QString source = QStringLiteral("UV");
    bool ok = true;
    {
        QPainter painter(&image);
        UiPaintBoundary::WidgetPaintScope scope(&painter, &owner);
        const int flags = Qt::TextForceRightToLeft;
        hookedDrawPointF2(&painter, QPointF(10, 20), source, flags, 17);
        ok = cacheDrawText == target && recordedTextFlags == flags && recordedJustification == 17;
        hookedDrawPointF2(&painter, QPointF(10, 20), source,
                         flags | Qt::TextBypassShaping, 23);
        ok = ok && cacheDrawText == source && recordedTextFlags == (flags | Qt::TextBypassShaping) &&
            recordedJustification == 23;
    }
    for (QPaintDevice* device : {static_cast<QPaintDevice*>(&image),
                                static_cast<QPaintDevice*>(&pixmap),
                                static_cast<QPaintDevice*>(&picture)}) {
        QPainter painter(device);
        QStyleOption option;
        QStyleOptionComplex complex;
        const QString expected = device == &picture ? source : target;
        style.drawControl(QStyle::CE_PushButton, &option, &painter, &owner);
        ok = ok && cacheDrawText == expected && cacheStyleText == expected;
        style.drawPrimitive(QStyle::PE_PanelButtonCommand, &option, &painter, &owner);
        ok = ok && cacheDrawText == expected && cacheStyleText == expected;
        style.drawComplexControl(QStyle::CC_ComboBox, &complex, &painter, &owner);
        ok = ok && cacheDrawText == expected && cacheStyleText == expected;
        // The scope must not leak into an unrelated image/export operation.
        style.drawControl(QStyle::CE_PushButton, &option, &painter, nullptr);
        ok = ok && cacheDrawText == source && cacheStyleText == source;
        hookedDrawRect(&painter, image.rect(), Qt::AlignLeft, source, nullptr);
        ok = ok && cacheDrawText == source;
    }
    g_drawRect = previous;
    g_drawPointF2 = previousPoint;
    ok = ok && unrelatedCacheRetainsSource;
    std::printf("widget-owned style caches and unrelated export isolation: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    const QString source = QStringLiteral("UV");
    const QString target = lookup(source);
    g_translations["UV"] = target;
    g_translations["MergePointPairs"] = QStringLiteral("合并点对");
    if (translatedRichIdentifiers(QStringLiteral("<b>MergePointPairs</b> MergePointPairs")) !=
        QStringLiteral("<b>合并点对</b> MergePointPairs") ||
        !translatedRichIdentifiers(QStringLiteral("<span>MergePointPairs</span>")).isEmpty())
        return 5;
    if (!checkDisplayCaches(target)) return 4;
    const QFontMetrics fm(app.font());
    const int originalWidth = fm.horizontalAdvance(source);
    const QString originalElide = fm.elidedText(source, Qt::ElideRight, 80);
    WidgetDisplayMetrics::install(lookup);
    installProxyStyle();
    if (!WidgetDisplayMetrics::patchedSlots || !WidgetDisplayMetrics::sizeHooked) return 2;
    if (fm.horizontalAdvance(source) != originalWidth ||
        fm.elidedText(source, Qt::ElideRight, 80) != originalElide) return 3;
    int failures = 0;
    // Render a synthetic label to memory; never read the user's desktop.
    // QTextDocument must use the host foreground palette, including disabled.
    ApplicationFilter richFilter;
    app.installEventFilter(&richFilter);
    QLabel rich(QStringLiteral("<b>UV</b>"));
    rich.resize(420, 60);
    QPalette richPalette = rich.palette();
    richPalette.setColor(QPalette::Active, QPalette::WindowText, QColor(255, 0, 255));
    richPalette.setColor(QPalette::Inactive, QPalette::WindowText, QColor(255, 0, 255));
    richPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0, 255, 0));
    rich.setPalette(richPalette);
    const auto containsColor = [&](const QColor& expected) {
        QImage image(rich.size(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        rich.render(&image);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == expected) return true;
        return false;
    };
    const bool activePalette = containsColor(QColor(255, 0, 255));
    rich.setEnabled(false);
    const bool disabledPalette = containsColor(QColor(0, 255, 0));
    std::printf("rich_label_palette=%d disabled_palette=%d\n", activePalette, disabledPalette);
    if (!activePalette || !disabledPalette) ++failures;
    app.removeEventFilter(&richFilter);
    const auto check = [&](const char* type, const QSize& actual, const QSize& expected) {
        const bool same = actual == expected;
        std::printf("%s %s actual=%dx%d expected=%dx%d\n", same ? "PASS" : "FAIL",
            type, actual.width(), actual.height(), expected.width(), expected.height());
        if (!same) ++failures;
    };
    QLabel label(source), labelReference(target);
    QPushButton button(source), buttonReference(target);
    QCheckBox checkBox(source), checkReference(target);
    QRadioButton radio(source), radioReference(target);
    QGroupBox group(source), groupReference(target);
    QTabBar tabs, tabsReference;
    tabs.addTab(source); tabsReference.addTab(target);
    QComboBox combo, comboReference;
    combo.addItem(source); comboReference.addItem(target);
    QMenuBar menu, menuReference;
    menu.addAction(source); menuReference.addAction(target);
    QListWidget list, listReference;
    list.addItem(source); listReference.addItem(target);
    applyWidget(&list);
    check("label", label.sizeHint(), labelReference.sizeHint());
    check("button", button.sizeHint(), buttonReference.sizeHint());
    check("checkbox", checkBox.sizeHint(), checkReference.sizeHint());
    check("radio", radio.sizeHint(), radioReference.sizeHint());
    check("group", group.minimumSizeHint(), groupReference.minimumSizeHint());
    check("tab", tabs.sizeHint(), tabsReference.sizeHint());
    tabs.setShape(QTabBar::RoundedWest); tabsReference.setShape(QTabBar::RoundedWest);
    check("vertical-tab", tabs.sizeHint(), tabsReference.sizeHint());
    check("combo", combo.sizeHint(), comboReference.sizeHint());
    check("menu", menu.sizeHint(), menuReference.sizeHint());
    check("list-row", QSize(list.sizeHintForColumn(0),list.sizeHintForRow(0)),
        QSize(listReference.sizeHintForColumn(0),listReference.sizeHintForRow(0)));
    if (label.text() != source || button.text() != source || tabs.tabText(0) != source ||
        combo.currentText() != source || list.item(0)->text() != source ||
        menu.actions().at(0)->text() != source) ++failures;
    enabled = false;
    toggleLanguage();
    QLabel english(source);
    check("existing-label-English", label.sizeHint(), english.sizeHint());
    if (english.sizeHint().width() >= labelReference.sizeHint().width()) ++failures;
    std::printf("Qt %s: %u widget import slots, source data retained, failures=%d\n",
        qVersion(), WidgetDisplayMetrics::patchedSlots, failures);
    return failures ? 1 : 0;
}
