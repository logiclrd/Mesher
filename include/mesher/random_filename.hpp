#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <utility>

namespace mesher {

// Both dictionaries are compiled into the executable by CMake. Runtime use
// never opens word-list files in CWD, the install tree, or elsewhere.
std::pair<std::size_t,std::size_t> embeddedFilenameWordCounts();

// Deterministic overload for unit tests, seeded at the call site.
std::string randomFilenameStem(std::mt19937_64& random);

// Non-deterministic user-facing overload. This constructs one adjective,
// one space and one noun, without a suffix (the selected Save filter owns it).
std::string randomFilenameStem();

} // namespace mesher
