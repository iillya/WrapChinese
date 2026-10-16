#pragma once

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QRegularExpression>

#include <cstddef>
#include <vector>

// Validates exact Qt placeholder pairs and provides a separate, bounded numeric
// matcher. Only the product allowlist is installed in the runtime catalog.
namespace WrapChinesePlaceholders {

constexpr int kMaximumPatternLength = 4096;
constexpr std::size_t kMaximumFields = 32;

struct Token final {
    bool placeholder = false;
    QString text;
    std::size_t fieldIndex = 0;
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

// Exact dictionary entries may retain general Qt placeholders. They are not
// automatically eligible for matching already-formatted runtime strings.
inline bool validatePair(const QString& source, const QString& target) {
    std::vector<Token> left, right;
    QHash<QString, int> leftFields, rightFields;
    parsePattern(source, left, leftFields);
    parsePattern(target, right, rightFields);
    return leftFields == rightFields;
}

class Catalog final {
public:
    bool addOrAssign(const QString& source, const QString& target,
                     QString* error = nullptr) {
        std::vector<Token> left, right;
        QHash<QString, int> leftFields, rightFields;
        if (source.size() > 500 || target.size() > 1000 ||
            !parsePattern(source, left, leftFields, error) ||
            !parsePattern(target, right, rightFields, error) ||
            leftFields != rightFields || left.front().placeholder ||
            left.front().text.size() < 4) return false;
        int captures = 0;
        QString expression = QStringLiteral("\\A");
        QHash<QString, int> firstCapture;
        std::vector<std::pair<int, int>> repeats;
        bool priorField = false;
        for (const Token& token : left) {
            if (!token.placeholder) {
                expression += QRegularExpression::escape(token.text);
                priorField = false;
                continue;
            }
            if (priorField || token.text.size() != 1 ||
                token.text.at(0) < QLatin1Char('1') || token.text.at(0) > QLatin1Char('9') ||
                ++captures > 8) return false;
            priorField = true;
            const int prior = firstCapture.value(token.text, 0);
            if (prior) repeats.emplace_back(prior, captures);
            else firstCapture.insert(token.text, captures);
            expression += QStringLiteral("([+-]?[0-9]{1,16}(?:\\.[0-9]{1,8})?)");
        }
        expression += QStringLiteral("\\z");
        for (Token& token : right) {
            if (token.placeholder) token.fieldIndex = firstCapture.value(token.text);
        }
        Entry entry{source, QRegularExpression(expression), std::move(right), std::move(repeats)};
        if (!entry.expression.isValid()) return false;
        entry.expression.optimize();
        for (Entry& prior : entries_) {
            if (prior.source == source) { prior = std::move(entry); return true; }
        }
        if (entries_.size() >= 64) return false;
        entries_.push_back(std::move(entry));
        return true;
    }

    void clear() { entries_.clear(); }
    std::size_t size() const noexcept { return entries_.size(); }

    QString lookup(const QString& text) const {
        if (text.isEmpty() || text.size() > 500) return {};
        QString result;
        bool matched = false;
        for (const Entry& entry : entries_) {
            if (entry.source.front() != text.front()) continue;
            const auto match = entry.expression.match(text);
            if (!match.hasMatch()) continue;
            bool consistent = true;
            for (const auto& repeat : entry.repeats)
                if (match.captured(repeat.first) != match.captured(repeat.second)) consistent = false;
            if (!consistent) continue;
            if (matched) return {}; // Ambiguity must not depend on entry order.
            matched = true;
            for (const Token& token : entry.target)
                result += token.placeholder ? match.captured(int(token.fieldIndex)) : token.text;
        }
        return result;
    }

private:
    struct Entry {
        QString source;
        QRegularExpression expression;
        std::vector<Token> target;
        std::vector<std::pair<int, int>> repeats;
    };
    std::vector<Entry> entries_;
};

} // namespace WrapChinesePlaceholders
