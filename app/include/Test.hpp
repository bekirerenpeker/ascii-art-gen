#pragma once

namespace Test {

// Scratch space for driving the engine directly, without going through argument
// parsing. main() calls this first and returns immediately if it returns true,
// so experiments never have to fight the CLI. Returns false when empty.
bool run();

};   // namespace Test
