#ifndef BOT_TACTICAL_AI_H
#define BOT_TACTICAL_AI_H

// Forward declaration if Vector is from another file like extdll.h (assuming it's available)
// struct Vector;
// Actually, it's better to include the necessary header if Vector is defined elsewhere,
// e.g. from "extdll.h" or a specific math header. For HPB_bot, Vector is in extdll.h.
#include "extdll.h" // For Vector definition
// #include "bot_constants.h" // For MAX_TEAMS, MAX_OBJECTIVES (to be created or use existing limits)

// Placeholder if bot_constants.h isn't created yet for these specific constants
#ifndef MAX_TEAMS
#define MAX_TEAMS 4
#endif
#ifndef MAX_OBJECTIVES
#define MAX_OBJECTIVES 16 // Max number of trackable objectives on a map
#endif
#ifndef MAX_CLASSES_PER_MOD // Max distinct classes a mod might have (e.g. TFC has ~10)
#define MAX_CLASSES_PER_MOD 12
#endif


// Enum for Objective Types
typedef enum {
    OBJ_TYPE_NONE = 0,
    OBJ_TYPE_FLAG,              // Original, might be too specific if we want to learn "flag-like"
    OBJ_TYPE_CAPTURE_POINT,     // Original
    OBJ_TYPE_BOMB_TARGET,       // Original
    OBJ_TYPE_RESCUE_ZONE,       // Original
    OBJ_TYPE_RESOURCE_NODE,     // Original (for health/ammo/armor packs)
    OBJ_TYPE_STRATEGIC_LOCATION,// Original (generic important spot)

    // New semantic types to be learned:
    OBJ_TYPE_FLAG_LIKE_PICKUP,    // Something to be touched/used and "carried" or causes immediate major state change
    OBJ_TYPE_CONTROL_AREA,        // Area where prolonged presence leads to capture/score
    OBJ_TYPE_DESTRUCTIBLE_TARGET, // Something to be shot/damaged for an effect
    OBJ_TYPE_HEALTH_REFILL_STATION, // Specific type for health chargers
    OBJ_TYPE_ARMOR_REFILL_STATION,  // Specific type for HEV chargers
    OBJ_TYPE_AMMO_REFILL_POINT,   // Could be for ammo packs if not covered by RESOURCE_NODE
    OBJ_TYPE_PRESSABLE_BUTTON,    // func_button or similar
    OBJ_TYPE_DOOR_OBSTACLE,       // A door that needs opening to progress
    OBJ_TYPE_WEAPON_SPAWN         // For a weapon pickup location
    // Add more as concepts develop
} ObjectiveType_e;

// Enum for Game Phase
typedef enum {
    GAME_PHASE_UNKNOWN = 0,
    GAME_PHASE_WARMUP,
    GAME_PHASE_SETUP,        // e.g., CS buy time
    GAME_PHASE_ROUND_ACTIVE,
    GAME_PHASE_ROUND_ENDING, // e.g., bomb planted, flag taken near cap
    GAME_PHASE_POST_ROUND,
    GAME_PHASE_GAME_OVER
} GamePhase_e;

typedef enum {
    PROP_OWNER_TEAM,
    PROP_FLAG_CARRIER_TEAM,
    PROP_FLAG_POSITION,
    PROP_IS_CONTESTED,
    PROP_IS_ACTIVE
} ObjectiveProperty_e;


typedef struct {
    int id;                             // Waypoint index or other unique ID
    ObjectiveType_e type;
    Vector position;
    int owner_team;                     // Owning team ID (-1 if neutral, or 0, 1, etc.)
    bool is_contested;                  // True if significant enemy presence near friendly objective, or vice-versa

    // Flag specific status (relevant if type == OBJ_TYPE_FLAG)
    bool is_flag_at_home_base;
    int flag_carrier_team;              // Team ID of the flag carrier (-1 if not carried, -2 if carrier unknown but flag not home)
    Vector flag_current_position;       // Updated if flag is dropped or carried

    // Add other objective-specific status fields as needed
    // float capture_progress;          // For capture points that take time
    // bool is_bomb_planted;           // For CS bomb targets

} ObjectivePointStatus_t;


typedef struct {
    int team_id;                        // e.g., 0, 1, 2, 3 (or 1, 2 for 2-team games)
    bool is_active_team;                // True if this team exists in the current mod/map
    int num_alive_players;
    int num_total_players;              // Alive + Dead but still in team

    // Mod-specific class counts (e.g., for TFC, CS roles if distinguishable)
    int class_counts[MAX_CLASSES_PER_MOD]; // Index by a mod-specific class enum
    char class_names[MAX_CLASSES_PER_MOD][32]; // Names for these classes for debugging/NN input mapping

    float aggregate_health_ratio;       // Sum of current health / sum of max health (0.0-1.0)
    float aggregate_armor_ratio;        // Similar for armor

    // Mod-specific resources
    int team_resource_A;                // e.g., CS: total team money (sum, avg, or discrete categories)
    int team_resource_B;                // e.g., TFC: number of active dispensers, total metal stored/generated
                                        // Could also be # of key powerups controlled
    // Tech level / Upgrades if the mod supports it
    // int team_tech_level;

} TeamTacticalInfo_t;


typedef struct {
    char current_map_name[32];
    float game_time_elapsed_total;      // gpGlobals->time
    float round_time_elapsed;           // Time since current round started
    float round_time_remaining;         // If applicable
    GamePhase_e current_game_phase;

    int team_scores[MAX_TEAMS];

    ObjectivePointStatus_t objective_points[MAX_OBJECTIVES];
    int num_valid_objectives;           // How many entries in objective_points are used

    TeamTacticalInfo_t team_info[MAX_TEAMS];
    int num_active_teams;               // How many entries in team_info are for active teams

    // Potentially, a simplified representation of the overall map control
    // float map_control_ ನಮ್ಮ_team_vs_enemy_team_ratio; // e.g. based on territory, #objectives controlled

} GlobalTacticalState_t;


// Function Prototypes for Tactical AI System
void TacticalAI_LevelInit();
void TacticalAI_UpdatePeriodicState();
void TacticalAI_UpdateSummarizedObjectiveStates(int num_objectives_to_summarize);
const GlobalTacticalState_t& GetGlobalTacticalState();

// Event Handlers to be called from bot_client.cpp or other game event hooks
void TacticalAI_OnScoreChanged(int team_id, int score_delta, int new_total_score);
void TacticalAI_OnRoundPhaseChanged(GamePhase_e new_phase, float time_arg);
void TacticalAI_OnObjectiveStateMsg(int objective_unique_id, ObjectiveProperty_e prop, int new_value_team, Vector new_pos_val);


#endif // BOT_TACTICAL_AI_H
