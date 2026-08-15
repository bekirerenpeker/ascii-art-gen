// std::getenv is standard and safe for the read-only lookup below; this only
// silences MSVC's non-standard deprecation of it. Must precede any include.
#ifdef _WIN32
    #define _CRT_SECURE_NO_WARNINGS
#endif

#include "output/Terminal.hpp"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>

    #include <io.h>

    // Missing from older Windows SDK headers.
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace Terminal {

namespace {

// COLUMNS / LINES fallback, for the cases where the platform query fails --
// pipes, CI runners, and some editor-embedded terminals.
bool sizeFromEnvironment(int& cols, int& rows)
{
    const char* colsText = std::getenv("COLUMNS");
    const char* rowsText = std::getenv("LINES");
    if (!colsText || !rowsText) return false;

    const int parsedCols = std::atoi(colsText);
    const int parsedRows = std::atoi(rowsText);
    if (parsedCols <= 0 || parsedRows <= 0) return false;

    cols = parsedCols;
    rows = parsedRows;
    return true;
}

}   // namespace

// Returns whether ANSI escapes can be expected to work, which is not quite the
// same as "a console was reconfigured": when stdout is redirected there is no
// console to touch and the escapes land in the file verbatim, which is exactly
// what the .ans output path wants. Use isTty() to decide whether to emit color,
// not this.
bool enableAnsi()
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return true;   // redirected, nothing to configure
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;

    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    // Every POSIX terminal worth targeting interprets ANSI without being asked.
    return true;
#endif
}

bool enableUtf8()
{
#ifdef _WIN32
    // Makes the console treat the bytes we write as UTF-8. Only matters once
    // the charset reaches past ASCII -- braille, blocks, box drawing.
    return SetConsoleOutputCP(CP_UTF8) != 0;
#else
    return true;
#endif
}

bool isTty()
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

bool getSize(int& cols, int& rows)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    {
        // srWindow is the visible viewport. dwSize is the scrollback buffer,
        // which is usually far taller and is not what we want to fill.
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        const int height = info.srWindow.Bottom - info.srWindow.Top + 1;

        if (width > 0 && height > 0)
        {
            cols = width;
            rows = height;
            return true;
        }
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
    {
        cols = ws.ws_col;
        rows = ws.ws_row;
        return true;
    }
#endif

    return sizeFromEnvironment(cols, rows);
}

};   // namespace Terminal
