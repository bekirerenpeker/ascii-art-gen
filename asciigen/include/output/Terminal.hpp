#pragma once

namespace Terminal {

bool enableAnsi();   // Windows: opt into VT processing.
bool enableUtf8();   // Windows: SetConsoleOutputCP(CP_UTF8).
bool isTty();        // is stdout a terminal, or redirected?
bool getSize(int& cols, int& rows);

// Best-effort only -- there is no reliable way to ask a terminal whether its
// current font actually has a glyph for a given codepoint, only heuristics
// about which terminal programs are known to ship one that does. False
// doesn't guarantee it'll look wrong, true doesn't guarantee it won't --
// treat this as "safe to try," not a promise. Classic conhost.exe on
// whatever raster or legacy TrueType font a user still has configured is the
// common case that reliably lacks full block-drawing coverage, which is why
// that's the fallback rather than the default.
bool supportsUnicodeBlocks();

};   // namespace Terminal
