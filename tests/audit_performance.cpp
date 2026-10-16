#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef WRAP_AUDIT_BASELINE
#include "../build/audit-before/source/hook.cpp"
#else
#include "../source/hook.cpp"
#endif
#include <cstdio>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    g_pluginDir = QString::fromLocal8Bit(argv[1]);
    loadDictionaries();
    QStringList sources;
    const auto dictionary = QJsonDocument::fromJson([&] {
        QFile f(g_pluginDir + QStringLiteral("/translations/dictionary_zh.json"));
        f.open(QIODevice::ReadOnly); return f.readAll();
    }()).object().value(QStringLiteral("translations")).toObject();
    for (auto i = dictionary.constBegin(); i != dictionary.constEnd() && sources.size() < 900; ++i)
        if (i.key().size() < 80 && !i.key().contains(QLatin1Char('%'))) sources.append(i.key());
    QElapsedTimer timer;
    volatile int checksum = 0;
    constexpr int queries = 300000;
    timer.start();
    for (int i = 0; i < queries; ++i) checksum += translated(sources.at(i % sources.size())).size();
    const qint64 queryNs = timer.nsecsElapsed();
    QLabel label(QStringLiteral("<b>LoadGeom</b>"));
    label.resize(500, 60);
    ApplicationFilter filter;
    app.installEventFilter(&filter);
    QImage image(label.size(), QImage::Format_ARGB32);
    constexpr int paints = 1000;
    timer.restart();
    for (int i = 0; i < paints; ++i) label.render(&image);
    const qint64 richNs = timer.nsecsElapsed();
    app.removeEventFilter(&filter);
    std::printf("{\"queries\":%d,\"query_ns\":%lld,\"rich_paints\":%d,\"rich_ns\":%lld,\"checksum\":%d}\n",
        queries, static_cast<long long>(queryNs), paints, static_cast<long long>(richNs), checksum);
    return 0;
}
