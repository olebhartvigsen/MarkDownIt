#pragma once
#include "textbuffer.h"
#include "caret.h"
#include <string>

// Check if the just-typed character at the caret position triggers
// an autoformat transformation. If so, modify the buffer and adjust
// the caret. Returns true if a transformation was applied.
//
// trigger: the character that was just typed (e.g. ' ' for headings/lists,
//          '`' for code blocks, '*' for bold, etc.)
bool CheckAutoformat(TextBuffer* buf, Selection* sel, char trigger);

// Specific autoformat checks (exposed for testing):

// Check if typing a space after "#", "##", etc. at line start
// triggers a heading format. Returns true and reformats if so.
bool AutoformatHeading(TextBuffer* buf, Selection* sel);

// Check if typing a space after "-", "*" at line start triggers bullet list.
bool AutoformatBulletList(TextBuffer* buf, Selection* sel);

// Check if typing a space after "N." at line start triggers ordered list.
bool AutoformatOrderedList(TextBuffer* buf, Selection* sel);

// Check if typing a space after ">" at line start triggers blockquote.
bool AutoformatBlockquote(TextBuffer* buf, Selection* sel);

// Check if typing a closing ** wraps the preceding text in bold.
bool AutoformatBold(TextBuffer* buf, Selection* sel);

// Check if typing a closing ` wraps the preceding text in inline code.
bool AutoformatInlineCode(TextBuffer* buf, Selection* sel);
