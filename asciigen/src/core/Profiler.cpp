#include "core/Profiler.hpp"
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace Profiler {

namespace {

struct Event
{
    const char* name;
    const char* category;
    long long startUs;
    long long durUs;
    unsigned long long thread;
};

// Relaxed because the flag is set once before any work starts and read many
// times after; there is nothing to order against it.
std::atomic<bool> g_enabled {false};

std::mutex g_mutex;
std::vector<Event> g_events;
std::vector<std::pair<std::string, std::string>> g_metadata;
std::chrono::steady_clock::time_point g_origin;

unsigned long long currentThread()
{
    return (unsigned long long)std::hash<std::thread::id> {}(std::this_thread::get_id());
}

// The viewers take a raw JSON string, so anything that could break out of one
// has to go. Span names here are compile-time literals we control, but that is
// a property of today's call sites rather than a guarantee.
void writeEscaped(std::ofstream& out, const char* text)
{
    for (const char* c = text; *c; c++) {
        if (*c == '"' || *c == '\\') out << '\\' << *c;
        else if (*c == '\n') out << "\\n";
        else out << *c;
    }
}

}   // namespace

void begin()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    g_events.clear();
    g_events.reserve(256);
    g_metadata.clear();
    g_origin = std::chrono::steady_clock::now();

    g_enabled.store(true, std::memory_order_relaxed);
}

void describe(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_metadata.emplace_back(key, value);
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }

void record(const char* name, const char* category, long long startUs, long long durUs)
{
    if (!enabled()) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back({name, category, startUs, durUs, currentThread()});
}

bool writeTo(const std::filesystem::path& filepath)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_enabled.store(false, std::memory_order_relaxed);

    if (g_events.empty()) return false;

    std::ofstream out(filepath);
    if (!out) return false;

    // "X" is a complete event: one object carrying both start and duration, so
    // nothing has to be paired up afterwards. Nesting is inferred from the
    // timestamps, which is why no depth is recorded here.
    out << "{\n\"displayTimeUnit\":\"ms\",\n\"traceEvents\":[\n";

    for (size_t i = 0; i < g_events.size(); i++) {
        const Event& e = g_events[i];

        out << "{\"name\":\"";
        writeEscaped(out, e.name);
        out << "\",\"cat\":\"";
        writeEscaped(out, e.category);
        out << "\",\"ph\":\"X\",\"ts\":" << e.startUs << ",\"dur\":" << e.durUs
            << ",\"pid\":1,\"tid\":" << e.thread << "}";

        if (i + 1 < g_events.size()) out << ',';
        out << '\n';
    }

    out << "],\n\"otherData\":{";

    for (size_t i = 0; i < g_metadata.size(); i++) {
        out << "\"";
        writeEscaped(out, g_metadata[i].first.c_str());
        out << "\":\"";
        writeEscaped(out, g_metadata[i].second.c_str());
        out << "\"";

        if (i + 1 < g_metadata.size()) out << ',';
    }

    out << "}}\n";

    g_events.clear();
    g_metadata.clear();
    return true;
}

Scope::Scope(const char* name, const char* category)
    : m_name(name), m_category(category), m_start(std::chrono::steady_clock::now())
{
}

Scope::~Scope()
{
    if (!enabled()) return;

    const auto now = std::chrono::steady_clock::now();
    const auto startUs =
        std::chrono::duration_cast<std::chrono::microseconds>(m_start - g_origin).count();
    const auto durUs = std::chrono::duration_cast<std::chrono::microseconds>(now - m_start).count();

    record(m_name, m_category, startUs, durUs);
}

}   // namespace Profiler
