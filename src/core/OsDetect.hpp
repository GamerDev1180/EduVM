#pragma once

#include <string>

#include "core/VmConfig.hpp"

namespace eduvm {

struct OsGuess {
    bool detected = false;
    GuestOs os = GuestOs::Other;
    std::string name;  // e.g. "Ubuntu 24.04.1", "Windows 11"
    int memoryMb = 2048;
    int diskGb = 20;
};

// Guesses the guest OS from an installer file name, like VMware's wizard
// does, so the defaults are right without the user knowing hardware specs.
OsGuess guessOsFromIso(const std::string& isoPath);

// Defaults when the user picks the OS by hand.
OsGuess defaultsFor(GuestOs os);

}  // namespace eduvm
