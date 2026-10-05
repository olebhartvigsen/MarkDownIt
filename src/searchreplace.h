#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWord = false;
};

struct TextMatch {
    uint32_t start = 0;
    uint32_t length = 0;
};

std::vector<TextMatch> FindTextMatches(const std::string& text,
                                        const std::string& query,
                                        SearchOptions options = {});

// Range filter for FindTextMatchesScoped. Return false to keep a candidate
// range out of the result. The filter is how the caller limits searching to
// what it can actually show and edit, for example rendered document text
// only, or only the bytes inside the current selection.
using SearchRangeFilter = bool (*)(uint32_t start, uint32_t length,
                                   void* context);

// FindTextMatches with an explicit search scope.
//
// Two rules on top of the plain matcher:
//   * A candidate the filter rejects is skipped, not consumed, so a match
//     inside a rejected range is still found when it continues on the next
//     character.
//   * A match never spans a line break. Searching "e\nA" therefore finds
//     nothing, and a match can never join the end of one block with the
//     start of the next (find-replace guide section 19). Pass a filter that
//     accepts everything to search the raw text including hidden Markdown
//     syntax and line breaks.
std::vector<TextMatch> FindTextMatchesScoped(const std::string& text,
                                             const std::string& query,
                                             SearchOptions options,
                                             SearchRangeFilter filter,
                                             void* context);

std::string ReplaceTextMatches(const std::string& text,
                               const std::vector<TextMatch>& matches,
                               const std::string& replacement);

bool IsSearchWordCharacter(unsigned char c);
