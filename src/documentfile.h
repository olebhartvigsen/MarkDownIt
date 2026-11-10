// What a file on disk becomes in the editor, and back (plan 4.1, batch 4
// family 6). Portable text work, no window and no file API: the ports
// read and write the bytes, this decides what the buffer holds and what
// goes back to disk.
//
// A file arrives with a byte order mark or without, with CRLF or LF line
// ends, and a standalone .mmd or .svg file is bare source that the
// editor shows inside a synthetic fence so the normal pipeline renders
// it. Saving undoes all of it, so the file keeps the form it was opened
// in. Windows and macOS used to carry this twice (three times on
// Windows, with Reload), and the mac copy knew neither the BOM nor the
// .mmd fence.

#pragma once

#include <string>

struct FileFormat {
    bool hasBom = false;
    bool crlf = false;
    bool isMmd = false;
    bool isSvg = false;
    bool mmdWrapped = false;
    bool svgWrapped = false;
};

// Whether the path ends in the extension, compared in lower case.
// The extension includes the dot, for example ".mmd".
bool PathHasExtension(const std::string& pathUtf8, const char* ext);

// Whether the text has a fence of the language, wraps bare source in
// one, and takes the synthetic fence away again.
bool HasLangFence(const std::string& text, const std::string& lang);
std::string WrapLangFence(const std::string& lang, const std::string& text);
std::string UnwrapLangFence(const std::string& lang, const std::string& text);

// The buffer text for the bytes of a file. An empty path means text with
// no file behind it (an import), so no fence is added for it.
std::string DecodeFileText(const std::string& raw,
                           const std::string& pathUtf8, FileFormat* format);

// The bytes to write for the buffer text: the fence taken away, the line
// ends and the BOM put back.
std::string EncodeFileText(const std::string& text, const FileFormat& format);
