#pragma once

#include "import_hook.h"
#include "shortcut_hint_sources.h"
#include <QtCore/QRegularExpression>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QWidget>
#include <atomic>
#include <memory>
#include <vector>

// Wrap rasterizes context-help Markdown into reusable English pixmaps. Keep
// those host caches intact and substitute a private pixmap only when a known
// help widget displays it. Images, keycaps, source documents and exports retain
// their original data. No paint-time capture, file I/O or widget enumeration.
namespace ShortcutHintDisplay {
struct Rule {
    QRegularExpression expression;
    QStringList prose;
};
struct Edit { int position; int length; QString target; };
inline std::vector<Rule> rules;
inline QHash<QString, QString> proseSegments;
inline DWORD guiThread = 0;
inline const std::atomic_bool* enabled = nullptr;
inline thread_local bool inHook = false;
inline std::vector<WrapImportHooks::PatchRecord> patches;

class Guard final {
public:
    Guard() : previous_(inHook) { inHook = true; }
    ~Guard() { inHook = previous_; }
private:
    bool previous_;
};

inline QStringList splitKeys(const QString& text, QStringList& keys) {
    QStringList prose;
    int position = 0;
    while (position < text.size()) {
        const int begin = text.indexOf(QLatin1Char('*'), position);
        if (begin < 0) break;
        const int end = text.indexOf(QLatin1Char('*'), begin + 1);
        if (end <= begin + 1) return {};
        prose.append(text.mid(position, begin - position));
        keys.append(text.mid(begin + 1, end - begin - 1));
        position = end + 1;
    }
    prose.append(text.mid(position));
    return prose;
}

inline void initialize(QString (*lookup)(const QString&),
                       const std::atomic_bool* languageState) {
    guiThread = GetCurrentThreadId();
    enabled = languageState;
    rules.clear();
    proseSegments.clear();
    QSet<QString> ambiguousSegments;
    for (const char* sourceUtf8 : kShortcutHintSources) {
        const QString source = QString::fromUtf8(sourceUtf8);
        const QString target = lookup(source);
        QStringList sourceKeys, targetKeys;
        const QStringList sourceProse = splitKeys(source, sourceKeys);
        const QStringList targetProse = splitKeys(target, targetKeys);
        if (sourceKeys.isEmpty() || sourceKeys != targetKeys ||
            sourceProse.size() != targetProse.size()) continue;
        QString pattern = QStringLiteral("^");
        for (int index = 0; index < sourceProse.size(); ++index) {
            const QString segment = sourceProse.at(index).trimmed();
            const QString proseTarget = targetProse.at(index).trimmed();
            if (!segment.isEmpty() && !proseTarget.isEmpty() && segment != proseTarget) {
                const auto previous = proseSegments.constFind(segment);
                if (previous != proseSegments.constEnd() && *previous != proseTarget)
                    ambiguousSegments.insert(segment);
                else proseSegments.insert(segment, proseTarget);
            }
            QString literal;
            for (const QChar ch : sourceProse.at(index))
                literal += ch == QLatin1Char(' ')
                    ? QStringLiteral("[ \\t\\x{00a0}]+")
                    : QRegularExpression::escape(QString(ch));
            pattern += QLatin1Char('(') + literal + QLatin1Char(')');
            if (index < sourceKeys.size())
                pattern += QStringLiteral("(?:%1|\\x{fffc})")
                    .arg(QRegularExpression::escape(sourceKeys.at(index)));
        }
        pattern += QLatin1Char('$');
        QRegularExpression expression(pattern);
        if (expression.isValid()) rules.push_back({expression, targetProse});
    }
    for (const QString& segment : ambiguousSegments) proseSegments.remove(segment);
}

// Require every non-empty line to be an approved complete help line. A title,
// filename, editable document or unknown paragraph cannot become a help cache.
inline std::vector<Edit> plan(const QTextDocument& document) {
    if (document.characterCount() > 24000 || document.blockCount() > 256)
        return {};
    std::vector<Edit> edits;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        int start = 0;
        while (start <= text.size()) {
            int end = text.indexOf(QChar::LineSeparator, start);
            if (end < 0) end = text.size();
            int first = start, last = end;
            while (first < last && text.at(first).isSpace()) ++first;
            while (last > first && text.at(last - 1).isSpace()) --last;
            if (last > first) {
                const QString line = text.mid(first, last - first);
                bool matched = false;
                // Some Wrap views rasterize the prose between keycaps as
                // separate Markdown chunks. Match those approved full chunks
                // exactly; never translate or reconstruct a keycap.
                const auto prose = proseSegments.constFind(line);
                if (prose != proseSegments.constEnd()) {
                    edits.push_back({block.position() + first, last - first, *prose});
                    matched = true;
                }
                for (const Rule& rule : rules) {
                    if (matched) break;
                    const auto match = rule.expression.match(line);
                    if (!match.hasMatch()) continue;
                    matched = true;
                    for (int index = 0; index < rule.prose.size(); ++index) {
                        const int length = match.capturedLength(index + 1);
                        if (!length || match.captured(index + 1) == rule.prose.at(index))
                            continue;
                        edits.push_back({block.position() + first +
                            match.capturedStart(index + 1), length,
                            rule.prose.at(index)});
                    }
                    break;
                }
                if (!matched) return {};
            }
            if (end == text.size()) break;
            start = end + 1;
        }
    }
    return edits;
}

inline void applyEdits(QTextDocument& display, const std::vector<Edit>& edits) {
    // Work backwards so source offsets and all image/keycap formats stay valid.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        QTextCursor cursor(&display);
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
        cursor.insertText(it->target, cursor.charFormat());
    }
}

using DrawContents = void (*)(QTextDocument*, QPainter*, const QRectF&);
using DrawPixmapPoint = void (*)(QPainter*, const QPointF&, const QPixmap&);
using DrawPixmapRect = void (*)(QPainter*, const QRectF&, const QPixmap&, const QRectF&);
inline DrawContents originalContents = nullptr;
inline DrawPixmapPoint originalPoint = nullptr;
inline DrawPixmapRect originalRect = nullptr;

struct Cache {
    qint64 key = 0;
    QPixmap chinese;
    QSize pixelSize;
    qreal dpr = 1;
    std::unique_ptr<QTextDocument> source;
    std::vector<Edit> edits;
    QRectF rect;
    QTransform transform;
    QRect viewport, window;
    QPen pen;
    QBrush brush;
    QFont font;
    QPainterPath clip;
    QPainter::RenderHints hints;
    QPainter::CompositionMode composition;
    qreal opacity;
    bool clipped;
    quint64 bytes = 0;
};
inline std::vector<std::unique_ptr<Cache>> caches;
inline quint64 cacheBytes = 0;
inline QHash<qint64, Cache*> cacheIndex;
constexpr quint64 kMaximumCacheBytes = 32 * 1024 * 1024;
constexpr std::size_t kMaximumCaches = 512;

inline void clearCaches() { cacheIndex.clear(); caches.clear(); cacheBytes = 0; }
inline void shutdown() {
    // The DLL is process-pinned. Retiring these import slots cannot unload code
    // from an in-flight passthrough callback on another thread.
    WrapImportHooks::restoreImports(patches);
    clearCaches();
}

inline bool isHelpPainter(const QPainter* painter) {
    if (!painter || !painter->device() ||
        painter->device()->devType() != QInternal::Widget) return false;
    const auto* widget = static_cast<const QWidget*>(painter->device());
    for (int depth = 0; widget && depth < 16; ++depth, widget = widget->parentWidget())
        if (widget->inherits("Wrap::WContextHelpHintBase")) return true;
    return false;
}

inline const QPixmap& displayed(QPainter* painter, const QPixmap& source) {
    if (inHook || GetCurrentThreadId() != guiThread || !enabled ||
        !enabled->load(std::memory_order_acquire))
        return source;
    const auto found = cacheIndex.constFind(source.cacheKey());
    if (found == cacheIndex.constEnd() || !isHelpPainter(painter)) return source;
    {
        Cache& cache = **found;
        if (cache.chinese.isNull()) {
            Guard guard;
            // Keep the small rendering recipe when old raster variants expire.
            // The host may still own its English cache and reuse it much later.
            for (auto& old : caches) {
                if (cacheBytes + cache.bytes <= kMaximumCacheBytes) break;
                if (!old->chinese.isNull()) {
                    cacheBytes -= old->bytes;
                    old->chinese = QPixmap();
                }
            }
            // Wrap's Markdown helper constructs a transparent QPixmap before
            // drawContents (verified statically); icons live in the document.
            cache.chinese = QPixmap(cache.pixelSize);
            if (cache.chinese.isNull()) return source;
            cache.chinese.setDevicePixelRatio(cache.dpr);
            cache.chinese.fill(Qt::transparent);
            cacheBytes += cache.bytes;
            std::unique_ptr<QTextDocument> display(cache.source->clone());
            applyEdits(*display, cache.edits);
            QPainter output(&cache.chinese);
            if (!output.isActive()) {
                cache.chinese = QPixmap();
                cacheBytes -= cache.bytes;
                return source;
            }
            output.setViewport(cache.viewport);
            output.setWindow(cache.window);
            output.setWorldTransform(cache.transform);
            output.setPen(cache.pen);
            output.setBrush(cache.brush);
            output.setFont(cache.font);
            output.setRenderHints(cache.hints);
            output.setCompositionMode(cache.composition);
            output.setOpacity(cache.opacity);
            if (cache.clipped) output.setClipPath(cache.clip);
            originalContents(display.get(), &output, cache.rect);
        }
        return cache.chinese;
    }
    return source;
}

inline void hookedContents(QTextDocument* document, QPainter* painter,
                           const QRectF& rect) {
    if (!originalContents) return;
    if (inHook || GetCurrentThreadId() != guiThread || !document || !painter ||
        !painter->device() || painter->device()->devType() != QInternal::Pixmap) {
        originalContents(document, painter, rect);
        return;
    }
    Guard guard;
    auto* pixmap = static_cast<QPixmap*>(painter->device());
    const quint64 bytes = quint64(pixmap->width()) * pixmap->height() * 4;
    const auto edits = bytes <= kMaximumCacheBytes ? plan(*document) : std::vector<Edit>();
    std::unique_ptr<Cache> cache;
    if (!edits.empty()) {
        cache = std::make_unique<Cache>();
        cache->pixelSize = pixmap->size();
        cache->dpr = pixmap->devicePixelRatio();
        cache->source.reset(document->clone());
        cache->edits = edits;
        cache->rect = rect;
        cache->transform = painter->worldTransform();
        cache->viewport = painter->viewport();
        cache->window = painter->window();
        cache->pen = painter->pen();
        cache->brush = painter->brush();
        cache->font = painter->font();
        cache->clip = painter->clipPath();
        cache->clipped = painter->hasClipping();
        cache->hints = painter->renderHints();
        cache->composition = painter->compositionMode();
        cache->opacity = painter->opacity();
        cache->bytes = bytes;
    }
    // Always rasterize the source into the host's pixmap, even in Chinese mode.
    originalContents(document, painter, rect);
    if (!cache) return;
    cache->key = pixmap->cacheKey();
    if (cacheIndex.contains(cache->key)) {
        for (auto it = caches.begin(); it != caches.end(); ++it) {
            if ((*it)->key != cache->key) continue;
            if (!(*it)->chinese.isNull()) cacheBytes -= (*it)->bytes;
            caches.erase(it);
            break;
        }
    }
    while (caches.size() >= kMaximumCaches) {
        if (!caches.front()->chinese.isNull()) cacheBytes -= caches.front()->bytes;
        cacheIndex.remove(caches.front()->key);
        caches.erase(caches.begin());
    }
    cacheIndex.insert(cache->key, cache.get());
    caches.push_back(std::move(cache));
}

inline void hookedPoint(QPainter* painter, const QPointF& point, const QPixmap& pixmap) {
    const QPixmap& display = displayed(painter, pixmap);
    Guard guard;
    if (originalPoint) originalPoint(painter, point, display);
}
inline void hookedRect(QPainter* painter, const QRectF& rect, const QPixmap& pixmap,
                       const QRectF& sourceRect) {
    const QPixmap& display = displayed(painter, pixmap);
    Guard guard;
    if (originalRect) originalRect(painter, rect, display, sourceRect);
}


inline void resolveHooks(HMODULE gui) {
    originalContents = WrapImportHooks::resolveFunction<DrawContents>(gui,
        "_ZN13QTextDocument12drawContentsEP8QPainterRK6QRectF");
    originalPoint = WrapImportHooks::resolveFunction<DrawPixmapPoint>(gui,
        "_ZN8QPainter10drawPixmapERK7QPointFRK7QPixmap");
    originalRect = WrapImportHooks::resolveFunction<DrawPixmapRect>(gui,
        "_ZN8QPainter10drawPixmapERK6QRectFRK7QPixmapS2_");
}
inline void patchModule(HMODULE module) {
    if (!originalContents || !originalPoint || !originalRect) return;
    using WrapImportHooks::functionAddress;
    const WrapImportHooks::Replacement symbols[] = {
        {functionAddress(originalContents), functionAddress(&hookedContents)},
        {functionAddress(originalPoint), functionAddress(&hookedPoint)},
        {functionAddress(originalRect), functionAddress(&hookedRect)},
    };
    WrapImportHooks::replaceImports(module, symbols, sizeof(symbols) / sizeof(symbols[0]), &patches);
}
} // namespace ShortcutHintDisplay
