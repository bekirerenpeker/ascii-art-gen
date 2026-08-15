#pragma once

namespace Terminal {

bool enableAnsi();   // Windows: opt into VT processing.
bool enableUtf8();   // Windows: SetConsoleOutputCP(CP_UTF8).
bool isTty();        // is stdout a terminal, or redirected?
bool getSize(int& cols, int& rows);

};   // namespace Terminal
