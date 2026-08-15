#include "core/Charset.hpp"

const Charset& Charset::ascii()
{
    // Printable ASCII, space through tilde (32-126 both included)
    static const Charset instance = [] {
        std::u32string glyphs;
        glyphs.reserve(0x7F - 0x20);
        for (char32_t cp = 0x20; cp < 0x7F; ++cp) glyphs += cp;
        return Charset(std::move(glyphs));
    }();

    return instance;
}
