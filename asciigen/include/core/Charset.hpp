#pragma once

#include <cstdint>
#include <string>

class Charset
{
  public:
    explicit Charset() {}
    explicit Charset(std::u32string glyphs) : m_glyphs(std::move(glyphs)) {}
    explicit Charset(const std::string& glyphs) : m_glyphs(glyphs.begin(), glyphs.end()) {}

    char32_t codepointAt(uint16_t index) const { return m_glyphs[index]; }
    uint16_t size() const { return static_cast<uint16_t>(m_glyphs.size()); }
    int indexOf(char32_t codepoint) const;

    static const Charset& ascii();
    static const Charset& blocks();

  private:
    std::u32string m_glyphs;
};
