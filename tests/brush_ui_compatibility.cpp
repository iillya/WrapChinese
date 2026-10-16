#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../source/hook.cpp"
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QItemEditorFactory>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QToolButton>
#include <cstdio>

// Deliberately omit Q_OBJECT: Qt metadata still says QStyledItemDelegate,
// although this is a host-specific painting/size implementation.
class HostBrushDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(96, 78);
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex&) const override {
        painter->fillRect(option.rect, QColor(25, 180, 100));
    }
};

// A product style may have no factory key, or inherit a native key while
// overriding metrics and drawing. Neither can be recreated as a stock style.
class HostBrushStyle final : public QProxyStyle {
public:
    explicit HostBrushStyle(const QString& name)
        : QProxyStyle(QStyleFactory::create(QStringLiteral("fusion"))) {
        setObjectName(name);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override {
        if (metric == PM_ToolBarIconSize) return 47;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

static void resetStyleState() {
    // Each case explicitly chooses a fresh host style before initialization.
    g_proxyStyleInstalled = false;
}

static bool iconsAndLayout(QApplication& app) {
    g_translations["Relax"] = QStringLiteral("松弛");
    g_translations["Move"] = QStringLiteral("移动");
    QSplitter splitter;
    auto* scroll = new QScrollArea(&splitter);
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(110);
    auto* palette = new QWidget;
    auto* layout = new QVBoxLayout(palette);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    QPixmap icon(32, 32); icon.fill(QColor(25, 170, 225));
    QList<QToolButton*> buttons;
    for (int index = 0; index < 13; ++index) {
        auto* button = new QToolButton(palette);
        button->setText(index % 2 ? QStringLiteral("Move") : QStringLiteral("Relax"));
        button->setIcon(QIcon(icon));
        button->setIconSize(QSize(32, 32));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setFixedSize(94, 78);
        layout->addWidget(button);
        buttons.append(button);
    }
    scroll->setWidget(palette);
    splitter.addWidget(scroll);
    splitter.addWidget(new QWidget);
    splitter.resize(800, 320);
    splitter.setSizes({110, 690});
    splitter.show();
    app.processEvents();
    bool ok = splitter.sizes().at(0) > 0 && scroll->viewport()->width() > 0;
    for (auto* button : buttons) {
        QImage image(button->size() * button->devicePixelRatioF(), QImage::Format_ARGB32);
        image.setDevicePixelRatio(button->devicePixelRatioF());
        image.fill(Qt::transparent);
        button->render(&image);
        int colored = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == QColor(25, 170, 225)) ++colored;
        ok = ok && colored > 400 && !button->icon().isNull() && button->width() > 0 &&
            button->height() > 0 && !button->isHidden() &&
            (button->text() == QStringLiteral("Relax") || button->text() == QStringLiteral("Move"));
    }
    std::printf("brush_buttons=13 icons_layout_source_preserved=%d dpr=%.2f\n",
                ok, splitter.devicePixelRatioF());
    return ok;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    const bool diagnose = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--diagnose");
    bool ok = true;

    QListWidget list;
    list.addItem(QStringLiteral("Relax"));
    auto* hostDelegate = new HostBrushDelegate(&list);
    list.setItemDelegate(hostDelegate);
    const bool misleadingMetaName = QString::fromLatin1(hostDelegate->metaObject()->className()) ==
                                    QLatin1String("QStyledItemDelegate");
    applyWidget(&list);
    const bool delegatePreserved = list.itemDelegate() == hostDelegate && list.sizeHintForRow(0) == 78;
    std::printf("custom_delegate_meta_looks_default=%d custom_delegate_preserved=%d\n",
                misleadingMetaName, delegatePreserved);
    ok &= misleadingMetaName && delegatePreserved;

    for (const QString& key : {QStringLiteral("host-brush-style"), QStringLiteral("fusion")}) {
        app.setStyle(new HostBrushStyle(key));
        QStyle* before = app.style();
        resetStyleState();
        installProxyStyle();
        const bool preserved = app.style() == before &&
            app.style()->pixelMetric(QStyle::PM_ToolBarIconSize) == 47;
        std::printf("host_style_key=%s preserved=%d resulting_style=%s\n",
            key.toUtf8().constData(), preserved, app.style()->metaObject()->className());
        ok &= preserved;
    }

    app.setStyle(QStringLiteral("fusion"));
    app.setStyleSheet(QStringLiteral("QToolButton { padding: 3px; color: #dddddd; }"));
    QStyle* stylesheetStyle = app.style();
    resetStyleState();
    installProxyStyle();
    const bool stylesheetPreserved = app.style() == stylesheetStyle;
    std::printf("stylesheet_style_preserved=%d\n", stylesheetPreserved);
    ok &= stylesheetPreserved;
    ok &= iconsAndLayout(app);

    app.setStyleSheet(QString());
    app.setStyle(QStringLiteral("fusion"));
    QPalette dark = app.palette();
    dark.setColor(QPalette::Window, QColor(45, 45, 45));
    dark.setColor(QPalette::WindowText, QColor(220, 220, 220));
    app.setPalette(dark);
    resetStyleState();
    installProxyStyle();
    const bool nativeStyleSupported = dynamic_cast<ChineseProxyStyle*>(app.style()) != nullptr;
    std::printf("native_style_proxy_supported=%d\n", nativeStyleSupported);
    ok &= nativeStyleSupported;
    ok &= app.palette() == dark;
    ok &= iconsAndLayout(app);

    QToolButton deliberatelyHidden;
    deliberatelyHidden.hide();
    toggleLanguage();
    ok &= deliberatelyHidden.isHidden();
    toggleLanguage();
    for (int points : {12, 18, 26}) {
        QFont font = app.font(); font.setPointSize(points); app.setFont(font);
        ok &= iconsAndLayout(app);
    }
    // A configured default editor factory is host behavior too.
    QListWidget configuredList;
    auto* configured = new QStyledItemDelegate(&configuredList);
    QItemEditorFactory factory;
    configured->setItemEditorFactory(&factory);
    configuredList.setItemDelegate(configured);
    applyWidget(&configuredList);
    const auto* translatedDelegate = dynamic_cast<const QStyledItemDelegate*>(configuredList.itemDelegate());
    ok &= translatedDelegate && translatedDelegate->itemEditorFactory() == &factory;

    std::printf("brush_ui_compatibility=%d diagnostic_mode=%d\n", ok, diagnose);
    return ok || diagnose ? 0 : 1;
}
