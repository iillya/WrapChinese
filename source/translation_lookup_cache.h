#pragma once

#include <QtCore/QHashFunctions>
#include <QtCore/QString>

#include <array>
#include <cstddef>
#include <utility>

// Paint and font-metric hooks see the same labels many times per frame.  A
// small per-thread cache stores both hits and misses without introducing a
// lock into those hot paths.
namespace WrapChineseLookupCache {

template <std::size_t SlotCount = 256>
class DirectMapped final {
    static_assert(SlotCount > 0 && (SlotCount & (SlotCount - 1)) == 0,
                  "SlotCount must be a power of two");

public:
    template <typename Resolver>
    QString lookup(const QString& source, Resolver&& resolver) {
        const std::size_t hash = qHash(source);
        Entry& entry = entries_[hash & (SlotCount - 1)];
        if (entry.valid && entry.hash == hash && entry.source == source)
            return entry.translation;

        QString translation = std::forward<Resolver>(resolver)();
        entry.hash = hash;
        entry.source = source;
        entry.translation = translation;
        entry.valid = true;
        return translation;
    }

    void clear() noexcept {
        for (Entry& entry : entries_) entry = Entry{};
    }

private:
    struct Entry final {
        std::size_t hash = 0;
        QString source;
        QString translation;
        bool valid = false;
    };

    std::array<Entry, SlotCount> entries_{};
};

} // namespace WrapChineseLookupCache
