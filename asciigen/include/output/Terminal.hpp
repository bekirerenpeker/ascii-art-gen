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

// Plain ANSI escapes -- safe to call unconditionally, including when stdout
// isn't a real terminal (they just land in whatever's on the other end,
// harmlessly, same as any other escape this project emits).
void hideCursor();
void showCursor();

// Hides on construction, shows again on destruction -- covers every normal
// return path (including an exception) through whatever's drawing between
// the two. Does NOT cover the process being killed outright (Ctrl+C, a
// crash): nothing runs a destructor then, which is what main.cpp's own
// SIGINT handler is for -- see its own note.
class CursorGuard
{
  public:
    CursorGuard() { hideCursor(); }
    ~CursorGuard() { showCursor(); }

    CursorGuard(const CursorGuard&) = delete;
    CursorGuard& operator=(const CursorGuard&) = delete;
};

};   // namespace Terminal
