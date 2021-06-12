#include "patches_battle.h"

#include "custom_enemy.h"
#include "mod.h"
#include "mod_state.h"
#include "patch.h"
#include "patches_mario_move.h"

#include <ttyd/battle.h>
#include <ttyd/battle_ac.h>
#include <ttyd/battle_actrecord.h>
#include <ttyd/battle_database_common.h>
#include <ttyd/battle_damage.h>
#include <ttyd/battle_unit.h>
#include <ttyd/item_data.h>

#include <cstdint>
#include <cstring>

namespace mod::infinite_pit {

namespace {

using ::ttyd::battle_database_common::BattleWeapon;
using ::ttyd::battle_unit::BattleWorkUnit;

namespace BattleUnitType = ::ttyd::battle_database_common::BattleUnitType;
namespace ItemType = ::ttyd::item_data::ItemType;

}

// Function hooks.
extern int32_t (*g_BattleActionCommandCheckDefence_trampoline)(
    BattleWorkUnit*, BattleWeapon*);
extern void (*g__getSickStatusParam_trampoline)(
    BattleWorkUnit*, BattleWeapon*, int32_t, int8_t*, int8_t*);
// Patch addresses.
extern const int32_t g_BattleCheckDamage_Patch_PaybackDivisor;
extern const int32_t g_BattleCheckDamage_Patch_HoldFastDivisor;
extern const int32_t g_BattleCheckDamage_Patch_ReturnPostageDivisor;

namespace battle {
    
void ApplyFixedPatches() {
    g_BattleActionCommandCheckDefence_trampoline = patch::hookFunction(
        ttyd::battle_ac::BattleActionCommandCheckDefence,
        [](BattleWorkUnit* unit, BattleWeapon* weapon) {
            // Run normal logic if option turned off.
            if (!(g_Mod->state_.options_ 
                    & StateManager::SUPERGUARDS_COST_FP)) {
                return g_BattleActionCommandCheckDefence_trampoline(unit, weapon);
            }
            
            int8_t superguard_frames[7];
            bool restore_superguard_frames = false;
            // Temporarily disable Superguarding if FP is too low.
            int32_t fp = ttyd::battle_unit::BtlUnit_GetFp(unit);
            if (fp < 1) {
                restore_superguard_frames = true;
                memcpy(superguard_frames, ttyd::battle_ac::superguard_frames, 7);
                for (int32_t i = 0; i < 7; ++i) {
                    ttyd::battle_ac::superguard_frames[i] = 0;
                }
            }
            const int32_t defense_result =
                g_BattleActionCommandCheckDefence_trampoline(unit, weapon);
            // Successful Superguard, subtract FP.
            if (defense_result == 5) {
                ttyd::battle_unit::BtlUnit_SetFp(unit, fp - 1);
                // Count towards FP-spending conditions. (Use Mario's FP 
                // spent always, since no conditions care who spends it).
                ttyd::battle_actrecord::BtlActRec_AddCount(
                    &ttyd::battle::g_BattleWork->act_record_work.mario_fp_spent);
            }
            if (restore_superguard_frames) {
                memcpy(ttyd::battle_ac::superguard_frames, superguard_frames, 7);
            }
            return defense_result;
        });
            
    g__getSickStatusParam_trampoline = patch::hookFunction(
        ttyd::battle_damage::_getSickStatusParam, [](
            BattleWorkUnit* unit, BattleWeapon* weapon, int32_t status_type,
            int8_t* turn_count, int8_t* strength) {
                // Run vanilla logic.
                g__getSickStatusParam_trampoline(
                    unit, weapon, status_type, turn_count, strength);
                // If badge type and status type (DEF-Up) are correct,
                // change the effect strength based on the badges equipped.
                if (status_type == 14 && (
                    weapon->item_id == ItemType::SUPER_CHARGE ||
                    weapon->item_id == ItemType::SUPER_CHARGE_P)) {
                    bool is_mario = unit->current_kind == BattleUnitType::MARIO;
                    int8_t badges = mario_move::GetToughenUpLevel(is_mario);
                    *strength = badges + 1;
                }
                // If unit is an enemy and status is Charge (and not an item),
                // change its power in the same way as ATK / FP damage.
                if (status_type == 16 && !weapon->item_id &&
                    unit->current_kind <= BattleUnitType::BONETAIL) {
                    int32_t altered_charge;
                    GetEnemyStats(
                        unit->current_kind, nullptr, &altered_charge,
                        nullptr, nullptr, nullptr, *strength);
                    if (altered_charge > 99) altered_charge = 99;
                    *strength = altered_charge;
                }
            });
        
    // Increase all forms of Payback-esque status returned damage to 1x.
    mod::patch::writePatch(
        reinterpret_cast<void*>(g_BattleCheckDamage_Patch_PaybackDivisor),
        0x38000032U /* li r0, 50 */);
    mod::patch::writePatch(
        reinterpret_cast<void*>(g_BattleCheckDamage_Patch_HoldFastDivisor),
        0x38000032U /* li r0, 50 */);
    mod::patch::writePatch(
        reinterpret_cast<void*>(g_BattleCheckDamage_Patch_ReturnPostageDivisor),
        0x38000032U /* li r0, 50 */);
        
    // Change frame windows for guarding / Superguarding at different levels
    // of Simplifiers / Unsimplifiers to be more symmetric.
    const int8_t kGuardFrames[] =     { 12, 10, 9, 8, 7, 6, 5, 0 };
    const int8_t kSuperguardFrames[]  = { 5, 4, 4, 3, 2, 2, 1, 0 };
    mod::patch::writePatch(
        ttyd::battle_ac::guard_frames, kGuardFrames, sizeof(kGuardFrames));
    mod::patch::writePatch(
        ttyd::battle_ac::superguard_frames, kSuperguardFrames, 
        sizeof(kSuperguardFrames));
}

}  // namespace battle
}  // namespace mod::infinite_pit