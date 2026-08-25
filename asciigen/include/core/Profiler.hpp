#pragma once

#include <chrono>
#include <filesystem>
#include <string>

// Timing spans written out as Chrome Trace Event JSON -- the format
// ui.perfetto.dev, speedscope.app and chrome://tracing all read directly. No
// dependency: it is an array of objects with a name, a start and a duration,
// and nesting falls out of the timestamps rather than needing to be declared.
//
// Two levels of "off". ASCIIGEN_PROFILING compiles every scope out of existence,
// so a build without it carries nothing at all. With it compiled in but no
// --profile given, a scope costs one relaxed bool read; spans sit at stage
// granularity and never inside per-pixel loops, so that cost is unmeasurable.
namespace Profiler {

void begin();
bool enabled();

// Recorded into the trace's otherData block and shown by the viewer. Exists so a
// trace identifies itself: which command produced it, which build, and when --
// without that, two runs are indistinguishable on screen and a stale file looks
// exactly like a fresh one.
void describe(const std::string& key, const std::string& value);

// Written on a single line each, in the order they finished.
void record(const char* name, const char* category, long long startUs, long long durUs);

// Writes the trace and turns profiling back off. False if the file could not be
// opened, or if nothing was ever recorded.
bool writeTo(const std::filesystem::path& filepath);

class Scope
{
  public:
    Scope(const char* name, const char* category);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

  private:
    const char* m_name;
    const char* m_category;
    std::chrono::steady_clock::time_point m_start;
};

};   // namespace Profiler

#ifdef ASCIIGEN_PROFILING

#define ASCIIGEN_PROFILE_CONCAT_INNER(a, b) a##b
#define ASCIIGEN_PROFILE_CONCAT(a, b) ASCIIGEN_PROFILE_CONCAT_INNER(a, b)

#define ASCIIGEN_PROFILE(name, category)                                                      \
    ::Profiler::Scope ASCIIGEN_PROFILE_CONCAT(profilerScope, __LINE__)(name, category)

#else

#define ASCIIGEN_PROFILE(name, category) ((void)0)

#endif
