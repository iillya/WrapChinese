#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define WRAP_CHINESE_TEST_HOOKS
#include "../source/hook.cpp"
#include <QtCore/QTemporaryDir>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QListView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QSpinBox>
#include <cstdio>

static bool require(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    return condition;
}

class LazyModel final : public QStandardItemModel {
public:
    int fetches = 0;
    bool canFetchMore(const QModelIndex&) const override { return true; }
    void fetchMore(const QModelIndex&) override { ++fetches; }
};

static bool editorBoundary(QWidget* widget) {
    widget->window()->show();
    QImage image(400, 40, QImage::Format_ARGB32);
    QPainter painter(&image);
    UiPaintBoundary::WidgetPaintScope scope(&painter, widget);
    return !UiPaintBoundary::isDisplayPainter(&painter) &&
        translatePaint(&painter, QStringLiteral("File")).isEmpty() &&
        WidgetDisplayMetrics::translateMeasured(QStringLiteral("File"), false).isEmpty();
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    bool ok = true;
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    const auto dictionary = [&](const QString& name, const QJsonObject& entries) {
        QString error;
        return writeCaptureFile(temp.filePath(QStringLiteral("translations/") + name),
            QJsonDocument(QJsonObject{{QStringLiteral("$schema"), QStringLiteral("sp-translation-v1")},
                {QStringLiteral("language"), QStringLiteral("zh-CN")},
                {QStringLiteral("translations"), entries}}).toJson(), error);
    };
    QDir().mkpath(temp.filePath(QStringLiteral("translations")));
    dictionary(QStringLiteral("a_zh.json"), {{QStringLiteral("Example_Key"), QStringLiteral("前文")},
        {QStringLiteral("File"), QStringLiteral("文件")}});
    dictionary(QStringLiteral("z_zh.json"), {{QStringLiteral("Example Key"), QStringLiteral("后文")},
        {QStringLiteral("File"), QStringLiteral("后文件")}});
    g_pluginDir = temp.path(); loadDictionaries();
    ok &= require(translated(QStringLiteral("File")) == QStringLiteral("后文件") &&
        translated(QStringLiteral("EXAMPLE KEY")) == QStringLiteral("后文") &&
        !g_dictionaryConflicts.isEmpty(), "deterministic dictionary override and conflict diagnostics");
    ok &= require(normalize(QString::fromUtf8("Ｆｉｌｅ_Ｒéｎａｍｅ\xc2\xad")) ==
        QStringLiteral("file rename"), "normalization matches display standard");

    g_pluginDir = QString::fromLocal8Bit(argv[1]); loadDictionaries();
    ok &= require(translated(QString::fromUtf8("Ｆｉｌｅ")) == QStringLiteral("文件"),
        "fullwidth Latin interface text can use the shared normalizer");
    ok &= require(!translated(QStringLiteral("Clear -12.50 point(s)")).isEmpty() &&
        translatedForMeasurement(QStringLiteral("Clear -12.50 point(s)")).isEmpty() &&
        translated(QStringLiteral("CLEAR -12.50 point(s)")).isEmpty() &&
        translated(QStringLiteral(" Clear -12.50 point(s)")).isEmpty(), "numeric templates respect full source and measurement boundary");
    ok &= require(translated(QStringLiteral("File File does not exist!")).isEmpty() &&
        translated(QStringLiteral("Camera12")).isEmpty() &&
        translated(QStringLiteral("C:/File")).isEmpty() &&
        translated(QStringLiteral("https://example.com/File")).isEmpty(), "arbitrary names paths and string templates are preserved");

    app.setStyle(QStringLiteral("fusion"));
    const QPalette originalPalette = app.palette();
    QMainWindow window;
    auto* bar = window.menuBar();
    bar->setMouseTracking(false);
    bar->addAction(QStringLiteral("File"));
    bar->addAction(QStringLiteral("Edit"));
    installMenuActions();
    auto* help = new QAction(QStringLiteral("Help"), bar);
    bar->insertAction(g_languageAction, help);
    auto* tail = bar->addAction(QStringLiteral("Host Tail"));
    for (int i = 0; i < 20; ++i) installMenuActions();
    const auto actions = bar->actions();
    const int at = actions.indexOf(help);
    ok &= require(actions.size() == 7 && actions.at(at + 1) == g_languageAction &&
        actions.at(at + 2) == g_authorAction && actions.at(at + 3) == g_githubAction &&
        actions.last() == tail, "late Help menu placement is stable and idempotent");

    QListView view;
    auto* delegate = view.itemDelegate();
    g_enabled.store(false);
    ApplicationFilter lifecycle;
    QKeyEvent overrideEvent(QEvent::ShortcutOverride, Qt::Key_F3, Qt::NoModifier);
    overrideEvent.ignore();
    ok &= require(lifecycle.eventFilter(&window, &overrideEvent) && overrideEvent.isAccepted(),
        "configured key claims ShortcutOverride before host QShortcuts");
    QEvent polish(QEvent::Polish);
    lifecycle.eventFilter(&view, &polish);
    ok &= require(view.itemDelegate() != delegate, "lifecycle remains active while language is disabled");
    g_enabled.store(true);
    installProxyStyle();
    QLineEdit edit(QStringLiteral("File"));
    QPlainTextEdit text(QStringLiteral("File"));
    QComboBox combo; combo.setEditable(true); combo.addItem(QStringLiteral("File"));
    QSpinBox spin;
    ok &= require(editorBoundary(&edit) && editorBoundary(text.viewport()) &&
        editorBoundary(&combo) && editorBoundary(&spin), "input and editor viewports are excluded from painting and measuring");

    QLabel label(QStringLiteral("File Options"));
    label.show();
    label.setTextFormat(Qt::PlainText);
    QImage fragment(400, 40, QImage::Format_ARGB32);
    QPainter painter(&fragment);
    UiPaintBoundary::WidgetPaintScope scope(&painter, &label);
    // The complete source is deliberately known to distinguish whole-label
    // recovery from drawing a non-elided wrapped line.
    g_translations.insert(QStringLiteral("File Options"), QStringLiteral("完整设置"));
    g_lookupCache.clear();
    ok &= require(translatePaint(&painter, QStringLiteral("File")) == QStringLiteral("文件"),
        "non-elided label fragments do not repeat the complete label");
    painter.end();

    QLabel rich(QStringLiteral("<b>LoadGeom</b>"));
    rich.resize(400, 50);
    app.installEventFilter(&lifecycle);
    QImage rendered(rich.size(), QImage::Format_ARGB32);
    const unsigned before = g_richDocumentBuilds;
    for (int i = 0; i < 100; ++i) rich.render(&rendered);
    const unsigned first = g_richDocumentBuilds;
    rich.resize(450, 50); rendered = QImage(rich.size(), QImage::Format_ARGB32); rich.render(&rendered);
    ok &= require(first == before + 1 && g_richDocumentBuilds == first + 1,
        "rich label reuses layout and invalidates it after resize");
    app.removeEventFilter(&lifecycle);

    LazyModel model;
    model.appendRow(new QStandardItem(QStringLiteral("File")));
    int budget = 100;
    collectModel(&model, QModelIndex(), [](const QString&) {}, 0, budget);
    ok &= require(model.fetches == 0, "capture never fetches lazy model data");
    ok &= require(!WrapImportHooks::resolveFunction<void(*)()>(GetModuleHandleW(L"Qt5Core.dll"),
        "_ZN7QObject16staticMetaObjectE"), "data exports cannot become executable hook targets");
    const QString qtRoot = qEnvironmentVariable("WRAP_ROOT",
        QStringLiteral("C:/Program Files/Faceform/Wrap 2025.11.14"));
    ok &= require(WrapChineseCompatibility::supportsQt5Directory(qtRoot.toStdWString()) &&
        !WrapChineseCompatibility::hasExport(L"C:\\Windows\\System32\\kernel32.dll",
            "_ZN7QObject16staticMetaObjectE"), "Qt MinGW ABI checked without loading host modules");

    installDrawTextHooks();
    ok &= require(!g_drawPatches.empty() && !WidgetDisplayMetrics::patches.empty(),
        "drawing and measurement IAT recovery records exist");
    shutdownDisplay(); shutdownDisplay();
    std::printf("shutdown delegate=%d tracking=%d actions=%d palette=%d style=%s records=%zu/%zu/%zu cache=%d\n",
        view.itemDelegate() == delegate, bar->hasMouseTracking(), bar->actions().size(),
        app.palette() == originalPalette, app.style()->objectName().toUtf8().constData(),
        g_drawPatches.size(), WidgetDisplayMetrics::patches.size(), ShortcutHintDisplay::patches.size(),
        g_richDocuments.size());
    ok &= require(view.itemDelegate() == delegate && !bar->hasMouseTracking() &&
        bar->actions().size() == 4 && app.palette() == originalPalette &&
        app.style()->objectName() == QLatin1String("fusion") &&
        g_drawPatches.empty() && WidgetDisplayMetrics::patches.empty() &&
        ShortcutHintDisplay::patches.empty() && g_richDocuments.isEmpty(),
        "repeated shutdown restores owned hooks delegates style palette menu and caches");
    std::printf("audit_runtime=%d\n", ok);
    return ok ? 0 : 1;
}
