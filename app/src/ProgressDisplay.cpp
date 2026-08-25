#include "ProgressDisplay.hpp"
#include "output/Terminal.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ProgressDisplay {

namespace {

// Stylistic only -- the bar reads fine in plain text too, this just makes it
// nicer to look at. Truecolor is safe here: Terminal::isTty() already gated
// everything that reaches this on being a real terminal, and every terminal that
// still honours cursor-control escapes also honours 24-bit colour ones.
constexpr const char* kReset = "\x1b[0m";
constexpr const char* kLabelColor = "\x1b[1m";           // bold, default colour
constexpr const char* kStageColor = "\x1b[38;2;140;170;238m";   // soft blue
constexpr const char* kBarFillColor = "\x1b[38;2;138;212;135m"; // soft green
constexpr const char* kBarEmptyColor = "\x1b[38;2;90;90;90m";   // dim grey
constexpr const char* kPercentColor = "\x1b[38;2;180;180;180m";

std::string renderOneLine(const Line& line)
{
    const float fraction = std::clamp(line.progress->fraction.load(std::memory_order_relaxed), 0.f, 1.f);
    const char* stage = line.progress->stage.load(std::memory_order_relaxed);

    int cols = 0, rows = 0;
    const int totalWidth = (Terminal::getSize(cols, rows) && cols > 20) ? std::min(cols - 1, 100) : 70;

    // Budget: label, " [", stage, "] ", bar, " ", percent. Bar takes whatever's
    // left, clamped so it never vanishes on a narrow terminal or balloons on a huge one.
    const int fixedWidth = (int)line.label.size() + 3 + (int)std::string(stage).size() + 2 + 1 + 5;
    const int barWidth = std::clamp(totalWidth - fixedWidth, 10, 40);

    const int filled = (int)std::lround(barWidth * fraction);

    std::string out;
    out += kLabelColor;
    out += line.label;
    out += kReset;
    out += " [";
    out += kStageColor;
    out += stage;
    out += kReset;
    out += "] ";

    out += kBarFillColor;
    for (int i = 0; i < filled; i++) out += "\xE2\x96\x88";   // U+2588 FULL BLOCK
    out += kBarEmptyColor;
    for (int i = filled; i < barWidth; i++) out += "\xE2\x96\x91";   // U+2591 LIGHT SHADE
    out += kReset;

    char pct[8];
    std::snprintf(pct, sizeof(pct), " %3d%%", (int)std::lround(fraction * 100.f));
    out += kPercentColor;
    out += pct;
    out += kReset;

    return out;
}

}   // namespace

void Renderer::draw(const std::vector<Line>& lines)
{
    if (!Terminal::isTty()) return;

    // Move back up over the previous draw before writing the new one, or every
    // redraw would scroll the terminal by `lines.size()` instead of overwriting.
    if (m_linesDrawn > 0) std::printf("\x1b[%dA", m_linesDrawn);

    for (const Line& line : lines) {
        // \x1b[2K clears the whole line first -- a shorter new line (stage name
        // changed length) would otherwise leave a trailing fragment of the old one.
        std::printf("\r\x1b[2K%s\n", renderOneLine(line).c_str());
    }

    // A shrinking line count (never happens yet -- video will be able to, once a
    // worker finishes and drops off the list) still has old lines below the new
    // ones to clear, or they'd sit there stale until something else overwrites them.
    for (int i = (int)lines.size(); i < m_linesDrawn; i++) std::printf("\r\x1b[2K\n");
    if (m_linesDrawn > (int)lines.size())
        std::printf("\x1b[%dA", m_linesDrawn - (int)lines.size());

    std::fflush(stdout);
    m_linesDrawn = (int)lines.size();
}

void Renderer::finish()
{
    if (!Terminal::isTty() || m_linesDrawn == 0) return;

    std::printf("\x1b[%dA", m_linesDrawn);
    for (int i = 0; i < m_linesDrawn; i++) std::printf("\r\x1b[2K\n");
    std::printf("\x1b[%dA", m_linesDrawn);
    std::fflush(stdout);

    m_linesDrawn = 0;
}

void runUntilDone(const std::function<bool()>& isDone, const std::vector<Line>& lines, int intervalMs)
{
    Renderer renderer;
    while (!isDone()) {
        renderer.draw(lines);
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
    renderer.draw(lines);
    renderer.finish();
}

}   // namespace ProgressDisplay
