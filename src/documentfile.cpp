#include "documentfile.h"

#include <cctype>
#include <cstring>

bool PathHasExtension(const std::string& path, const char* ext) {
    const size_t n = std::strlen(ext);
    if (path.size() < n) return false;
    const char* tail = path.c_str() + path.size() - n;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower(static_cast<unsigned char>(tail[i])) !=
            std::tolower(static_cast<unsigned char>(ext[i]))) {
            return false;
        }
    }
    return true;
}

bool HasLangFence(const std::string& text,
                  const std::string& lang) {
    size_t i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        std::string line = text.substr(i,
            (eol == std::string::npos ? text.size() : eol) - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Accept leading indentation of up to 3 spaces (CommonMark).
        size_t b = 0;
        while (b < line.size() && b < 3 && line[b] == ' ') b++;
        if (line.compare(b, 3, "```") == 0) {
            std::string info = line.substr(b + 3);
            if (!info.empty() && info[0] == ' ') info.erase(0, info.find_first_not_of(" \t"));
            if (info == lang) return true;
        }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    return false;
}

// Wrap bare source of the given language in a fence that the
// markdown parser promotes to a rendered block.
std::string WrapLangFence(const std::string& lang,
                                 const std::string& text) {
    std::string out = "```" + lang + "\n";
    out += text;
    if (out.empty() || out.back() != '\n') out += '\n';
    if (text.empty()) out += '\n';  // keep the code block non-empty
    out += "```\n";
    return out;
}

// Strip the synthetic fence added by WrapLangFence. Removes at
// most one opening fence right at the top and a matching closing
// fence at the end, so content that legitimately contains other
// fences stays intact when the wrap was skipped.
std::string UnwrapLangFence(const std::string& lang,
                                   const std::string& text) {
    const std::string openingFence = "```" + lang;
    const size_t fl = openingFence.size();
    size_t b = 0;
    while (b < text.size() && text[b] == '\n') b++;
    bool opening = (text.size() >= b + fl &&
        text.compare(b, fl, openingFence) == 0 &&
        (b + fl == text.size() || text[b + fl] == '\n' ||
         text[b + fl] == '\r'));
    if (!opening) return text;
    size_t i = b + fl;
    if (i < text.size() && (text[i] == '\n' || text[i] == '\r')) i++;
    if (i < text.size() && text[i - 1] == '\r' && text[i] == '\n') i++;
    size_t j = text.find("```", i);
    // Closing fence must sit at a line start to be ours.
    while (j != std::string::npos) {
        if (j == 0 || text[j - 1] == '\n') break;
        j = text.find("```", j + 1);
    }
    if (j == std::string::npos) return text;
    size_t e = j + 3;
    if (e < text.size() && text[e] == '\r') e++;
    if (e < text.size() && text[e] == '\n') e++;
    std::string inner = text.substr(i, j - i);
    // Empty file: wrap added one blank line; drop it on unwrap so
    // an empty file stays empty.
    if (inner == "\n") return std::string();
    return inner;
}

std::string DecodeFileText(const std::string& raw,
                           const std::string& pathUtf8, FileFormat* format) {
    FileFormat f;
    f.hasBom = raw.size() >= 3 &&
               static_cast<unsigned char>(raw[0]) == 0xEF &&
               static_cast<unsigned char>(raw[1]) == 0xBB &&
               static_cast<unsigned char>(raw[2]) == 0xBF;
    std::string text = f.hasBom ? raw.substr(3) : raw;
    // Mermaid source travels as .mmd, the extension the CLI writes, and
    // as .mermaid, the one the editor plugins write. Both are the same
    // bare source, so both open as a diagram.
    f.isMmd = !pathUtf8.empty() &&
              (PathHasExtension(pathUtf8, ".mmd") ||
               PathHasExtension(pathUtf8, ".mermaid"));
    f.isSvg = !pathUtf8.empty() && PathHasExtension(pathUtf8, ".svg");
    if (f.isMmd && !HasLangFence(text, "mermaid")) {
        text = WrapLangFence("mermaid", text);
        f.mmdWrapped = true;
    }
    if (f.isSvg && !HasLangFence(text, "svg")) {
        text = WrapLangFence("svg", text);
        f.svgWrapped = true;
    }
    f.crlf = text.find("\r\n") != std::string::npos;
    if (format) *format = f;
    return text;
}

std::string EncodeFileText(const std::string& text, const FileFormat& format) {
    std::string content = text;
    if (format.isMmd && format.mmdWrapped) {
        content = UnwrapLangFence("mermaid", content);
    }
    if (format.isSvg && format.svgWrapped) {
        content = UnwrapLangFence("svg", content);
    }
    std::string out;
    if (format.hasBom) out += "\xEF\xBB\xBF";
    if (!format.crlf) {
        out += content;
        return out;
    }
    // CR followed by LF passes through as a pair, a lone LF or a lone CR
    // becomes CRLF.
    for (size_t i = 0; i < content.size(); i++) {
        if (content[i] == '\r') {
            out += "\r\n";
            if (i + 1 < content.size() && content[i + 1] == '\n') i++;
        } else if (content[i] == '\n') {
            out += "\r\n";
        } else {
            out += content[i];
        }
    }
    return out;
}
