#include "patches_ui.h"

#include "common_functions.h"
#include "common_types.h"
#include "mod.h"
#include "mod_state.h"
#include "patch.h"
#include "patches_item.h"

#include <gc/mtx.h>
#include <gc/types.h>
#include <ttyd/gx/GXAttr.h>
#include <ttyd/gx/GXTexture.h>
#include <ttyd/gx/GXTransform.h>
#include <ttyd/battle_seq_end.h>
#include <ttyd/eff_updown.h>
#include <ttyd/icondrv.h>
#include <ttyd/item_data.h>
#include <ttyd/mario_party.h>
#include <ttyd/mario_pouch.h>
#include <ttyd/sound.h>
#include <ttyd/statuswindow.h>
#include <ttyd/win_main.h>
#include <ttyd/win_party.h>

#include <cstdint>
#include <cstring>

// Assembly patch functions.
extern "C" {
    // eff_updown_disp_patches.s
    void StartDispUpdownNumberIcons();
    void BranchBackDispUpdownNumberIcons();
    // status_window_patches.s
    void StartPreventDpadShortcutsOutsidePit();
    void ConditionalBranchPreventDpadShortcutsOutsidePit();
    void BranchBackPreventDpadShortcutsOutsidePit();
    // win_item_patches.s
    void StartFixItemWinPartyDispOrder();
    void BranchBackFixItemWinPartyDispOrder();
    void StartFixItemWinPartySelectOrder();
    void BranchBackFixItemWinPartySelectOrder();
    void StartCheckForUnusableItemInMenu();
    void ConditionalBranchCheckForUnusableItemInMenu();
    void BranchBackCheckForUnusableItemInMenu();
    void StartUseSpecialItems();
    void BranchBackUseSpecialItems();
  
    void dispUpdownNumberIcons(
        int32_t number, void* tex_obj, gc::mtx34* icon_mtx, gc::mtx34* view_mtx,
        uint32_t unk0) {
        mod::infinite_pit::ui::DisplayUpDownNumberIcons(
            number, tex_obj, icon_mtx, view_mtx, unk0);
    }
    bool checkOutsidePit() {
        return strcmp("jon", mod::GetCurrentArea()) != 0;
    }
    void getPartyMemberMenuOrder(ttyd::win_party::WinPartyData** party_data) {
        mod::infinite_pit::ui::GetPartyMemberMenuOrder(party_data);
    }
    bool checkForUnusableItemInMenu() {
        return mod::infinite_pit::ui::CheckForUnusableItemInMenu();
    }
    void useSpecialItems(ttyd::win_party::WinPartyData** party_data) {
        mod::infinite_pit::ui::UseSpecialItemsInMenu(party_data);
    }
}

namespace mod::infinite_pit {

namespace {
    
using ::ttyd::mario_pouch::PouchData;
using ::ttyd::win_party::WinPartyData;

namespace ItemType = ::ttyd::item_data::ItemType;

}

// Function hooks.
extern void (*g_statusWinDisp_trampoline)(void);
extern void (*g_gaugeDisp_trampoline)(double, double, int32_t);
extern const char* (*g_BattleGetRankNameLabel_trampoline)(int32_t);
// Patch addresses.
extern const int32_t g_effUpdownDisp_TwoDigitSupport_BH;
extern const int32_t g_effUpdownDisp_TwoDigitSupport_EH;
extern const int32_t g_statusWinDisp_HideDpadMenuOutsidePit_BH;
extern const int32_t g_statusWinDisp_HideDpadMenuOutsidePit_EH;
extern const int32_t g_statusWinDisp_HideDpadMenuOutsidePit_CH1;
extern const int32_t g_itemUseDisp_FixPartyOrder_BH;
extern const int32_t g_itemUseDisp_FixPartyOrder_EH;
extern const int32_t g_winItemMain_FixPartyOrder_BH;
extern const int32_t g_winItemMain_FixPartyOrder_EH;
extern const int32_t g_winItemMain_CheckInvalidTarget_BH;
extern const int32_t g_winItemMain_CheckInvalidTarget_EH;
extern const int32_t g_winItemMain_CheckInvalidTarget_CH1;
extern const int32_t g_winItemMain_UseSpecialItems_BH;
extern const int32_t g_winItemMain_Patch_AlwaysUseItemsInMenu;
extern const int32_t g__btlcmd_MakeSelectWeaponTable_Patch_GetNameFromItem;

namespace ui {
    
namespace {

// Displays the Star Power in 0.01 units numerically below the status window.
void DisplayStarPowerNumber() {
    // Don't display SP if no Star Powers have been unlocked yet.
    if (ttyd::mario_pouch::pouchGetMaxAP() <= 0) return;
    
    // Don't try to display SP if the status bar is not on-screen.
    float menu_height = *reinterpret_cast<float*>(
        reinterpret_cast<uintptr_t>(ttyd::statuswindow::g_StatusWindowWork)
        + 0x24);
    if (menu_height < 100.f || menu_height > 330.f) return;
    
    gc::mtx34 matrix;
    int32_t unknown_param = -1;
    int32_t current_AP = ttyd::mario_pouch::pouchGetAP();
    gc::mtx::PSMTXTrans(&matrix, 192.f, menu_height - 100.f, 0.f);
    ttyd::icondrv::iconNumberDispGx(
        &matrix, current_AP, /* is_small = */ 1, &unknown_param);
}

// Display the orbs representing the Star Power (replaces the vanilla logic
// since it wasn't built around receiving Star Powers out of order).
void DisplayStarPowerOrbs(double x, double y, int32_t star_power) {
    int32_t max_star_power = ttyd::mario_pouch::pouchGetMaxAP();
    if (max_star_power > 800) max_star_power = 800;
    if (star_power > max_star_power) star_power = max_star_power;
    if (star_power < 0) star_power = 0;
    
    int32_t full_orbs = star_power / 100;
    int32_t remainder = star_power % 100;
    int32_t part_frame = remainder * 15 / 99;
    if (remainder > 0 && star_power > 0 && part_frame == 0) part_frame = 1;
    
    if (part_frame != 0) {
        gc::vec3 pos = { 
            static_cast<float>(x + 32.f * full_orbs),
            static_cast<float>(y),
            0.f };
        ttyd::icondrv::iconDispGx(
            1.f, &pos, 0x10, ttyd::statuswindow::gauge_wakka[part_frame]);
    }
    // Draw grey orbs up to the max amount of SP / 100 (rounded up, max of 8).
    for (int32_t i = 0; i < (max_star_power + 99) / 100; ++i) {
        gc::vec3 pos = {
            static_cast<float>(x + 32.f * i), 
            static_cast<float>(y + 12.f),
            0.f };
        uint16_t icon = i < full_orbs ?
            ttyd::statuswindow::gauge_back[i] : 0x1c7;
        ttyd::icondrv::iconDispGx(1.f, &pos, 0x10, icon);
    }
}

}
    
void ApplyFixedPatches() {
    g_statusWinDisp_trampoline = patch::hookFunction(
        ttyd::statuswindow::statusWinDisp, []() {
            g_statusWinDisp_trampoline();
            DisplayStarPowerNumber();
        });
        
    g_gaugeDisp_trampoline = patch::hookFunction(
        ttyd::statuswindow::gaugeDisp, [](double x, double y, int32_t sp) {
            // Replaces the original logic completely.
            DisplayStarPowerOrbs(x, y, sp);
        });
        
    // Fix rank string shown in the menu.
    g_BattleGetRankNameLabel_trampoline = patch::hookFunction(
        ttyd::battle_seq_end::BattleGetRankNameLabel,
        [](int32_t level) {
            int32_t rank = ttyd::mario_pouch::pouchGetPtr()->rank;
            if (rank < 0 || rank > 3) rank = 0;
            return ttyd::battle_seq_end::_rank_up_data[rank].mario_menu_msg;
        });
        
    // Apply patch to effUpdownDisp code to display the correct number
    // when Charging / +ATK/DEF-ing by more than 9 points.
    mod::patch::writeBranchPair(
        reinterpret_cast<void*>(g_effUpdownDisp_TwoDigitSupport_BH),
        reinterpret_cast<void*>(g_effUpdownDisp_TwoDigitSupport_EH),
        reinterpret_cast<void*>(StartDispUpdownNumberIcons),
        reinterpret_cast<void*>(BranchBackDispUpdownNumberIcons));
        
    // Apply patches to statusWinDisp to prevent D-Pad shortcuts from appearing
    // if the player is outside the Pit (so it doesn't interfere with the
    // Infinite Pit options menu).
    mod::patch::writeBranch(
        reinterpret_cast<void*>(g_statusWinDisp_HideDpadMenuOutsidePit_BH),
        reinterpret_cast<void*>(StartPreventDpadShortcutsOutsidePit));
    mod::patch::writeBranch(
        reinterpret_cast<void*>(BranchBackPreventDpadShortcutsOutsidePit),
        reinterpret_cast<void*>(g_statusWinDisp_HideDpadMenuOutsidePit_EH));
    mod::patch::writeBranch(
        reinterpret_cast<void*>(ConditionalBranchPreventDpadShortcutsOutsidePit),
        reinterpret_cast<void*>(g_statusWinDisp_HideDpadMenuOutsidePit_CH1));
    
    // Apply patches to item menu code to display the correct available partners
    // (both functions use identical code).
    mod::patch::writeBranchPair(
        reinterpret_cast<void*>(g_itemUseDisp_FixPartyOrder_BH),
        reinterpret_cast<void*>(g_itemUseDisp_FixPartyOrder_EH),
        reinterpret_cast<void*>(StartFixItemWinPartyDispOrder),
        reinterpret_cast<void*>(BranchBackFixItemWinPartyDispOrder));
    mod::patch::writeBranchPair(
        reinterpret_cast<void*>(g_winItemMain_FixPartyOrder_BH),
        reinterpret_cast<void*>(g_winItemMain_FixPartyOrder_EH),
        reinterpret_cast<void*>(StartFixItemWinPartySelectOrder),
        reinterpret_cast<void*>(BranchBackFixItemWinPartySelectOrder));
        
    // Apply patch to item menu code to check for invalid item targets
    // (e.g. using Shine Sprites on fully-upgraded partners or Mario).
    mod::patch::writeBranch(
        reinterpret_cast<void*>(g_winItemMain_CheckInvalidTarget_BH),
        reinterpret_cast<void*>(StartCheckForUnusableItemInMenu));
    mod::patch::writeBranch(
        reinterpret_cast<void*>(BranchBackCheckForUnusableItemInMenu),
        reinterpret_cast<void*>(g_winItemMain_CheckInvalidTarget_EH));
    mod::patch::writeBranch(
        reinterpret_cast<void*>(ConditionalBranchCheckForUnusableItemInMenu),
        reinterpret_cast<void*>(g_winItemMain_CheckInvalidTarget_CH1));
        
    // Apply patch to item menu code to properly use Shine Sprite items.
    mod::patch::writeBranchPair(
        reinterpret_cast<void*>(g_winItemMain_UseSpecialItems_BH),
        reinterpret_cast<void*>(StartUseSpecialItems),
        reinterpret_cast<void*>(BranchBackUseSpecialItems));

    // Prevents the menu from closing if you use an item on the active party.
    mod::patch::writePatch(
        reinterpret_cast<void*>(g_winItemMain_Patch_AlwaysUseItemsInMenu),
        0x4800001cU /* b 0x1c */);
        
    // Make item name in battle menu based on item data rather than weapon data.
    mod::patch::writePatch(
        reinterpret_cast<void*>(
            g__btlcmd_MakeSelectWeaponTable_Patch_GetNameFromItem),
        0x807b0004U /* r3, 0x4 (r27) */);
}

void DisplayUpDownNumberIcons(
    int32_t number, void* tex_obj, gc::mtx34* icon_mtx, gc::mtx34* view_mtx, 
    uint32_t unk0) {
    gc::mtx34 pos_mtx;
    gc::mtx34 temp_mtx;
    
    ttyd::gx::GXAttr::GXSetNumTexGens(1);
    ttyd::gx::GXAttr::GXSetTexCoordGen2(0, 1, 4, 60, 0, 125);
    
    int32_t abs_number = number < 0 ? -number : number;
    if (abs_number > 99) abs_number = 99;
    double x_pos = abs_number > 9 ? 10.0 : 5.0;
    
    do {
        // Print digits, right-to-left.
        ttyd::icondrv::iconGetTexObj(
            &tex_obj, ttyd::eff_updown::icon_id[abs_number % 10]);
        ttyd::gx::GXTexture::GXLoadTexObj(&tex_obj, 0);
        if (number < 0) {
            gc::mtx::PSMTXTrans(&pos_mtx, x_pos, 7.0, 1.0);
        } else {
            gc::mtx::PSMTXTrans(&pos_mtx, x_pos, 0.0, 1.0);
        }
        gc::mtx::PSMTXConcat(icon_mtx, &pos_mtx, &temp_mtx);
        gc::mtx::PSMTXConcat(view_mtx, &temp_mtx, &temp_mtx);
        ttyd::gx::GXTransform::GXLoadPosMtxImm(&temp_mtx, 0);
        ttyd::eff_updown::polygon(
            -8.0, 16.0, 16.0, 16.0, 1.0, 1.0, 0, unk0);
        x_pos -= 10.0;
    } while (abs_number /= 10);
        
    // Print plus / minus sign.
    if (number < 0) {
        ttyd::icondrv::iconGetTexObj(&tex_obj, 0x1f6);
        ttyd::gx::GXTexture::GXLoadTexObj(&tex_obj, 0);
        gc::mtx::PSMTXTrans(&pos_mtx, x_pos, 7.0, 1.0);
    } else {
        ttyd::icondrv::iconGetTexObj(&tex_obj, 0x1f5);
        ttyd::gx::GXTexture::GXLoadTexObj(&tex_obj, 0);
        gc::mtx::PSMTXTrans(&pos_mtx, x_pos, 0.0, 1.0);
    }
    gc::mtx::PSMTXConcat(icon_mtx, &pos_mtx, &temp_mtx);
    gc::mtx::PSMTXConcat(view_mtx, &temp_mtx, &temp_mtx);
    ttyd::gx::GXTransform::GXLoadPosMtxImm(&temp_mtx, 0);
    ttyd::eff_updown::polygon(
        -8.0, 16.0, 16.0, 16.0, 1.0, 1.0, 0, unk0);
}

void GetPartyMemberMenuOrder(WinPartyData** out_party_data) {
    WinPartyData* party_data = ttyd::win_party::g_winPartyDt;
    // Get the currently active party member.
    const int32_t party_id = ttyd::mario_party::marioGetParty();
    
    // Put the currently active party member in the first slot.
    WinPartyData** current_order = out_party_data;
    for (int32_t i = 0; i < 7; ++i) {
        if (party_data[i].partner_id == party_id) {
            *current_order = party_data + i;
            ++current_order;
        }
    }
    // Put the remaining party members in the remaining slots, ordered by
    // the order they appear in g_winPartyDt.
    ttyd::mario_pouch::PouchPartyData* pouch_data = 
        ttyd::mario_pouch::pouchGetPtr()->party_data;
    for (int32_t i = 0; i < 7; ++i) {
        int32_t id = party_data[i].partner_id;
        if ((pouch_data[id].flags & 1) && id != party_id) {
            *current_order = party_data + i;
            ++current_order;
        }
    }
}

bool CheckForUnusableItemInMenu() {
    void* winPtr = ttyd::win_main::winGetPtr();
    const int32_t item = reinterpret_cast<int32_t*>(winPtr)[0x2d4 / 4];
    
    // If not a Shine Sprite, item is not unusable; can return.
    if (item != ItemType::GOLD_BAR_X3) return false;
    
    // If the player isn't actively making a selection, can return safely.
    uint32_t& buttons = reinterpret_cast<uint32_t*>(winPtr)[0x4 / 4];
    if (!(buttons & ButtonId::A) || (buttons & ButtonId::B)) return false;
    
    WinPartyData* party_data[7];
    GetPartyMemberMenuOrder(party_data);
    int32_t& party_member_target = reinterpret_cast<int32_t*>(winPtr)[0x2dc / 4];
    
    // Mario is selected.
    if (party_member_target == 0) {
        if (g_Mod->state_.GetOptionValue(
            StateManager::SHINE_SPRITES_MARIO)) {
            // Can use Shine Sprites only when Mario has SP.
            if (ttyd::mario_pouch::pouchGetMaxAP() > 0) return false;
        } else {
            // Shine Sprites will be used on partners instead.
            return false;
        }
    } else {
        // Shine Sprites can always be used on partners.
        return false;
    }
    
    // The item cannot be used; play a sound effect and return true.
    ttyd::sound::SoundEfxPlayEx(0x266, 0, 0x64, 0x40);
    return true;
}

void UseSpecialItemsInMenu(WinPartyData** party_data) {
    void* winPtr = ttyd::win_main::winGetPtr();
    const int32_t item = reinterpret_cast<int32_t*>(winPtr)[0x2d4 / 4];
    
    // If the item is a Strawberry Cake or Shine Sprite...
    if (item == ItemType::CAKE || item == ItemType::GOLD_BAR_X3) {
        int32_t& party_member_target = 
            reinterpret_cast<int32_t*>(winPtr)[0x2dc / 4];
        if (party_member_target == 0 && item == ItemType::GOLD_BAR_X3 &&
            !g_Mod->state_.GetOptionValue(
                StateManager::SHINE_SPRITES_MARIO)) {
            // If Mario is selected for using a Shine Sprite, change the
            // target to the active party member instead.
            party_member_target = 1;
        }
        
        int32_t selected_party_id = 0;
        if (party_member_target > 0) {
            // Convert the selected menu index into the PouchPartyData index.
            selected_party_id = party_data[party_member_target - 1]->partner_id;
        }
        
        if (item == ItemType::CAKE) {
            // Add just bonus HP / FP (the base is added after this function).
            if (selected_party_id == 0) {
                ttyd::mario_pouch::pouchSetHP(
                    ttyd::mario_pouch::pouchGetHP() +
                    item::GetBonusCakeRestoration());
            } else {
                ttyd::mario_pouch::pouchSetPartyHP(
                    selected_party_id,
                    ttyd::mario_pouch::pouchGetPartyHP(selected_party_id) + 
                    item::GetBonusCakeRestoration());
            }
            ttyd::mario_pouch::pouchSetFP(
                ttyd::mario_pouch::pouchGetFP() +
                item::GetBonusCakeRestoration());
        } else if (item == ItemType::GOLD_BAR_X3) {
            if (selected_party_id == 0) {
                // Mario selected; add +0.5 max SP (up to +7.0) and restore SP.
                PouchData& pouch = *ttyd::mario_pouch::pouchGetPtr();
                if (pouch.max_sp - g_Mod->state_.StarPowersObtained() 
                    * 100 < 700) {
                    pouch.max_sp += 50;
                }
                pouch.current_sp = pouch.max_sp;
            } else {
                ttyd::mario_pouch::PouchPartyData* pouch_data =
                    ttyd::mario_pouch::pouchGetPtr()->party_data +
                    selected_party_id;
                int16_t* hp_table = 
                    ttyd::mario_pouch::_party_max_hp_table +
                    selected_party_id * 4;
                    
                // Rank the selected party member up and fully heal them.
                if (pouch_data->hp_level < 2) {
                    ++pouch_data->hp_level;
                    ++pouch_data->attack_level;
                    ++pouch_data->tech_level;
                } else {
                    // Increase the Ultra Rank's max HP by 5.
                    if (hp_table[2] < 200) hp_table[2] += 5;
                }
                pouch_data->base_max_hp = hp_table[pouch_data->hp_level];
                pouch_data->current_hp = hp_table[pouch_data->hp_level];
                pouch_data->max_hp = hp_table[pouch_data->hp_level];
                // Include HP Plus P in current / max stats.
                const int32_t hp_plus_p_cnt =
                    ttyd::mario_pouch::pouchEquipCheckBadge(ItemType::HP_PLUS_P);
                pouch_data->current_hp += 5 * hp_plus_p_cnt;
                pouch_data->max_hp += 5 * hp_plus_p_cnt;
                
                // Save the partner upgrade count to the randomizer state.
                ++g_Mod->state_.partner_upgrades_[selected_party_id - 1];
            }
            
            // Increment the number of actual Shine Sprites, so it shows
            // the total count used in the Mario menu.
            if (ttyd::mario_pouch::pouchGetPtr()->shine_sprites < 999) {
                ++ttyd::mario_pouch::pouchGetPtr()->shine_sprites;
            }
        }
    }
    
    // Track items used in the menu...
    g_Mod->state_.IncrementPlayStat(StateManager::ITEMS_USED);
    
    // Run normal logic to add HP, FP, and SP afterwards...
}

}  // namespace ui
}  // namespace mod::infinite_pit