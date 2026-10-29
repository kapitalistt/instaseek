#include "core/text_matcher.hpp"

#include "core/utf8.hpp"

#include <re2/re2.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace instaseek::core {

namespace {

template <typename Signature>
struct Re2MatchTraits;

template <typename Result, typename Class, typename Text, typename Piece>
struct Re2MatchTraits<Result (Class::*)(Text, std::size_t, std::size_t, RE2::Anchor, Piece*, int) const> {
    using PieceType = Piece;
};

using Re2Piece = Re2MatchTraits<decltype(&RE2::Match)>::PieceType;

std::string escapeForRegex(std::string_view literal) {
    static constexpr std::string_view special = "\\^$.|?*+()[]{}";
    std::string escaped;
    escaped.reserve(literal.size() * 2);
    for (const char c : literal) {
        if (special.find(c) != std::string_view::npos) escaped.push_back('\\');
        escaped.push_back(c);
    }
    return escaped;
}

std::vector<std::string> literalAlternatives(const std::string& pattern) {
    if (pattern.find('|') == std::string::npos) return {pattern};
    std::vector<std::string> alternatives;
    std::size_t start = 0;
    while (start <= pattern.size()) {
        const std::size_t bar = pattern.find('|', start);
        const std::size_t end = bar == std::string::npos ? pattern.size() : bar;
        std::string_view piece(pattern.data() + start, end - start);
        while (!piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) piece.remove_prefix(1);
        while (!piece.empty() && (piece.back() == ' ' || piece.back() == '\t')) piece.remove_suffix(1);
        if (!piece.empty()) alternatives.emplace_back(piece);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (alternatives.empty()) alternatives.push_back(pattern);
    return alternatives;
}

}

std::vector<std::string> TextMatcher::alternatives(const MatcherOptions& options) {
    if (options.regex) return {options.pattern};
    return literalAlternatives(options.pattern);
}

struct TextMatcher::Impl {
    MatcherOptions options;
    std::string literal;
    std::optional<std::boyer_moore_horspool_searcher<const char*>> searcher;
    std::unique_ptr<RE2> regex;
};

TextMatcher::TextMatcher(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

TextMatcher::~TextMatcher() = default;

TextMatcher::Created TextMatcher::create(const MatcherOptions& options) {
    Created created;
    if (options.pattern.empty()) {
        created.error = MatcherError::EmptyPattern;
        return created;
    }
    if (options.pattern.find_first_of("\r\n") != std::string::npos) {
        created.error = MatcherError::MultilinePattern;
        return created;
    }

    auto impl = std::make_unique<Impl>();
    impl->options = options;
    const std::vector<std::string> pieces = alternatives(options);
    const bool alternation = !options.regex && pieces.size() > 1;
    if (!options.regex && !alternation && options.case_sensitive) {
        impl->literal = pieces.front();
        const char* first = impl->literal.data();
        impl->searcher.emplace(first, first + impl->literal.size());
    } else {
        RE2::Options re2_options;
        re2_options.set_case_sensitive(options.case_sensitive);
        re2_options.set_literal(!options.regex && !alternation);
        re2_options.set_never_nl(true);
        re2_options.set_log_errors(false);
        re2_options.set_max_mem(int64_t{64} << 20);
        std::string expression;
        if (options.regex) {
            expression = "(?m)" + options.pattern;
        } else if (alternation) {
            for (const std::string& piece : pieces) {
                if (!expression.empty()) expression.push_back('|');
                expression += escapeForRegex(piece);
            }
        } else {
            expression = pieces.front();
        }
        impl->regex = std::make_unique<RE2>(expression, re2_options);
        if (!impl->regex->ok()) {
            created.error = MatcherError::InvalidRegex;
            created.detail = impl->regex->error();
            return created;
        }
    }
    created.matcher = std::shared_ptr<const TextMatcher>(new TextMatcher(std::move(impl)));
    return created;
}

const MatcherOptions& TextMatcher::options() const { return impl_->options; }

std::optional<MatchSpan> TextMatcher::findRaw(std::string_view text, std::size_t from) const {
    if (from > text.size()) return std::nullopt;
    if (impl_->searcher) {
        const char* begin = text.data();
        const char* end = begin + text.size();
        const auto found = (*impl_->searcher)(begin + from, end);
        if (found.first == end) return std::nullopt;
        return MatchSpan{static_cast<std::size_t>(found.first - begin), impl_->literal.size()};
    }
    Re2Piece match;
    const Re2Piece whole(text.data(), text.size());
    if (!impl_->regex->Match(whole, from, text.size(), RE2::UNANCHORED, &match, 1)) return std::nullopt;
    return MatchSpan{static_cast<std::size_t>(match.data() - text.data()), static_cast<std::size_t>(match.size())};
}

std::optional<MatchSpan> TextMatcher::find(std::string_view text, std::size_t from) const {
    while (from <= text.size()) {
        const auto match = findRaw(text, from);
        if (!match) return std::nullopt;
        if (!impl_->options.whole_word || utf8::isWordBoundary(text, match->start, match->start + match->length))
            return match;
        const std::size_t step = match->start < text.size() ? utf8::decode(text, match->start).length : 1;
        from = match->start + std::max<std::size_t>(1, step);
    }
    return std::nullopt;
}

}
