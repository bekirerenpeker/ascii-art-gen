#include "ArgParser.hpp"
#include "Help.hpp"
#include "Options.hpp"
#include "Pipeline.hpp"
#include "core/Profiler.hpp"
#include "Test.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[])
{
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

        // Stamped into the trace so it identifies itself on screen. Without this
        // two runs look identical in the viewer, and there is no way to tell a
        // Debug trace from a Release one -- which is exactly the confusion that
        // makes timings look wrong.
        std::string command;
        for (int i = 1; i < argc; i++) command += std::string(i > 1 ? " " : "") + argv[i];

        Profiler::describe("command", command);
        Profiler::describe("build", ASCIIGEN_BUILD_TYPE);
    }

    const int status = Pipeline::run(options);

    if (!options.profilePath.empty()) {
        if (Profiler::writeTo(options.profilePath))
            std::cerr << "asciigen: trace written to " << options.profilePath << "\n"
                      << "  open it at https://ui.perfetto.dev or https://speedscope.app\n";
        else
            std::cerr << "asciigen: could not write trace to " << options.profilePath << "\n";
    }

    return status;
}
