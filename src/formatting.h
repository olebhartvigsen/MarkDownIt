#pragma once
#include "textbuffer.h"
#include "caret.h"
#include "dom.h"
#include <string>
#include <vector>

class UndoStack;

// Toggle an inline marker (e.g. ** for bold, * for italic, ` for code, ~~ for strikethrough).
// If the selection is already wrapped in the marker, remove it.
// If not, wrap it. Handles empty selection by inserting marker pair and
// placing the caret between them.
// If undo is non-null, records an undo entry for the operation.
void ToggleInlineMarker(TextBuffer* buf, Selection* sel, const std::string& marker,
                        UndoStack* undo = nullptr);

// Insert a link around the selection: [text](url)
// If the selection is empty, inserts [](url) with caret between [ and ].
void InsertLink(TextBuffer* buf, Selection* sel, const std::string& url,
                UndoStack* undo = nullptr);

// Toggle a fenced code block (``` ``` ```) around the selected paragraphs.
// If the selection spans one or more entire lines, wraps them in ```
// fences. If already fenced, removes the fences.
void ToggleCodeBlock(TextBuffer* buf, Selection* sel,
                      UndoStack* undo = nullptr);

// The inline markers that overlap the selection.
//
// One entry is one run of markers to take away. The content of the
// run stays, so `**text**` becomes `text`.
struct InlineMarkerRun {
    uint32_t markerStart = 0;  // the start of the left markers
    uint32_t markerEnd = 0;    // the end of the right markers
    uint32_t contentStart = 0; // the start of the content
    uint32_t contentEnd = 0;   // the end of the content
};

// The inline markers that overlap [start, end) of the document.
//
// The runs come back sorted from the right, so the caller can splice
// them one by one without the earlier ones shifting the later ones.
// Runs that overlap are merged, because a bold and an italic run can
// share their markers.
std::vector<InlineMarkerRun> CollectInlineMarkerRuns(
    const Document& doc, const std::string& text, uint32_t start, uint32_t end);

// Takes the inline formatting markers away from [start, end) and
// leaves the content. The block formatting is not touched.
//
// The selection covers the same text afterwards, without the markers.
// If undo is not null, the change is recorded as one undo step.
void ClearInlineFormatting(const Document& doc, TextBuffer* buf, Selection* sel,
                            UndoStack* undo = nullptr);

// Check if the text at the given range is already wrapped in the marker.
bool IsWrappedIn(const std::string& text, uint32_t start, uint32_t end,
                 const std::string& marker);


// --- Block formatting ---

// Set heading level on the current line. level 0 = remove heading.
void SetHeadingLevel(TextBuffer* buf, Selection* sel, int level,
                     UndoStack* undo = nullptr);

// Toggle unordered list (- prefix) on the current line.
void ToggleUnorderedList(TextBuffer* buf, Selection* sel,
                         UndoStack* undo = nullptr);

// Toggle ordered list (1. prefix) on the current line.
void ToggleOrderedList(TextBuffer* buf, Selection* sel,
                       UndoStack* undo = nullptr);

// Toggle blockquote (> prefix) on the current line.
void ToggleBlockquote(TextBuffer* buf, Selection* sel,
                      UndoStack* undo = nullptr);

// Indent the current line one level (for use inside lists).
void IndentLine(TextBuffer* buf, Selection* sel,
                UndoStack* undo = nullptr);

// Outdent the current line one level.
void OutdentLine(TextBuffer* buf, Selection* sel,
                 UndoStack* undo = nullptr);

// Renumber an ordered list run starting at the given line.
