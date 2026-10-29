#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instaseek::core {

struct MatcherOptions {
    std::string pattern;
    bool case_sensitive = false;
    bool whole_word = false;
    bool regex = false;
};

struct MatchSpan {
    std::size_t start = 0;
    std::size_t length = 0;
};

enum class MatcherError { None, EmptyPattern, MultilinePattern, InvalidRegex };

class TextMatcher {
public:
    struct Created {
        std::shared_ptr<const TextMatcher> matcher;
        MatcherError error = MatcherError::None;
        std::string detail;
    };

    static Created create(const MatcherOptions& options);
    static std::vector<std::string> alternatives(const MatcherOptions& options);

    ~TextMatcher();
    TextMatcher(const TextMatcher&) = delete;
    TextMatcher& operator=(const TextMatcher&) = delete;

    std::optional<MatchSpan> find(std::string_view text, std::size_t from) const;
    const MatcherOptions& options() const;

private:
    struct Impl;
    explicit TextMatcher(std::unique_ptr<Impl> impl);
    std::optional<MatchSpan> findRaw(std::string_view text, std::size_t from) const;

    std::unique_ptr<Impl> impl_;
};

}
