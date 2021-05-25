#pragma once

#include <cstdint>

namespace mod::infinite_pit::mario_move {

// Apply patches related to Mario's moves and move selection in battle.
void ApplyFixedPatches();

// Initializes Mario's moves' selected power level when entering/exiting battle.
void OnEnterExitBattle(bool is_start);
// Gets the current move level of Toughen Up for Mario or partner.
int8_t GetToughenUpLevel(bool is_mario);

}