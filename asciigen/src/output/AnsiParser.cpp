#include "output/AnsiParser.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace AnsiParser {

namespace {

// Decodes one UTF-8 codepoint starting at text[i], advances i past it.
// Malformed bytes decode as themselves rather than aborting -- this reads
// this project's OWN previously-written output, not untrusted input, so
// recovering something is more useful than refusing to.
char32_t decodeUtf8(const std::string& text, size_t& i)
{
    const unsigned char c0 = (unsigned char)text[i];
    if (c0 < 0x80) {
        i += 1;
        return c0;
    }

    int extra;
    char32_t cp;
    if ((c0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = c0 & 0x07;
    } else {
        i += 1;
        return c0;
    }

    if (i + extra >= text.size()) {
        i += 1;
        return c0;
    }

    for (int k = 1; k <= extra; k++) {
        const unsigned char c = (unsigned char)text[i + k];
        if ((c & 0xC0) != 0x80) {
            i += 1;
            return c0;
        }
        cp = (cp << 6) | (c & 0x3F);
    }

    i += 1 + extra;
    return cp;
}

// Standard terminal palette approximation -- Ansi16 is already lossy on the
// way OUT (AnsiRenderer::render picks the nearest of these 16 from a real
// RGB), so reversing it only ever recovers an approximation of an
// approximation, never the original colour.
RGB ansi16ToRgb(int code)
{
    static const RGB table[16] = {
        {0, 0, 0},     {205, 0, 0},   {0, 205, 0},   {205, 205, 0},
        {0, 0, 238},   {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
        {127, 127, 127}, {255, 0, 0}, {0, 255, 0},   {255, 255, 0},
        {92, 92, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
    };

    int idx = 7;
    if (code >= 30 && code <= 37) idx = code - 30;
    else if (code >= 90 && code <= 97) idx = code - 90 + 8;
    else if (code >= 40 && code <= 47) idx = code - 40;
    else if (code >= 100 && code <= 107) idx = code - 100 + 8;

    return table[idx];
}

// One row (no newline in it) -> however many cells it actually has. `charset`
// is always updated when given; `cells` only when the caller wants the full
// per-cell result rather than just the glyphs seen (collectGlyphs's whole
// point is skipping that second part).
void parseRow(const std::string& row, Charset* charset, std::vector<Cell>* cells)
{
    RGB fg {255, 255, 255}, bg {0, 0, 0};

    size_t i = 0;
    while (i < row.size()) {
        if ((unsigned char)row[i] == 0x1B && i + 1 < row.size() && row[i + 1] == '[') {
            size_t j = i + 2;
            while (j < row.size() && !std::isalpha((unsigned char)row[j])) j++;
            if (j >= row.size()) break;   // truncated escape -- stop here rather than misread it

            if (row[j] == 'm') {
                std::vector<int> params;
                size_t k = i + 2;
                while (k < j) {
                    size_t comma = row.find(';', k);
                    if (comma == std::string::npos || comma > j) comma = j;
                    params.push_back(comma > k ? std::atoi(row.substr(k, comma - k).c_str()) : 0);
                    k = comma + 1;
                }

                for (size_t p = 0; p < params.size(); p++) {
                    const int v = params[p];
                    if (v == 0) {
                        fg = {255, 255, 255};
                        bg = {0, 0, 0};
                    } else if (v == 38 && p + 4 < params.size() && params[p + 1] == 2) {
                        fg = {(uint8_t)params[p + 2], (uint8_t)params[p + 3], (uint8_t)params[p + 4]};
                        p += 4;
                    } else if (v == 48 && p + 4 < params.size() && params[p + 1] == 2) {
                        bg = {(uint8_t)params[p + 2], (uint8_t)params[p + 3], (uint8_t)params[p + 4]};
                        p += 4;
                    } else if ((v >= 30 && v <= 37) || (v >= 90 && v <= 97)) {
                        fg = ansi16ToRgb(v);
                    } else if ((v >= 40 && v <= 47) || (v >= 100 && v <= 107)) {
                        bg = ansi16ToRgb(v);
                    }
                }
            }
            // 'H' (cursor home, from screenControls) and anything else: no
            // colour/glyph effect -- just consumed and skipped.

            i = j + 1;
            continue;
        }

        const char32_t cp = decodeUtf8(row, i);
        if (charset) {
            const int idx = charset->append(cp);
            if (cells) cells->push_back({(uint16_t)idx, fg, bg});
        }
    }
}

// Splits `text` into rows on '\n' and calls `perRow` with each one. A
// trailing empty row from a final "\n" with nothing after it is dropped --
// that's the line terminator, not a real row of the grid.
template <typename PerRow>
void forEachRow(const std::string& text, PerRow perRow)
{
    size_t start = 0;
    bool any = false;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        const std::string row =
            text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);

        if (nl != std::string::npos || !row.empty()) {
            perRow(row);
            any = true;
        }
        (void)any;

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

}   // namespace

void collectGlyphs(const std::string& text, Charset& charset)
{
    forEachRow(text, [&](const std::string& row) { parseRow(row, &charset, nullptr); });
}

void parse(const std::string& text, Charset& charset, CellBuffer& buffer)
{
    std::vector<std::vector<Cell>> rows;

    forEachRow(text, [&](const std::string& row) {
        std::vector<Cell> cells;
        parseRow(row, &charset, &cells);
        rows.push_back(std::move(cells));
    });

    const int height = (int)rows.size();
    int width = 0;
    for (const auto& r : rows) width = std::max(width, (int)r.size());

    buffer.setSize(width, height);
    for (int y = 0; y < height; y++) {
        // Buffers are bottom-up (see AnsiRenderer::render's own note on why);
        // rows here were read top-down as printed, so printed row 0 (the top
        // of the grid) goes to the LAST buffer row.
        const int by = height - 1 - y;
        const std::vector<Cell>& row = rows[y];
        for (int x = 0; x < width; x++)
            buffer.getAt(x, by) = x < (int)row.size() ? row[x] : Cell {0, RGB {0, 0, 0}, RGB {0, 0, 0}};
    }
}

}   // namespace AnsiParser
