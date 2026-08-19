#include "ArgParser.hpp"
#include "Help.hpp"
#include "Options.hpp"
#include "Pipeline.hpp"
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

    return Pipeline::run(options);
}
