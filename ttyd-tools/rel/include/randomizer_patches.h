#pragma once

#include <gc/OSLink.h>
#include <ttyd/evtmgr.h>

#include <cstdint>

namespace mod::pit_randomizer {

// Code that runs after linking a new module.
void OnModuleLoaded(gc::OSLink::OSModuleInfo* module);

// Returns a string to display in place of the usual one for a given key,
// or nullptr if the default should be printed.
const char* GetReplacementMessage(const char* msg_key);

// Apply miscellaneous small patches that do not require function hooks.
void ApplyMiscPatches();

}