#pragma once

// File association: register .md and .markdown files so Explorer shows
// the MarkDownIt document icon and double-click opens the file in MarkDownIt.
//
// Registration is per-user (HKCU), no admin rights needed.
// The settings toggle calls Register/Unregister to change the state.

// Register .md and .markdown file associations for the current user.
// exePath is the full path to MarkDownIt.exe.
void RegisterMdAssociation(const wchar_t* exePath);

// Remove .md and .markdown file associations for the current user.
void UnregisterMdAssociation();

// Check whether .md files are currently registered to MarkDownIt.
bool IsMdRegistered();
