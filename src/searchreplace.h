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

std::string ReplaceTextMatches(const std::string& text,
                               const std::vector<TextMatch>& matches,
                               const std::string& replacement);

bool IsSearchWordCharacter(unsigned char c);
