#pragma once

#include <string>

// One place per user-visible find string (find spec: all user-visible
// strings are localizable). The model, the Windows bar and the mac bar
// all read their strings from here, so a translation table can replace
// this header wholesale without touching any behaviour.
namespace findstrings {

// Model-level strings, shown by both platforms.
inline const std::wstring kNoMatches = L"No matches";
inline const std::wstring kReadOnlyHint = L"Replacing is not available in this view.";
inline const std::wstring kCounterSeparator = L" of ";

}  // namespace findstrings
