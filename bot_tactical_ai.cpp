#include "bot_tactical_ai.h"
#include "waypoint.h" // For WAYPOINT, num_waypoints, W_FL_FLAG etc.
#include "bot.h"      // For bot_t, bots array, UTIL_GetTeam, UTIL_GetClass, IsAlive, mod_id
// #include "engine.h"   // Potentially for access to game rule entities or specific mod globals if needed
                    // (may not be needed directly if all info comes via bots/clients/waypoints)
#include <string.h>  // For strncpy, memset
#include <extdll.h> // For gpGlobals, INDEXENT, FNullEnt, FL_OBSERVER
#include <dllapi.h> // For  UTIL_LogPrintf (if needed for debugging, not used yet)


// Global instance of the tactical state
GlobalTacticalState_t g_tactical_state;

// External declaration for globals from other files
extern int mod_id; // From dll.cpp or similar
extern bot_t bots[32]; // From bot.cpp
extern WAYPOINT waypoints[MAX_WAYPOINTS]; // from waypoint.cpp
extern int num_waypoints; // from waypoint.cpp
extern globalvars_t  *gpGlobals; // From engine


// Accessor function
const GlobalTacticalState_t& GetGlobalTacticalState() {
    return g_tactical_state;
}

// Helper to convert waypoint flags to ObjectiveType_e
ObjectiveType_e GetObjectiveTypeFromWaypointFlags(int waypoint_flags) {
    if (waypoint_flags & W_FL_FLAG) return OBJ_TYPE_FLAG; // TFC, CTF-like OpFor
    if (waypoint_flags & W_FL_FLAG_GOAL) return OBJ_TYPE_CAPTURE_POINT; // TFC flag capture zone
    if (waypoint_flags & W_FL_FLF_CAP) return OBJ_TYPE_CAPTURE_POINT; // FrontLineForce capture point
    // W_FL_SENTRYGUN, W_FL_DISPENSER, W_FL_HEALTH, W_FL_ARMOR, W_FL_AMMO could be STRATEGIC_LOCATION
    // or OBJ_TYPE_RESOURCE_NODE if we want to track them with more detail.
    // For now, keeping it simple. OBJ_TYPE_STRATEGIC_LOCATION can be a generic fallback.
    if (waypoint_flags & (W_FL_SENTRYGUN | W_FL_DISPENSER)) return OBJ_TYPE_STRATEGIC_LOCATION;

    // CS specific (Note: CS waypoints might not typically have these flags, CS objectives are often by entity name/type)
    // This part would need integration with CS-specific entity detection if used for CS objectives.
    // For example, if a waypoint is near a "func_bomb_target", it's a bomb target.
    // if (waypoint_flags & W_FL_CS_BOMB_TARGET) return OBJ_TYPE_BOMB_TARGET;
    // if (waypoint_flags & W_FL_CS_RESCUE_ZONE) return OBJ_TYPE_RESCUE_ZONE;

    return OBJ_TYPE_NONE;
}


void TacticalAI_LevelInit() {
    memset(&g_tactical_state, 0, sizeof(GlobalTacticalState_t));

    if (gpGlobals) { // Ensure gpGlobals is valid
        strncpy(g_tactical_state.current_map_name, STRING(gpGlobals->mapname), sizeof(g_tactical_state.current_map_name) - 1);
        g_tactical_state.current_map_name[sizeof(g_tactical_state.current_map_name) - 1] = '\0';
        g_tactical_state.game_time_elapsed_total = gpGlobals->time;
    } else {
        strncpy(g_tactical_state.current_map_name, "unknown", sizeof(g_tactical_state.current_map_name) -1);
        g_tactical_state.current_map_name[sizeof(g_tactical_state.current_map_name) - 1] = '\0';
        // game_time_elapsed_total will remain 0
    }

    g_tactical_state.current_game_phase = GAME_PHASE_UNKNOWN; // Will be updated by events/logic
    g_tactical_state.num_valid_objectives = 0;
    g_tactical_state.num_active_teams = 0; // Will be determined by iterating players or from game rules

    // Initialize objective points from waypoints
    for (int i = 0; i < num_waypoints && g_tactical_state.num_valid_objectives < MAX_OBJECTIVES; ++i) {
        ObjectiveType_e obj_type = GetObjectiveTypeFromWaypointFlags(waypoints[i].flags);
        if (obj_type != OBJ_TYPE_NONE) {
            ObjectivePointStatus_t* ops = &g_tactical_state.objective_points[g_tactical_state.num_valid_objectives];
            ops->id = i; // Waypoint index as ID
            ops->type = obj_type;
            ops->position = waypoints[i].origin;
            ops->owner_team = -1; // Neutral by default, will be updated by game events or periodic checks
            ops->is_contested = false;

            if (obj_type == OBJ_TYPE_FLAG) {
                ops->is_flag_at_home_base = true;
                ops->flag_carrier_team = -1; // Not carried
                ops->flag_current_position = waypoints[i].origin; // Assume flag starts at its waypoint
            }
            // Initialize other fields like capture_progress, is_bomb_planted to defaults
            g_tactical_state.num_valid_objectives++;
        }
    }

    // Initialize team info structures (basic setup)
    for(int i=0; i < MAX_TEAMS; ++i) {
        g_tactical_state.team_info[i].team_id = i;
        g_tactical_state.team_info[i].is_active_team = false; // Mark inactive until players are found
        memset(g_tactical_state.team_info[i].class_counts, 0, sizeof(g_tactical_state.team_info[i].class_counts));
        // Initialize class names (example for TFC, should be mod-dependent)
        if (mod_id == TFC_DLL) {
             // This is just an example, real mapping needed
            char tfc_class_names[MAX_CLASSES_PER_MOD][32] = {"CIV", "SCO", "SNP", "SOL", "DEM", "MED", "HWG", "PYR", "SPY", "ENG", "", ""};
            for(int j=0; j < MAX_CLASSES_PER_MOD; ++j) strncpy(g_tactical_state.team_info[i].class_names[j], tfc_class_names[j], 31);
        } else {
            for(int j=0; j < MAX_CLASSES_PER_MOD; ++j) sprintf(g_tactical_state.team_info[i].class_names[j], "Class%d", j);
        }
        g_tactical_state.team_info[i].aggregate_health_ratio = 0.0f;
        g_tactical_state.team_info[i].aggregate_armor_ratio = 0.0f;
        g_tactical_state.team_info[i].team_resource_A = 0;
        g_tactical_state.team_info[i].team_resource_B = 0;
    }

    // TODO: Determine num_active_teams based on mod_id and game rules if possible,
    // or wait for first TacticalAI_UpdatePeriodicState
}

// --- Message Parsing Handler Function Stubs ---

void TacticalAI_HandleTeamScoreUpdate(int team_id, int new_score) {
    if (team_id >= 0 && team_id < MAX_TEAMS) {
        g_tactical_state.team_scores[team_id] = new_score;
    }
}

void TacticalAI_HandleObjectiveCaptured(int objective_id_or_waypoint_index, int capturing_team_id) {
    for (int i = 0; i < g_tactical_state.num_valid_objectives; ++i) {
        if (g_tactical_state.objective_points[i].id == objective_id_or_waypoint_index) {
            g_tactical_state.objective_points[i].owner_team = capturing_team_id;
            g_tactical_state.objective_points[i].is_contested = false;
            return;
        }
    }
}

typedef enum { FLAG_EVENT_TAKEN, FLAG_EVENT_DROPPED, FLAG_EVENT_RETURNED, FLAG_EVENT_CAPTURED } FlagEventType_e;
void TacticalAI_HandleFlagEvent(int objective_id_or_waypoint_index, FlagEventType_e event_type, int by_team_id, int for_team_id_if_captured, Vector event_pos) {
    for (int i = 0; i < g_tactical_state.num_valid_objectives; ++i) {
        ObjectivePointStatus_t* ops = &g_tactical_state.objective_points[i];
        if (ops->id == objective_id_or_waypoint_index && ops->type == OBJ_TYPE_FLAG) {
            switch(event_type) {
                case FLAG_EVENT_TAKEN:
                    ops->is_flag_at_home_base = false;
                    ops->flag_carrier_team = by_team_id;
                    ops->flag_current_position = event_pos;
                    break;
                case FLAG_EVENT_DROPPED:
                    ops->flag_carrier_team = -1;
                    ops->flag_current_position = event_pos;
                    break;
                case FLAG_EVENT_RETURNED:
                    ops->is_flag_at_home_base = true;
                    ops->flag_carrier_team = -1;
                    ops->flag_current_position = ops->position;
                    ops->owner_team = -1; // Or original owning team if flags have owners
                    break;
                case FLAG_EVENT_CAPTURED:
                    ops->is_flag_at_home_base = true;
                    ops->flag_carrier_team = -1;
                    ops->flag_current_position = ops->position;
                    break;
            }
            return;
        }
    }
}

void TacticalAI_HandlePlayerSpawn(edict_t *pEntity) {
    // Let periodic update handle aggregates.
}

void TacticalAI_HandlePlayerDeath(edict_t *pVictim, edict_t *pKiller) {
    // Let periodic update handle aggregates.
}

void TacticalAI_HandleCSMoneyUpdate(int bot_index, int new_money) {
    if (mod_id == CSTRIKE_DLL) { // Ensure this is CS specific
        if (bot_index >=0 && bot_index < 32) {
            if(bots[bot_index].is_used) {
                 bots[bot_index].bot_money = new_money; // Primary store
            }
        }
    }
}

void TacticalAI_HandleRoundStateChange(GamePhase_e new_phase, float time_value) {
    g_tactical_state.current_game_phase = new_phase;
    if (new_phase == GAME_PHASE_ROUND_ACTIVE) {
        g_tactical_state.round_time_elapsed = 0;
        g_tactical_state.round_time_remaining = time_value;
    } else if (new_phase == GAME_PHASE_SETUP) {
        g_tactical_state.round_time_elapsed = 0;
        g_tactical_state.round_time_remaining = time_value; // Buy time for CS
    }
}

// --- Periodic State Update Function ---
void TacticalAI_UpdatePeriodicState() {
    if (!gpGlobals) return;
    g_tactical_state.game_time_elapsed_total = gpGlobals->time;

    // This needs a proper start time reference for round_time_elapsed.
    // For now, we assume TacticalAI_HandleRoundStateChange sets it correctly.
    // If round_time_remaining is set, it can be decremented.
    // static float last_update_time = gpGlobals->time;
    // float time_delta = gpGlobals->time - last_update_time;
    // if (g_tactical_state.current_game_phase == GAME_PHASE_ROUND_ACTIVE || g_tactical_state.current_game_phase == GAME_PHASE_SETUP) {
    //    g_tactical_state.round_time_elapsed += time_delta;
    //    if (g_tactical_state.round_time_remaining > 0) {
    //        g_tactical_state.round_time_remaining -= time_delta;
    //        if (g_tactical_state.round_time_remaining < 0) g_tactical_state.round_time_remaining = 0;
    //    }
    // }
    // last_update_time = gpGlobals->time;


    for (int i = 0; i < MAX_TEAMS; ++i) {
        g_tactical_state.team_info[i].num_alive_players = 0;
        g_tactical_state.team_info[i].num_total_players = 0;
        memset(g_tactical_state.team_info[i].class_counts, 0, sizeof(g_tactical_state.team_info[i].class_counts));
        g_tactical_state.team_info[i].aggregate_health_ratio = 0;
        g_tactical_state.team_info[i].aggregate_armor_ratio = 0;
        g_tactical_state.team_info[i].team_resource_A = 0;
        g_tactical_state.team_info[i].is_active_team = false;
    }
    g_tactical_state.num_active_teams = 0;

    float total_health[MAX_TEAMS] = {0};
    float total_max_health[MAX_TEAMS] = {0};
    float total_armor[MAX_TEAMS] = {0};
    float total_max_armor[MAX_TEAMS] = {0};

    for (int i = 1; i <= gpGlobals->maxClients; ++i) {
        edict_t *pPlayer = INDEXENT(i);
        if (pPlayer && !pPlayer->free && !(pPlayer->v.flags & FL_OBSERVER)) {
            int team_id = UTIL_GetTeam(pPlayer);
            if (team_id >= 0 && team_id < MAX_TEAMS) { // Ensure valid team_id (0-based from UTIL_GetTeam assumed)
                TeamTacticalInfo_t* tinfo = &g_tactical_state.team_info[team_id];
                if (!tinfo->is_active_team) { // First player found for this team
                    tinfo->is_active_team = true;
                }
                tinfo->num_total_players++;

                if (IsAlive(pPlayer)) {
                    tinfo->num_alive_players++;

                    int player_class_idx = 0;
                    if (mod_id == TFC_DLL) {
                        player_class_idx = pPlayer->v.playerclass; // Already 0-9 for TFC
                    } else if (mod_id == CSTRIKE_DLL) {
                        // CS class is more about role/equipment. Placeholder.
                        // Could assign a generic "OPERATIVE" class index.
                        player_class_idx = 0; // Example: default "class"
                    }
                    // Add other mod_id checks for class determination

                    if(player_class_idx >= 0 && player_class_idx < MAX_CLASSES_PER_MOD) {
                        tinfo->class_counts[player_class_idx]++;
                    }

                    total_health[team_id] += pPlayer->v.health;
                    total_max_health[team_id] += pPlayer->v.max_health > 0 ? pPlayer->v.max_health : 100;
                    total_armor[team_id] += pPlayer->v.armorvalue;
                    total_max_armor[team_id] += 100;

                    if (mod_id == CSTRIKE_DLL) {
                        bot_t* bot = UTIL_GetBotPointer(pPlayer);
                        if (bot) {
                            tinfo->team_resource_A += bot->bot_money;
                        }
                        // else if human player, need CVAR or message to get their money.
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_TEAMS; ++i) {
        TeamTacticalInfo_t* tinfo = &g_tactical_state.team_info[i];
        if (tinfo->is_active_team) {
            g_tactical_state.num_active_teams++; // Count active teams
            if (total_max_health[i] > 0) tinfo->aggregate_health_ratio = total_health[i] / total_max_health[i];
            else tinfo->aggregate_health_ratio = 0.0f;
            if (total_max_armor[i] > 0) tinfo->aggregate_armor_ratio = total_armor[i] / total_max_armor[i];
            else tinfo->aggregate_armor_ratio = 0.0f;
        }
    }
    // TODO: Update dynamic objective info (flag positions, capture point ownership from game entities)
}
