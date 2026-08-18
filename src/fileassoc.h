// File association: register .md files so Explorer shows the MarkDownIt
// document icon and double-click opens the file in MarkDownIt.
//
// Registration is per-user (HKCU), no admin rights needed.
// Later, a settings toggle can call UnregisterMdAssociation() to undo.

#pragma once

// Register .md file association for the current user.
// exePath is the full path to MarkDownIt.exe, used in shell\open\command
// and DefaultIcon. The document icon is resource index 1 (IDI_DOCICON).
void RegisterMdAssociation(const wchar_t* exePath);

// Remove .md file association for the current user.
void UnregisterMdAssociation();
