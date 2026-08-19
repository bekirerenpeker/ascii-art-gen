#pragma once

#include <string>

namespace Help {

// Prints assets/help/<topic>.txt. An empty topic prints the general page.
// Returns false if there is no page for that topic.
bool print(const std::string& topic);

};   // namespace Help
