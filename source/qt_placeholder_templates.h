#pragma once

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QStringView>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

// Matches already-formatted Qt strings against dictionary entries containing
// %1..%99, %L1..%L99 or %n.  Parsing tokens instead of building a regular
// expression keeps placeholder numbering correct for %10, reordering and
// repeated fields, and bounds adversarial backtracking.
namespace WrapChinesePlaceholders {

constexpr int kMaximumPatternLength = 4096;
constexpr int kMaximumCapturedLength = 2048;
constexpr std::size_t kMaximumFields = 32;
constexpr int kMaximumMatchSteps = 512;

struct Token final {
    bool placeholder = false;
    QString text;
    std::size_t fieldIndex = 0;
};

struct Template final {
    QString source;
    std::vector<Token> sourceTokens;
    std::vector<Token> translationTokens;
    int literalLength = 0;
};

inline void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

inline void appendLiteral(std::vector<Token>& tokens, QChar value) {
    if (!tokens.empty() && !tokens.back().placeholder)
        tokens.back().text += value;
    else
        tokens.push_back({false, QString(value), 0});
}

inline bool placeholderAt(const QString& pattern, int position,
                          QString& name, int& length) {
    name.clear();
    length = 0;
    if (position < 0 || position >= pattern.size() ||
        pattern[position] != QLatin1Char('%') || position + 1 >= pattern.size()) {
        return false;
    }
    int digit = position + 1;
    if (pattern[digit] == QLatin1Char('n')) {
        name = QStringLiteral("n");
        length = 2;
        return true;
    }
    if (pattern[digit] == QLatin1Char('L')) ++digit;
    if (digit >= pattern.size() || pattern[digit] < QLatin1Char('1') ||
        pattern[digit] > QLatin1Char('9')) {
        return false;
    }
    int end = digit + 1;
    if (end < pattern.size() && pattern[end] >= QLatin1Char('0') &&
        pattern[end] <= QLatin1Char('9')) {
        ++end;
    }
    name = pattern.mid(position + 1, end - position - 1);
    length = end - position;
    return true;
}

inline bool parsePattern(const QString& pattern, std::vector<Token>& tokens,
                         QHash<QString, int>& fields,
                         QString* error = nullptr) {
    tokens.clear();
    fields.clear();
    if (pattern.isEmpty() || pattern.size() > kMaximumPatternLength) {
        setError(error, QStringLiteral("pattern is empty or too long"));
        return false;
    }
    for (int position = 0; position < pattern.size();) {
        QString name;
        int length = 0;
        if (!placeholderAt(pattern, position, name, length)) {
            appendLiteral(tokens, pattern[position++]);
            continue;
        }
        ++fields[name];
        if (fields.size() > static_cast<int>(kMaximumFields)) {
            setError(error, QStringLiteral("too many placeholder fields"));
            return false;
        }
        tokens.push_back({true, name, 0});
        position += length;
    }
    if (fields.isEmpty()) {
        setError(error, QStringLiteral("pattern contains no Qt placeholder"));
        return false;
    }
    return true;
}

class Catalog final {
public:
    bool addOrAssign(const QString& source, const QString& translation,
                     QString* error = nullptr) {
        std::vector<Token> sourceTokens;
        std::vector<Token> translationTokens;
        QHash<QString, int> sourceFields;
        QHash<QString, int> translationFields;
        if (!parsePattern(source, sourceTokens, sourceFields, error) ||
            !parsePattern(translation, translationTokens, translationFields,
                          error)) {
            return false;
        }
        for (std::size_t index = 1; index < sourceTokens.size(); ++index) {
            if (sourceTokens[index - 1].placeholder &&
                sourceTokens[index].placeholder) {
                setError(error, QStringLiteral(
                    "adjacent source placeholders are ambiguous"));
                return false;
            }
        }
        if (sourceFields != translationFields) {
            setError(error, QStringLiteral(
                "source and translation placeholders must match exactly"));
            return false;
        }

        QHash<QString, std::size_t> fieldIndices;
        std::size_t nextField = 0;
        for (Token& token : sourceTokens) {
            if (!token.placeholder) continue;
            const auto found = fieldIndices.constFind(token.text);
            if (found == fieldIndices.constEnd()) {
                token.fieldIndex = nextField;
                fieldIndices.insert(token.text, nextField++);
            } else {
                token.fieldIndex = found.value();
            }
        }
        for (Token& token : translationTokens) {
            if (token.placeholder) token.fieldIndex = fieldIndices.value(token.text);
        }

        int literalLength = 0;
        for (const Token& token : sourceTokens)
            if (!token.placeholder) literalLength += token.text.size();
        Template value{source, std::move(sourceTokens),
                       std::move(translationTokens), literalLength};
        for (Template& existing : templates_) {
            if (existing.source == source) {
                existing = std::move(value);
                finalized_ = false;
                return true;
            }
        }
        templates_.push_back(std::move(value));
        finalized_ = false;
        return true;
    }

    void finalize() {
        std::sort(templates_.begin(), templates_.end(),
                  [](const Template& left, const Template& right) {
            if (left.literalLength != right.literalLength)
                return left.literalLength > right.literalLength;
            return left.source < right.source;
        });
        buckets_.clear();
        prefixless_.clear();
        for (std::size_t index = 0; index < templates_.size(); ++index) {
            const auto& tokens = templates_[index].sourceTokens;
            if (!tokens.empty() && !tokens.front().placeholder &&
                !tokens.front().text.isEmpty()) {
                buckets_[tokens.front().text.front().unicode()].push_back(index);
            } else {
                prefixless_.push_back(index);
            }
        }
        finalized_ = true;
    }

    QString lookup(const QString& text) const {
        if (!finalized_ || text.isEmpty() ||
            text.size() > kMaximumPatternLength) return {};
        const auto found = buckets_.constFind(text.front().unicode());
        if (found != buckets_.constEnd()) {
            const QString result = lookupIndices(text, found.value());
            if (!result.isEmpty()) return result;
        }
        return lookupIndices(text, prefixless_);
    }

    void clear() {
        templates_.clear();
        buckets_.clear();
        prefixless_.clear();
        finalized_ = true;
    }

    std::size_t size() const noexcept { return templates_.size(); }

private:
    struct MatchState final {
        std::array<QString, kMaximumFields> values;
        std::array<bool, kMaximumFields> assigned{};
    };

    static bool matchTokens(const std::vector<Token>& tokens,
                            std::size_t tokenIndex, const QString& text,
                            int position, MatchState& state,
                            int& steps) {
        if (++steps > kMaximumMatchSteps) return false;
        if (tokenIndex == tokens.size()) return position == text.size();
        const Token& token = tokens[tokenIndex];
        if (!token.placeholder) {
            if (position > text.size() - token.text.size() ||
                QStringView(text).mid(position, token.text.size()) !=
                    QStringView(token.text)) return false;
            return matchTokens(tokens, tokenIndex + 1, text,
                               position + token.text.size(), state, steps);
        }

        const std::size_t field = token.fieldIndex;
        if (state.assigned[field]) {
            const QString& captured = state.values[field];
            if (position > text.size() - captured.size() ||
                QStringView(text).mid(position, captured.size()) !=
                    QStringView(captured)) return false;
            return matchTokens(tokens, tokenIndex + 1, text,
                               position + captured.size(), state, steps);
        }

        const int maximumEnd =
            std::min(text.size(), position + kMaximumCapturedLength);
        if (tokenIndex + 1 == tokens.size()) {
            if (position >= text.size() || text.size() > maximumEnd) return false;
            MatchState branch = state;
            branch.values[field] = text.mid(position);
            branch.assigned[field] = true;
            if (matchTokens(tokens, tokenIndex + 1, text, text.size(), branch,
                            steps)) {
                state = std::move(branch);
                return true;
            }
            return false;
        }

        const QString& delimiter = tokens[tokenIndex + 1].text;
        int end = text.indexOf(delimiter, position + 1);
        while (end >= 0 && end <= maximumEnd) {
            MatchState branch = state;
            branch.values[field] = text.mid(position, end - position);
            branch.assigned[field] = true;
            if (matchTokens(tokens, tokenIndex + 1, text, end, branch, steps)) {
                state = std::move(branch);
                return true;
            }
            end = text.indexOf(delimiter, end + 1);
        }
        return false;
    }

    QString lookupIndices(const QString& text,
                          const std::vector<std::size_t>& indices) const {
        for (const std::size_t index : indices) {
            const Template& value = templates_[index];
            MatchState state;
            int steps = 0;
            if (!matchTokens(value.sourceTokens, 0, text, 0, state, steps))
                continue;
            QString result;
            result.reserve(std::min(kMaximumPatternLength,
                                    text.size() + value.literalLength));
            for (const Token& token : value.translationTokens) {
                if (token.placeholder) result += state.values[token.fieldIndex];
                else result += token.text;
                if (result.size() > kMaximumPatternLength) return {};
            }
            return result;
        }
        return {};
    }

    std::vector<Template> templates_;
    QHash<unsigned short, std::vector<std::size_t>> buckets_;
    std::vector<std::size_t> prefixless_;
    bool finalized_ = true;
};

} // namespace WrapChinesePlaceholders
