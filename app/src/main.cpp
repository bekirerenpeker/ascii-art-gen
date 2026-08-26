#include "ArgParser.hpp"
#include "Help.hpp"
#include "Options.hpp"
#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "output/Terminal.hpp"
#include "Test.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {

// Ctrl+C bypasses every destructor on the way out, so Terminal::CursorGuard
// (ProgressDisplay's draw loop, text-video playback) never gets to run its
// own cleanup -- this is the only thing that still shows the cursor again
// when that happens. std::exit rather than returning: a signal handler
// doesn't have anywhere to return TO that resumes the program sanely, and
// this project has nothing else registered on the way out worth preserving.
extern "C" void onInterrupt(int)
{
    Terminal::showCursor();
    std::exit(130);   // conventional 128+SIGINT
}

// "3m 5s" for the --profile crowd's benefit too, but doesn't need --profile
// (or even a build with ASCIIGEN_PROFILING compiled in) to show up -- this is
// wall-clock time around the one Pipeline::run() call, nothing to do with
// the scope-based tracing that feeds a trace file.
std::string formatDuration(std::chrono::steady_clock::duration d)
{
    const double totalSeconds = std::chrono::duration<double>(d).count();
    const long long wholeSeconds = (long long)totalSeconds;
    const long long hours = wholeSeconds / 3600;
    const long long minutes = (wholeSeconds % 3600) / 60;
    const long long seconds = wholeSeconds % 60;

    std::ostringstream ss;
    if (hours > 0) {
        ss << hours << "h " << minutes << "m " << seconds << "s";
    } else if (minutes > 0) {
        ss << minutes << "m " << seconds << "s";
    } else {
        // Sub-minute runs are the common case (most stills, short clips) --
        // worth a decimal here so "took 0s" doesn't read as "took no time".
        ss << std::fixed;
        ss.precision(totalSeconds < 10.0 ? 2 : 1);
        ss << totalSeconds << "s";
    }
    return ss.str();
}

}   // namespace

int main(int argc, char* argv[])
{
    std::signal(SIGINT, onInterrupt);

    // First, and before anything is parsed, so an experiment never has to be
    // expressed as flags. Empty by default.
    if (Test::run()) return 0;

    App::Options options;
    const ArgParser::Result result = ArgParser::parse(argc, argv, options);

    if (options.helpRequested) {
        if (Help::print(options.helpTopic)) return 0;

        std::cerr << "asciigen: no help page for \"" << options.helpTopic << "\"\n";
        Help::print("");
        return 2;
    }

    if (options.showVersion) {
        std::cout << "asciigen 0.1\n";
        return 0;
    }

    if (result == ArgParser::Result::ExitFailure) return 2;
    if (result == ArgParser::Result::ExitSuccess) return 0;

    // Text in, text out: an .ans or .txt input is echoed rather than converted.
    std::string ext = std::filesystem::path(options.input.path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    options.input.passthrough = ext == ".ans" || ext == ".txt";

    // Started before the pipeline and written after, so the outermost span
    // covers everything the run actually did.
    if (!options.profilePath.empty()) {
        Profiler::begin();
        Profiler::nameThread("main");

        // Stamped into the trace so it identifies itself on screen. Without this
        // two runs look identical in the viewer, and there is no way to tell a
        // Debug trace from a Release one -- which is exactly the confusion that
        // makes timings look wrong.
        std::string command;
        for (int i = 1; i < argc; i++) command += std::string(i > 1 ? " " : "") + argv[i];

        Profiler::describe("command", command);
        Profiler::describe("build", ASCIIGEN_BUILD_TYPE);
    }

    const auto runStart = std::chrono::steady_clock::now();
    const int status = Pipeline::run(options);
    const auto runEnd = std::chrono::steady_clock::now();

    if (!options.profilePath.empty()) {
        if (Profiler::writeTo(options.profilePath))
            std::cerr << "asciigen: trace written to " << options.profilePath << "\n"
                      << "  open it at https://ui.perfetto.dev or https://speedscope.app\n";
        else
            std::cerr << "asciigen: could not write trace to " << options.profilePath << "\n";
    }

    std::cout << "asciigen: took " << formatDuration(runEnd - runStart) << "\n";

    return status;
}
