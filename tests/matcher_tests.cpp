#include "test_harness.hpp"

#include "core/text_matcher.hpp"

using namespace instaseek::core;

namespace {

std::shared_ptr<const TextMatcher> make(std::string pattern, bool case_sensitive, bool whole_word, bool regex) {
    return TextMatcher::create({std::move(pattern), case_sensitive, whole_word, regex}).matcher;
}

}

INSTASEEK_TEST(literal_case_sensitive_search) {
    const auto matcher = make("needle", true, false, false);
    CHECK(matcher != nullptr);
    const auto match = matcher->find("hay needle hay Needle", 0);
    CHECK(match && match->start == 4 && match->length == 6);
    CHECK(!matcher->find("hay NEEDLE", 0));
    const auto second = matcher->find("needle needle", 1);
    CHECK(second && second->start == 7);
}

INSTASEEK_TEST(case_insensitive_search_folds_cyrillic) {
    const auto matcher = make("привет", false, false, false);
    CHECK(matcher != nullptr);
    const std::string text = "Он сказал: ПРИВЕТ!";
    const auto match = matcher->find(text, 0);
    CHECK(match && text.substr(match->start, match->length) == "ПРИВЕТ");
    const auto latin = make("Error", false, false, false);
    CHECK(latin->find("[error] disk full", 0).has_value());
    const auto special = make("a.b(c)", false, false, false);
    CHECK(!special->find("axb(c)", 0));
    CHECK(special->find("A.B(C)", 0).has_value());
}

INSTASEEK_TEST(whole_word_respects_unicode_letters) {
    const auto matcher = make("cat", false, true, false);
    const std::string text = "concatenate cat_ cat.";
    const auto match = matcher->find(text, 0);
    CHECK(match && match->start == 17);
    const auto russian = make("кот", false, true, false);
    const std::string russian_text = "котик и кот";
    const auto russian_match = russian->find(russian_text, 0);
    CHECK(russian_match && russian_text.substr(russian_match->start) == "кот");
}

INSTASEEK_TEST(regular_expressions_use_re2) {
    const auto matcher = make(R"(\d{3}-\d{4})", false, false, true);
    CHECK(matcher != nullptr);
    const auto match = matcher->find("call 555-1234 now", 0);
    CHECK(match && match->start == 5 && match->length == 8);

    const auto anchored = make("^b", true, false, true);
    const auto line_start = anchored->find("ab\nbc", 0);
    CHECK(line_start && line_start->start == 3);

    const auto never_newline = make("a.b", true, false, true);
    CHECK(!never_newline->find("a\nb", 0));
}

INSTASEEK_TEST(vertical_bar_searches_any_alternative) {
    const auto either = make("error | warning", true, false, false);
    CHECK(either != nullptr);
    const std::string text = "ok; warning: low disk; error: full";
    const auto first = either->find(text, 0);
    CHECK(first && text.substr(first->start, first->length) == "warning");
    const auto second = either->find(text, first->start + first->length);
    CHECK(second && text.substr(second->start, second->length) == "error");

    const auto special = make("a.b|(x)", true, false, false);
    CHECK(!special->find("axb", 0));
    CHECK(special->find("call (x) now", 0).has_value());

    const auto alternatives = TextMatcher::alternatives({"one| two |", false, false, false});
    CHECK(alternatives.size() == 2 && alternatives[0] == "one" && alternatives[1] == "two");
    CHECK(TextMatcher::alternatives({"a|b", false, false, true}).size() == 1);
    CHECK(TextMatcher::alternatives({"|", false, false, false}).size() == 1);
}

INSTASEEK_TEST(invalid_patterns_are_reported) {
    CHECK(TextMatcher::create({"(", false, false, true}).error == MatcherError::InvalidRegex);
    CHECK(!TextMatcher::create({"(", false, false, true}).detail.empty());
    CHECK(TextMatcher::create({"", false, false, false}).error == MatcherError::EmptyPattern);
    CHECK(TextMatcher::create({"a\nb", false, false, false}).error == MatcherError::MultilinePattern);
    CHECK(TextMatcher::create({"(", false, false, false}).matcher != nullptr);
}
