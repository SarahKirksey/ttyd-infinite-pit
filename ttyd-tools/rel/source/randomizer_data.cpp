#include "randomizer_data.h"

#include "common_functions.h"
#include "common_types.h"
#include "randomizer.h"

#include <ttyd/battle_database_common.h>
#include <ttyd/mariost.h>
#include <ttyd/npcdrv.h>
#include <ttyd/npc_data.h>
#include <ttyd/npc_event.h>

#include <cstdint>
#include <cstring>

namespace mod::pit_randomizer {

namespace {

using ::ttyd::npcdrv::NpcSetupInfo;
using ::ttyd::npcdrv::NpcTribeDescription;
using namespace ::ttyd::battle_database_common;  // for convenience
using namespace ::ttyd::npc_event;               // for convenience

// Events to run for a particular class of NPC (e.g. Goomba-like enemies).
struct NpcEntTypeInfo {
    int32_t* init_event;
    int32_t* regular_event;
    int32_t* dead_event;
    int32_t* find_event;
    int32_t* lost_event;
    int32_t* return_event;
    int32_t* blow_event;
};

// Stats for a particular kind of enemy (e.g. Hyper Goomba).
struct EnemyTypeInfo {
    BattleUnitType::e unit_type;
    int32_t         npc_tribe_idx;
    // How quickly the enemy's HP, ATK and DEF scale with the floor number.
    float           hp_scale;
    float           atk_scale;
    float           def_scale;
    // The reference point used as the enemy's "base" attack power; other
    // attacks will have the same difference in power as in the original game.
    // (e.g. a Hyper Goomba will charge by its attack power + 4).
    int32_t         atk_base;
    // The enemy's level will be this much higher than Mario's at base.
    int32_t         level_offset;
    // The enemy's HP and FP drop yields (this info isn't in BattleUnitSetup).
    PointDropData*  hp_drop_table;
    PointDropData*  fp_drop_table;
    // Makes a type of audience member more likely to spawn (-1 = none).
    int32_t         audience_type_boosted;
};

// All data required to construct a particular enemy NPC in a particular module.
// In particular, contains the offset in the given module for an existing
// BattleUnitSetup* to use as a reference for the constructed battle.
struct EnemyModuleInfo {
    BattleUnitType::e   unit_type;
    ModuleId::e         module;
    int32_t             battle_unit_setup_offset;
    int32_t             npc_ent_type_info_idx;
    int32_t             enemy_type_stats_idx;
};

PointDropData* kHpTables[] = {
    &battle_heart_drop_param_default, &battle_heart_drop_param_default2,
    &battle_heart_drop_param_default3, &battle_heart_drop_param_default4,
    &battle_heart_drop_param_default5
};
PointDropData* kFpTables[] = {
    &battle_flower_drop_param_default, &battle_flower_drop_param_default2,
    &battle_flower_drop_param_default3, &battle_flower_drop_param_default4,
    &battle_flower_drop_param_default5
};
const float kEnemyPartyCenterX = 90.0f;

const NpcEntTypeInfo kNpcInfo[] = {
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
    { &kuriboo_init_event, &kuriboo_move_event, &enemy_common_dead_event, &kuriboo_find_event, &kuriboo_lost_event, &kuriboo_return_event, &enemy_common_blow_event },
    { &patakuri_init_event, &patakuri_move_event, &enemy_common_dead_event, &patakuri_find_event, &patakuri_lost_event, &patakuri_return_event, &enemy_common_blow_event },
    { &nokonoko_init_event, &nokonoko_move_event, &enemy_common_dead_event, &nokonoko_find_event, &nokonoko_lost_event, &nokonoko_return_event, &enemy_common_blow_event },
    { &togenoko_init_event, &togenoko_move_event, &enemy_common_dead_event, &togenoko_find_event, &togenoko_lost_event, &togenoko_return_event, &enemy_common_blow_event },
    { &patapata_init_event, &patapata_move_event, &enemy_common_dead_event, &patapata_find_event, &patapata_lost_event, &patapata_return_event, &enemy_common_blow_event },
    { &met_init_event, &met_move_event, &enemy_common_dead_event, &met_find_event, &met_lost_event, &met_return_event, &enemy_common_blow_event },
    { &patamet_init_event, &patamet_move_event, &enemy_common_dead_event, &patamet_find_event, &patamet_lost_event, &patamet_return_event, &enemy_common_blow_event },
    { &chorobon_init_event, &chorobon_move_event, &enemy_common_dead_event, &chorobon_find_event, &chorobon_lost_event, &chorobon_return_event, &enemy_common_blow_event },
    { &pansy_init_event, &pansy_move_event, &enemy_common_dead_event, &pansy_find_event, &pansy_lost_event, &pansy_return_event, &enemy_common_blow_event },
    { &twinkling_pansy_init_event, &twinkling_pansy_move_event, &enemy_common_dead_event, &twinkling_pansy_find_event, &twinkling_pansy_lost_event, &twinkling_pansy_return_event, &enemy_common_blow_event },
    { &karon_init_event, &karon_move_event, &enemy_common_dead_event, &karon_find_event, &karon_lost_event, &karon_return_event, &karon_blow_event },
    { &honenoko2_init_event, &honenoko2_move_event, &enemy_common_dead_event, &honenoko2_find_event, &honenoko2_lost_event, &honenoko2_return_event, &enemy_common_blow_event },
    { &killer_init_event, &killer_move_event, &killer_dead_event, nullptr, nullptr, nullptr, &enemy_common_blow_event },
    { &killer_cannon_init_event, &killer_cannon_move_event, &enemy_common_dead_event, nullptr, nullptr, nullptr, &enemy_common_blow_event },
    { &sambo_init_event, &sambo_move_event, &enemy_common_dead_event, &sambo_find_event, &sambo_lost_event, &sambo_return_event, &enemy_common_blow_event },
    { &sinnosuke_init_event, &sinnosuke_move_event, &enemy_common_dead_event, &sinnosuke_find_event, &sinnosuke_lost_event, &sinnosuke_return_event, &enemy_common_blow_event },
    { &sinemon_init_event, &sinemon_move_event, &enemy_common_dead_event, &sinemon_find_event, &sinemon_lost_event, &sinemon_return_event, &enemy_common_blow_event },
    { &togedaruma_init_event, &togedaruma_move_event, &enemy_common_dead_event, &togedaruma_find_event, &togedaruma_lost_event, &togedaruma_return_event, &enemy_common_blow_event },
    { &barriern_init_event, &barriern_move_event, &barriern_dead_event, &barriern_find_event, &barriern_lost_event, &barriern_return_event, &barriern_blow_event },
    { &piders_init_event, &piders_move_event, &enemy_common_dead_event, &piders_find_event, nullptr, nullptr, &piders_blow_event },
    { &pakkun_init_event, &pakkun_move_event, &enemy_common_dead_event, &pakkun_find_event, nullptr, &pakkun_return_event, &enemy_common_blow_event },
    { &dokugassun_init_event, &dokugassun_move_event, &enemy_common_dead_event, &dokugassun_find_event, &dokugassun_lost_event, &dokugassun_return_event, &enemy_common_blow_event },
    { &basabasa2_init_event, &basabasa2_move_event, &basabasa2_dead_event, &basabasa2_find_event, &basabasa2_lost_event, &basabasa2_return_event, &enemy_common_blow_event },
    { &teresa_init_event, &teresa_move_event, &enemy_common_dead_event, &teresa_find_event, &teresa_lost_event, &teresa_return_event, &enemy_common_blow_event },
    { &bubble_init_event, &bubble_move_event, &enemy_common_dead_event, &bubble_find_event, &bubble_lost_event, &bubble_return_event, &enemy_common_blow_event },
    { &hbom_init_event, &hbom_move_event, &enemy_common_dead_event, &hbom_find_event, &hbom_lost_event, &hbom_return_event, &enemy_common_blow_event },
    { &zakowiz_init_event, &zakowiz_move_event, &zakowiz_dead_event, &zakowiz_find_event, &zakowiz_lost_event, &zakowiz_return_event, &zakowiz_blow_event },
    { &hannya_init_event, &hannya_move_event, &enemy_common_dead_event, &hannya_find_event, &hannya_lost_event, &hannya_return_event, &enemy_common_blow_event },
    { &mahoon_init_event, &mahoon_move_event, &mahoon_dead_event, &mahoon_find_event, &mahoon_lost_event, &mahoon_return_event, &enemy_common_blow_event },
    { &kamec_init_event, &kamec_move_event, &kamec_dead_event, &kamec_find_event, &kamec_lost_event, &kamec_return_event, &kamec_blow_event },
    { &kamec2_init_event, &kamec2_move_event, &kamec2_dead_event, &kamec2_find_event, &kamec2_lost_event, &kamec2_return_event, &kamec2_blow_event },
    { &hbross_init_event, &hbross_move_event, &hbross_dead_event, &hbross_find_event, &hbross_lost_event, &hbross_return_event, &hbross_blow_event },
    { &wanwan_init_event, &wanwan_move_event, &enemy_common_dead_event, &wanwan_find_event, nullptr, nullptr, &enemy_common_blow_event },
    { nullptr, nullptr, &enemy_common_dead_event, nullptr, nullptr, nullptr, nullptr },
};

const EnemyTypeInfo kEnemyInfo[] = {
    { BattleUnitType::BONETAIL, 325, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::ATOMIC_BOO, 148, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BANDIT, 274, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BIG_BANDIT, 129, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BADGE_BANDIT, 275, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BILL_BLASTER, 254, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOMBSHELL_BILL_BLASTER, 256, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BULLET_BILL, 255, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOMBSHELL_BILL, 257, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOB_OMB, 283, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BULKY_BOB_OMB, 304, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOB_ULK, 305, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DULL_BONES, 39, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::RED_BONES, 36, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DRY_BONES, 196, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_BONES, 197, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOO, 146, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_BOO, 147, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BRISTLE, 258, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_BRISTLE, 259, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HAMMER_BRO, 206, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BOOMERANG_BRO, 294, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::FIRE_BRO, 293, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::LAVA_BUBBLE, 302, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::EMBER, 159, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PHANTOM_EMBER, 303, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::BUZZY_BEETLE, 225, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPIKE_TOP, 226, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PARABUZZY, 228, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPIKY_PARABUZZY, 227, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::RED_SPIKY_BUZZY, 230, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::CHAIN_CHOMP, 301, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::RED_CHOMP, 306, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::CLEFT, 237, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HYPER_CLEFT, 236, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::MOON_CLEFT, 235, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HYPER_BALD_CLEFT, 288, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_CRAW, 308, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::CRAZEE_DAYZEE, 252, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::AMAZY_DAYZEE, 253, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::FUZZY, 248, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::GREEN_FUZZY, 249, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::FLOWER_FUZZY, 250, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::GOOMBA, 214, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPIKY_GOOMBA, 215, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PARAGOOMBA, 216, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HYPER_GOOMBA, 217, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HYPER_SPIKY_GOOMBA, 218, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::HYPER_PARAGOOMBA, 219, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::GLOOMBA, 220, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPIKY_GLOOMBA, 221, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PARAGLOOMBA, 222, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::KOOPA_TROOPA, 242, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PARATROOPA, 243, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::KP_KOOPA, 246, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::KP_PARATROOPA, 247, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SHADY_KOOPA, 282, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SHADY_PARATROOPA, 291, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_KOOPA, 244, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_PARATROOPA, 245, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::KOOPATROL, 205, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_KOOPATROL, 307, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::LAKITU, 280, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_LAKITU, 281, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPINY, 287, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SKY_BLUE_SPINY, -1, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::RED_MAGIKOOPA, 318, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::WHITE_MAGIKOOPA, 319, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::GREEN_MAGIKOOPA, 320, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::MAGIKOOPA, 321, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::X_NAUT, 271, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::X_NAUT_PHD, 273, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::ELITE_X_NAUT, 272, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PIDER, 266, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::ARANTULA, 267, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PALE_PIRANHA, 261, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PUTRID_PIRANHA, 262, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::FROST_PIRANHA, 263, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::PIRANHA_PLANT, 260, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::POKEY, 233, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::POISON_POKEY, 234, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_PUFF, 286, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::RUFF_PUFF, 284, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::ICE_PUFF, 285, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::POISON_PUFF, 265, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPINIA, 310, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPANIA, 309, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SPUNIA, 311, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SWOOPER, 239, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SWOOPULA, 240, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::SWAMPIRE, 241, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::WIZZERD, 295, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::DARK_WIZZERD, 296, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::ELITE_WIZZERD, 297, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::YUX, 268, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::Z_YUX, 269, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::X_YUX, 270, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::MINI_YUX, -1, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::MINI_Z_YUX, -1, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { BattleUnitType::MINI_X_YUX, -1, 0, 0, 0, 0, 0, kHpTables[0], kFpTables[0], -1 },
    { /* invalid enemy */ },
};

const EnemyModuleInfo kEnemyModuleInfo[] = {
    { BattleUnitType::BONETAIL, ModuleId::JON, 0x159d0, 34, 0 },
    { BattleUnitType::ATOMIC_BOO, ModuleId::JIN, 0x1a6a8, 24, 1 },
    { BattleUnitType::GLOOMBA, ModuleId::JON, 0x15a20, 1, 49 },
    { BattleUnitType::SPINIA, ModuleId::JON, 0x15b70, 28, 85 },
    { BattleUnitType::SPANIA, ModuleId::JON, 0x15e10, 28, 86 },
    { BattleUnitType::DULL_BONES, ModuleId::JON, 0x16050, 12, 12 },
    { BattleUnitType::FUZZY, ModuleId::JON, 0x16260, 8, 40 },
    { BattleUnitType::PARAGLOOMBA, ModuleId::JON, 0x16500, 2, 51 },
    { BattleUnitType::CLEFT, ModuleId::JON, 0x166e0, 17, 33 },
    { BattleUnitType::POKEY, ModuleId::JON, 0x16920, 15, 79 },
    { BattleUnitType::DARK_PUFF, ModuleId::JON, 0x16b60, 22, 81 },
    { BattleUnitType::PIDER, ModuleId::JON, 0x16d40, 20, 73 },
    { BattleUnitType::SPIKY_GLOOMBA, ModuleId::JON, 0x16f80, 1, 50 },
    { BattleUnitType::BANDIT, ModuleId::JON, 0x171f0, 1, 2 },
    { BattleUnitType::LAKITU, ModuleId::JON, 0x17490, 25, 62 },
    { BattleUnitType::BOB_OMB, ModuleId::JON, 0x176d0, 1, 9 },
    { BattleUnitType::BOO, ModuleId::JON, 0x17940, 24, 16 },
    { BattleUnitType::DARK_KOOPA, ModuleId::JON, 0x17bb0, 3, 58 },
    { BattleUnitType::HYPER_CLEFT, ModuleId::JON, 0x17d90, 17, 34 },
    { BattleUnitType::PARABUZZY, ModuleId::JON, 0x17fa0, 7, 28 },
    { BattleUnitType::SHADY_KOOPA, ModuleId::JON, 0x181e0, 3, 56 },
    { BattleUnitType::FLOWER_FUZZY, ModuleId::JON, 0x18420, 8, 42 },
    { BattleUnitType::DARK_PARATROOPA, ModuleId::JON, 0x18660, 5, 59 },
    { BattleUnitType::BULKY_BOB_OMB, ModuleId::JON, 0x188a0, 26, 10 },
    { BattleUnitType::LAVA_BUBBLE, ModuleId::JON, 0x18ab0, 25, 23 },
    { BattleUnitType::POISON_POKEY, ModuleId::JON, 0x18cf0, 15, 80 },
    { BattleUnitType::SPIKY_PARABUZZY, ModuleId::JON, 0x18f30, 7, 29 },
    { BattleUnitType::BADGE_BANDIT, ModuleId::JON, 0x19170, 1, 4 },
    { BattleUnitType::ICE_PUFF, ModuleId::JON, 0x19380, 22, 83 },
    { BattleUnitType::DARK_BOO, ModuleId::JON, 0x19590, 24, 17 },
    { BattleUnitType::RED_CHOMP, ModuleId::JON, 0x197d0, 33, 32 },
    { BattleUnitType::MOON_CLEFT, ModuleId::JON, 0x199e0, 17, 35 },
    { BattleUnitType::DARK_LAKITU, ModuleId::JON, 0x19c20, 25, 63 },
    { BattleUnitType::DRY_BONES, ModuleId::JON, 0x19e00, 11, 14 },
    { BattleUnitType::DARK_WIZZERD, ModuleId::JON, 0x1a010, 29, 92 },
    { BattleUnitType::FROST_PIRANHA, ModuleId::JON, 0x1a220, 21, 77 },
    { BattleUnitType::DARK_CRAW, ModuleId::JON, 0x1a430, 1, 37 },
    { BattleUnitType::WIZZERD, ModuleId::JON, 0x1a5e0, 29, 91 },
    { BattleUnitType::DARK_KOOPATROL, ModuleId::JON, 0x1a7f0, 4, 61 },
    { BattleUnitType::PHANTOM_EMBER, ModuleId::JON, 0x1aa00, 25, 25 },
    { BattleUnitType::SWOOPULA, ModuleId::JON, 0x1acd0, 23, 89 },
    { BattleUnitType::CHAIN_CHOMP, ModuleId::JON, 0x1af70, 33, 31 },
    { BattleUnitType::SPUNIA, ModuleId::JON, 0x1b1b0, 28, 87 },
    { BattleUnitType::DARK_BRISTLE, ModuleId::JON, 0x1b420, 18, 19 },
    { BattleUnitType::ARANTULA, ModuleId::JON, 0x1b630, 20, 74 },
    { BattleUnitType::PIRANHA_PLANT, ModuleId::JON, 0x1b870, 21, 78 },
    { BattleUnitType::ELITE_WIZZERD, ModuleId::JON, 0x1bd80, 29, 93 },
    { BattleUnitType::POISON_PUFF, ModuleId::JON, 0x1bff0, 22, 84 },
    { BattleUnitType::BOB_ULK, ModuleId::JON, 0x1c290, 26, 11 },
    { BattleUnitType::SWAMPIRE, ModuleId::JON, 0x1c500, 23, 90 },
    { BattleUnitType::AMAZY_DAYZEE, ModuleId::JON, 0x1c7a0, 10, 39 },
    { BattleUnitType::GOOMBA, ModuleId::GON, 0x16efc, 1, 43 },
    { BattleUnitType::SPIKY_GOOMBA, ModuleId::GON, 0x16efc, 1, 44 },
    { BattleUnitType::PARAGOOMBA, ModuleId::GON, 0x169dc, 2, 45 },
    { BattleUnitType::KOOPA_TROOPA, ModuleId::GON, 0x16cbc, 3, 52 },
    { BattleUnitType::PARATROOPA, ModuleId::GON, 0x1660c, 5, 53 },
    { BattleUnitType::RED_BONES, ModuleId::GON, 0x166bc, 12, 13 },
    { BattleUnitType::GOOMBA, ModuleId::GRA, 0x8690, 1, 43 },
    { BattleUnitType::HYPER_GOOMBA, ModuleId::GRA, 0x8690, 1, 46 },
    { BattleUnitType::HYPER_PARAGOOMBA, ModuleId::GRA, 0x8950, 2, 48 },
    { BattleUnitType::HYPER_SPIKY_GOOMBA, ModuleId::GRA, 0x8c70, 1, 47 },
    { BattleUnitType::CRAZEE_DAYZEE, ModuleId::GRA, 0x9090, 9, 38 },
    { BattleUnitType::GOOMBA, ModuleId::TIK, 0x27030, 1, 43 },
    { BattleUnitType::PARAGOOMBA, ModuleId::TIK, 0x27080, 2, 45 },
    { BattleUnitType::SPIKY_GOOMBA, ModuleId::TIK, 0x270b0, 1, 44 },
    { BattleUnitType::KOOPA_TROOPA, ModuleId::TIK, 0x26d60, 3, 52 },
    { BattleUnitType::HAMMER_BRO, ModuleId::TIK, 0x27120, 32, 20 },
    { BattleUnitType::MAGIKOOPA, ModuleId::TIK, 0x267d0, 31, 69 },
    { BattleUnitType::KOOPATROL, ModuleId::TIK, 0x26d30, 4, 60 },
    { BattleUnitType::GOOMBA, ModuleId::TOU2, 0x1eb40, 1, 43 },
    { BattleUnitType::KP_KOOPA, ModuleId::TOU2, 0x1ec50, 3, 54 },
    { BattleUnitType::KP_PARATROOPA, ModuleId::TOU2, 0x1ecb0, 5, 55 },
    { BattleUnitType::SHADY_PARATROOPA, ModuleId::TOU2, 0x1f3e0, 5, 57 },
    { BattleUnitType::HAMMER_BRO, ModuleId::TOU2, 0x1f610, 32, 20 },
    { BattleUnitType::BOOMERANG_BRO, ModuleId::TOU2, 0x1f670, 1, 21 },
    { BattleUnitType::FIRE_BRO, ModuleId::TOU2, 0x1f640, 1, 22 },
    { BattleUnitType::RED_MAGIKOOPA, ModuleId::TOU2, 0x1f510, 31, 66 },
    { BattleUnitType::WHITE_MAGIKOOPA, ModuleId::TOU2, 0x1f540, 31, 67 },
    { BattleUnitType::GREEN_MAGIKOOPA, ModuleId::TOU2, 0x1f570, 31, 68 },
    { BattleUnitType::RED_SPIKY_BUZZY, ModuleId::TOU2, 0x1f2b0, 6, 30 },
    { BattleUnitType::GREEN_FUZZY, ModuleId::TOU2, 0x1f490, 8, 41 },
    { BattleUnitType::PALE_PIRANHA, ModuleId::TOU2, 0x1ef10, 21, 75 },
    { BattleUnitType::BIG_BANDIT, ModuleId::TOU2, 0x1f1b0, 1, 3 },
    { BattleUnitType::SWOOPER, ModuleId::TOU2, 0x1f790, 23, 88 },
    { BattleUnitType::HYPER_BALD_CLEFT, ModuleId::TOU2, 0x1efc0, 16, 36 },
    { BattleUnitType::BRISTLE, ModuleId::TOU2, 0x1f330, 18, 18 },
    { BattleUnitType::X_NAUT, ModuleId::AJI, 0x44914, 1, 70 },
    { BattleUnitType::ELITE_X_NAUT, ModuleId::AJI, 0x44894, 1, 72 },
    { BattleUnitType::X_NAUT_PHD, ModuleId::AJI, 0x44aa4, 27, 71 },
    { BattleUnitType::YUX, ModuleId::AJI, 0x44f44, 19, 94 },
    { BattleUnitType::Z_YUX, ModuleId::AJI, 0x44b24, 19, 95 },
    { BattleUnitType::X_YUX, ModuleId::AJI, 0x450d4, 19, 96 },
    { BattleUnitType::GOOMBA, ModuleId::DOU, 0x163d8, 1, 43 },
    { BattleUnitType::BILL_BLASTER, ModuleId::DOU, 0x16698, 14, 5 },
    { BattleUnitType::EMBER, ModuleId::DOU, 0x16488, 25, 24 },
    { BattleUnitType::RED_BONES, ModuleId::LAS, 0x3c100, 12, 13 },
    { BattleUnitType::DARK_BONES, ModuleId::LAS, 0x3c1a0, 11, 15 },
    { BattleUnitType::BOMBSHELL_BILL_BLASTER, ModuleId::LAS, 0x3cab0, 14, 6 },
    { BattleUnitType::BUZZY_BEETLE, ModuleId::JIN, 0x1b078, 6, 26 },
    { BattleUnitType::SPIKE_TOP, ModuleId::JIN, 0x1b148, 6, 27 },
    { BattleUnitType::SWOOPER, ModuleId::JIN, 0x1ab38, 23, 88 },
    { BattleUnitType::GREEN_FUZZY, ModuleId::MUJ, 0x358b8, 8, 41 },
    { BattleUnitType::PUTRID_PIRANHA, ModuleId::MUJ, 0x35ac8, 21, 76 },
    { BattleUnitType::EMBER, ModuleId::MUJ, 0x35218, 25, 24 },
    { BattleUnitType::GOOMBA, ModuleId::EKI, 0xff48, 1, 43 },
    { BattleUnitType::RUFF_PUFF, ModuleId::EKI, 0xfff8, 22, 82 },
};

// Global structures for holding constructed battle information.
int32_t g_NumEnemies = 0;
int32_t g_Enemies[5] = { -1, -1, -1, -1, -1 };
NpcSetupInfo g_CustomNpc[2];
BattleUnitSetup g_CustomUnits[6];
BattleGroupSetup g_CustomBattleParty;
// AudienceTypeWeights g_CustomAudienceWeights[16];

}

ModuleId::e SelectEnemies(int32_t floor) {
    // TODO: Procedurally pick a group of enemies based on the floor number;
    // currently hardcoded to test one enemy type, in a group of 3.
    g_NumEnemies = 3;
    for (int32_t i = 0; i < 5; ++i) {
        if (i < g_NumEnemies) {
            g_Enemies[i] = g_EnemyTypeToTest;  // from randomizer.h
        } else {
            g_Enemies[i] = -1;
        }
    }
    
    for (int32_t i = 0; i < g_NumEnemies; ++i) {
        const EnemyModuleInfo* emi = kEnemyModuleInfo + g_Enemies[i];
        if (emi->module != ModuleId::JON) {
            return emi->module;
        }
    }
    return ModuleId::INVALID_MODULE;
}

void BuildBattle(
    uintptr_t pit_module_ptr, int32_t floor,
    NpcTribeDescription** out_npc_tribe_description,
    NpcSetupInfo** out_npc_setup_info) {

    const EnemyModuleInfo* enemy_module_info[5];
    const EnemyTypeInfo* enemy_info[5];
    const NpcEntTypeInfo* npc_info = nullptr;
    const BattleUnitSetup* unit_info[5];
    
    for (int32_t i = 0; i < g_NumEnemies; ++i) {
        uintptr_t module_ptr = pit_module_ptr;
        enemy_module_info[i] = kEnemyModuleInfo + g_Enemies[i];
        enemy_info[i] = kEnemyInfo + enemy_module_info[i]->enemy_type_stats_idx;
        if (enemy_module_info[i]->module != ModuleId::JON) {
            module_ptr = reinterpret_cast<uintptr_t>(
                ttyd::mariost::g_MarioSt->pMapAlloc);
        }
        if (i == 0) {
            npc_info = kNpcInfo + enemy_module_info[i]->npc_ent_type_info_idx;
        }
        unit_info[i] = reinterpret_cast<const BattleUnitSetup*>(
            module_ptr + enemy_module_info[i]->battle_unit_setup_offset);
    }
    
    // Construct the data for the NPC on the field from the lead enemy's info.
    NpcTribeDescription* npc_tribe =
        ttyd::npc_data::npcTribe + enemy_info[0]->npc_tribe_idx;
    g_CustomNpc[0].nameJp           = "\x93\x47";  // enemy
    g_CustomNpc[0].flags            = 0x1000000a;
    g_CustomNpc[0].reactionFlags    = 0;
    g_CustomNpc[0].initEvtCode      = npc_info->init_event;
    g_CustomNpc[0].regularEvtCode   = npc_info->regular_event;
    g_CustomNpc[0].talkEvtCode      = nullptr;
    g_CustomNpc[0].deadEvtCode      = npc_info->dead_event;
    g_CustomNpc[0].findEvtCode      = npc_info->find_event;
    g_CustomNpc[0].lostEvtCode      = npc_info->lost_event;
    g_CustomNpc[0].returnEvtCode    = npc_info->return_event;
    g_CustomNpc[0].blowEvtCode      = npc_info->blow_event;
    g_CustomNpc[0].territoryType    = ttyd::npcdrv::NpcTerritoryType::kSquare;
    g_CustomNpc[0].territoryBase    = { 0.0f, 0.0f, 0.0f };
    g_CustomNpc[0].territoryLoiter  = { 150.0f, 100.0f, 100.0f };
    g_CustomNpc[0].searchRange      = 200.0f;
    g_CustomNpc[0].searchAngle      = 300.0f;
    g_CustomNpc[0].homingRange      = 1000.0f;
    g_CustomNpc[0].homingAngle      = 360.0f;
    g_CustomNpc[0].battleInfoId     = 1;
    
    *out_npc_tribe_description = npc_tribe;
    *out_npc_setup_info = g_CustomNpc;
    
    // TODO: Generate custom audience weights.
    for (int32_t i = 0; i < g_NumEnemies; ++i) {
        BattleUnitSetup& custom_unit = g_CustomUnits[i];
        memcpy(&custom_unit, unit_info[i], sizeof(BattleUnitSetup));
        // TODO: Place enemies in the proper positions.
        // TODO: Link to Goomba's unit_kind if Goomba is the type selected.
        custom_unit.position.x = kEnemyPartyCenterX + (i-1) * 40.0f;
    }
    g_CustomBattleParty.num_enemies         = g_NumEnemies;
    g_CustomBattleParty.enemy_data          = g_CustomUnits;
    g_CustomBattleParty.held_item_weight    = 100;
    g_CustomBattleParty.random_item_weight  = 0;
    g_CustomBattleParty.no_item_weight      = 0;
    g_CustomBattleParty.hp_drop_table       = enemy_info[0]->hp_drop_table;
    g_CustomBattleParty.fp_drop_table       = enemy_info[0]->fp_drop_table;
    
    // Make the current floor's battle point to the constructed party setup.
    int8_t* enemy_100 =
        reinterpret_cast<int8_t*>(pit_module_ptr + kPitEnemy100Offset);
    BattleSetupData* pit_battle_setups =
        reinterpret_cast<BattleSetupData*>(
            pit_module_ptr + kPitBattleSetupTblOffset);
    BattleSetupData* battle_setup = pit_battle_setups + enemy_100[floor % 100];
    battle_setup->flag_off_loadouts[0].group_data = &g_CustomBattleParty;
    battle_setup->flag_off_loadouts[1].weight = 0;
    battle_setup->flag_off_loadouts[1].group_data = nullptr;
    battle_setup->flag_off_loadouts[1].stage_data = nullptr;
    // If floor > 100, fix the background to always display the floor 80+ bg.
    if (floor > 100 && floor % 100 != 99) {
        battle_setup->flag_off_loadouts[0].stage_data =
            pit_battle_setups[50].flag_off_loadouts[0].stage_data;
    }
    // TODO: other battle setup data tweaks (audience makeup, etc.)?
}

}