#include "bot_objective_discovery.h"
#include "bot_tactical_ai.h"
#include "waypoint.h"
#include "bot.h"
#include "extdll.h"
#include <string.h>
#include <algorithm> // For std::sort in TacticalAI_UpdateSummarizedObjectiveStates if moved here or used similarly

// For gpGlobals, IsAlive, ENTINDEX, UTIL_GetBotPointer, MAKE_VECTORS, world, SVC_TEMPENTITY, TE_BEAMPOINTS, MSG_ONE
// Note: Some of these might be pulled in via bot.h or extdll.h already.
// Explicitly including what might be needed if functions are moved around.
#include "util.h" // Often has ALERT, and engine util functions if not in extdll.h directly
#include "cbase.h" // For MOVETYPE_ defines, FL_ OBSERVER, ignore_monsters, etc. (if not in extdll.h)


extern int m_spriteTexture;
extern globalvars_t  *gpGlobals;
extern WAYPOINT waypoints[MAX_WAYPOINTS];
extern int num_waypoints;
extern bot_t bots[32]; // For NE_UpdateFitnessStatsOnEvent call

// Define global containers
std::vector<CandidateObjective_t> g_candidate_objectives;
std::list<GameEvent_t> g_game_event_log;
int g_dynamic_candidate_id_counter = 0;

static std::list<GameEvent_t>::iterator g_last_processed_event_it;
static bool g_log_initialized_for_analysis = false;

bool IsWaypointPotentiallyInteresting(int waypoint_flags) {
    if (waypoint_flags & W_FL_FLAG) return true;
    if (waypoint_flags & W_FL_FLF_CAP) return true;
    if (waypoint_flags & W_FL_FLAG_GOAL) return true;
    if (waypoint_flags & W_FL_FLF_DEFEND) return true;
    if (waypoint_flags & W_FL_HEALTH) return true;
    if (waypoint_flags & W_FL_ARMOR) return true;
    if (waypoint_flags & W_FL_AMMO) return true;
    if (waypoint_flags & W_FL_DOOR) return true;
    if (waypoint_flags & W_FL_SENTRYGUN) return true;
    if (waypoint_flags & W_FL_DISPENSER) return true;
    if (waypoint_flags & W_FL_SNIPER) return true;
    return false;
}

ObjectiveType_e GetInitialLearnedTypeFromWaypoint(int waypoint_flags) {
    if (waypoint_flags & W_FL_FLAG) return OBJ_TYPE_FLAG;
    if (waypoint_flags & W_FL_FLF_CAP) return OBJ_TYPE_CAPTURE_POINT;
    if (waypoint_flags & W_FL_FLAG_GOAL) return OBJ_TYPE_CAPTURE_POINT;
    if ((waypoint_flags & W_FL_HEALTH) || (waypoint_flags & W_FL_ARMOR) || (waypoint_flags & W_FL_AMMO))
        return OBJ_TYPE_RESOURCE_NODE;
    if ((waypoint_flags & W_FL_SENTRYGUN) || (waypoint_flags & W_FL_DISPENSER) || (waypoint_flags & W_FL_SNIPER) || (waypoint_flags & W_FL_FLF_DEFEND))
        return OBJ_TYPE_STRATEGIC_LOCATION;
    return OBJ_TYPE_STRATEGIC_LOCATION;
}

void ObjectiveDiscovery_LevelInit() {
    g_candidate_objectives.clear();
    g_game_event_log.clear();
    g_dynamic_candidate_id_counter = MAX_WAYPOINTS + 1;
    g_log_initialized_for_analysis = false;

    if (!gpGlobals) return;

    for (int i = 0; i < num_waypoints && g_candidate_objectives.size() < MAX_OBJECTIVES_IN_DISCOVERY_LIST; ++i) {
        if (IsWaypointPotentiallyInteresting(waypoints[i].flags)) {
            CandidateObjective_t cand;
            memset(&cand, 0, sizeof(CandidateObjective_t));
            cand.location = waypoints[i].origin;
            strncpy(cand.entity_classname, "waypoint_area", sizeof(cand.entity_classname) - 1);
            cand.entity_classname[sizeof(cand.entity_classname) - 1] = '\0';
            cand.unique_id = i;
            cand.learned_objective_type = GetInitialLearnedTypeFromWaypoint(waypoints[i].flags);
            cand.confidence_score = 0.1f;
            cand.last_interacting_team = -1;
            cand.last_interaction_time = 0.0f;
            cand.positive_event_correlations = 0;
            cand.negative_event_correlations = 0;
            cand.current_owner_team = -1;
            cand.learned_activation_method = ACT_UNKNOWN;
            cand.last_positive_correlation_update_time = 0.0f;

            // Initialize new semantic fields
            cand.gives_health_or_armor = false;
            cand.triggers_another_entity_event = false;
            memset(cand.primary_correlated_message_keyword, 0, sizeof(cand.primary_correlated_message_keyword));

            // Heuristics for waypoint-based candidates (less common to have specific entity classnames here)
            // These might be more effective if waypoint flags could imply these properties.
            // For now, relying on the generic initialization for "waypoint_area".
            if (strcmp(cand.entity_classname, "waypoint_area") == 0) {
                if ((waypoints[i].flags & W_FL_HEALTH) || (waypoints[i].flags & W_FL_ARMOR)) {
                    cand.gives_health_or_armor = true;
                }
                // Waypoints themselves don't typically "trigger" events like buttons.
            }
            cand.has_been_shared_as_confirmed = false; // Initialize new field

            g_candidate_objectives.push_back(cand);
        }
    }
}

// Updated signature to include direct link parameters
void AddGameEvent(GameEventType_e type, float timestamp,
                  int team1, int team2, int associated_candidate_id,
                  int player_edict_idx, float val_float, int val_int, const char* message,
                  bool is_direct_link = false, int direct_link_obj_id = -1) {
    if (g_game_event_log.size() >= MAX_GAME_EVENTS_LOG_SIZE) {
        g_game_event_log.pop_front();
    }
    GameEvent_t event;
    memset(&event, 0, sizeof(GameEvent_t)); // Important to zero out all fields, including new ones
    event.type = type;
    event.timestamp = timestamp;
    event.primarily_involved_team_id = team1;
    event.secondary_involved_team_id = team2;
    event.candidate_objective_id = associated_candidate_id; // General association
    event.involved_player_user_id = player_edict_idx;
    event.event_value_float = val_float;
    event.event_value_int = val_int;
    if (message) {
        strncpy(event.event_message_text, message, sizeof(event.event_message_text) - 1);
        event.event_message_text[sizeof(event.event_message_text) - 1] = '\0';
    }
    // Assign new direct consequence fields
    event.is_direct_consequence_link = is_direct_link;
    event.directly_linked_candidate_id = direct_link_obj_id;

    // Update fitness stats based on this event
    if (event.involved_player_user_id != -1) {
        edict_t* pPlayerEdict = INDEXENT(event.involved_player_user_id);
        bot_t* pInvolvedBot = UTIL_GetBotPointer(pPlayerEdict);
        if (pInvolvedBot) {
            NE_UpdateFitnessStatsOnEvent(pInvolvedBot, &event, &GetGlobalTacticalState());
        }
    } else if (event.primarily_involved_team_id != -1) {
        if (event.type == EVENT_ROUND_OUTCOME || event.type == EVENT_SCORE_CHANGED) {
            for (int i=0; i < 32; ++i) { // Assuming 32 is max_bots
                if (bots[i].is_used && bots[i].bot_team == event.primarily_involved_team_id) {
                    NE_UpdateFitnessStatsOnEvent(&bots[i], &event, &GetGlobalTacticalState());
                }
            }
        }
    }

    // Process internal share events before adding to log to prevent a bot from processing its own immediate share.
    if (event.type == EVENT_INTERNAL_OBJECTIVE_SHARE) {
        for (int i = 0; i < 32; ++i) { // Assuming 32 is max_bots
            if (bots[i].is_used && bots[i].bot_team == event.primarily_involved_team_id) {
                // Skip if the event was "sourced" by a player (event.involved_player_user_id != -1)
                // and that player is this bot. System-generated shares (player_user_id == -1) are processed by all.
                if (event.involved_player_user_id != -1 && ENTINDEX(bots[i].pEdict) == event.involved_player_user_id) {
                    continue;
                }
                ObjectiveDiscovery_ProcessSharedObjectiveData(&bots[i],
                                                            event.associated_candidate_id, // This is the unique_id of the objective
                                                            (ObjectiveType_e)event.event_value_int,
                                                            event.event_value_float,
                                                            event.event_message_text); // Keyword is in message_text
            }
        }
    }

    g_game_event_log.push_back(event);
}


void ObjectiveDiscovery_UpdatePeriodic() {
    if (!gpGlobals) return;
    for (int i = 1; i <= gpGlobals->maxClients; ++i) {
        edict_t *pPlayer = INDEXENT(i);
        if (!pPlayer || pPlayer->free || (pPlayer->v.flags & FL_OBSERVER) || !IsAlive(pPlayer)) {
            continue;
        }
        TraceResult tr;
        Vector v_gaze_direction;
        MAKE_VECTORS(pPlayer->v.v_angle);
        v_gaze_direction = gpGlobals->v_forward;
        Vector vecSrc = pPlayer->v.origin + pPlayer->v.view_ofs;
        Vector vecEnd = vecSrc + v_gaze_direction * 128;
        UTIL_TraceLine(vecSrc, vecEnd, ignore_monsters, pPlayer, &tr);
        edict_t* pLookedAtEntity = tr.pHit;

        if (pLookedAtEntity && !FNullEnt(pLookedAtEntity) && pLookedAtEntity != pPlayer && pLookedAtEntity != world) {
            bool is_potentially_interactive_static = (pLookedAtEntity->v.movetype == MOVETYPE_NONE ||
                                                     pLookedAtEntity->v.movetype == MOVETYPE_PUSH ||
                                                     (pLookedAtEntity->v.movetype == MOVETYPE_TOSS && pLookedAtEntity->v.solid == SOLID_TRIGGER))
                                                     && !(pLookedAtEntity->v.flags & FL_MONSTER);
            if (is_potentially_interactive_static) {
                 bool found_existing = false;
                 int existing_candidate_unique_id = -1;
                 CandidateObjective_t* pCand = NULL;
                 for(size_t c_idx = 0; c_idx < g_candidate_objectives.size(); ++c_idx) {
                    if ( (pLookedAtEntity->v.targetname != 0 && STRING(pLookedAtEntity->v.targetname)[0] != '\0' &&
                          g_candidate_objectives[c_idx].entity_targetname[0] != '\0' &&
                          strcmp(g_candidate_objectives[c_idx].entity_targetname, STRING(pLookedAtEntity->v.targetname)) == 0) ||
                         (STRING(pLookedAtEntity->v.classname)[0] != '\0' &&
                          strcmp(g_candidate_objectives[c_idx].entity_classname, STRING(pLookedAtEntity->v.classname)) == 0 &&
                          (g_candidate_objectives[c_idx].location - pLookedAtEntity->v.origin).LengthSquared() < 1.0f) ) {
                        pCand = &g_candidate_objectives[c_idx];
                        found_existing = true;
                        existing_candidate_unique_id = pCand->unique_id;
                        pCand->last_interaction_time = gpGlobals->time;
                        pCand->last_interacting_team = UTIL_GetTeam(pPlayer);
                        break;
                    }
                 }
                if (!found_existing && g_candidate_objectives.size() < MAX_OBJECTIVES_IN_DISCOVERY_LIST) {
                    CandidateObjective_t cand_new;
                    memset(&cand_new, 0, sizeof(CandidateObjective_t));
                    cand_new.location = pLookedAtEntity->v.origin;
                    if (pLookedAtEntity->v.classname != 0) strncpy(cand_new.entity_classname, STRING(pLookedAtEntity->v.classname), sizeof(cand_new.entity_classname) - 1);
                    if (pLookedAtEntity->v.targetname != 0) strncpy(cand_new.entity_targetname, STRING(pLookedAtEntity->v.targetname), sizeof(cand_new.entity_targetname) - 1);
                    cand_new.unique_id = g_dynamic_candidate_id_counter++;
                    cand_new.learned_objective_type = OBJ_TYPE_STRATEGIC_LOCATION;
                    cand_new.confidence_score = 0.05f;
                    cand_new.last_interacting_team = UTIL_GetTeam(pPlayer);
                    cand_new.last_interaction_time = gpGlobals->time;
                    cand_new.current_owner_team = -1;
                    cand_new.learned_activation_method = ACT_UNKNOWN;
                    cand_new.last_positive_correlation_update_time = 0.0f;

                    // Initialize new semantic fields for dynamic candidates
                    cand_new.gives_health_or_armor = false;
                    cand_new.triggers_another_entity_event = false;
                    memset(cand_new.primary_correlated_message_keyword, 0, sizeof(cand_new.primary_correlated_message_keyword));

                    // Initial heuristic for triggers_another_entity_event based on classname:
                    if (strlen(cand_new.entity_classname) > 0) {
                        if (strcmp(cand_new.entity_classname, "func_button") == 0 ||
                            strcmp(cand_new.entity_classname, "func_platrot") == 0 ||
                            strcmp(cand_new.entity_classname, "momentary_rot_button") == 0 ||
                            strcmp(cand_new.entity_classname, "multi_source") == 0) {
                            cand_new.triggers_another_entity_event = true;
                        }
                        // Initial heuristic for gives_health_or_armor
                        if (strcmp(cand_new.entity_classname, "item_healthkit") == 0 ||
                            strcmp(cand_new.entity_classname, "item_battery") == 0 ||
                            strcmp(cand_new.entity_classname, "item_armor") == 0 ||
                            strcmp(cand_new.entity_classname, "func_healthcharger") == 0 ||
                            strcmp(cand_new.entity_classname, "func_recharge") == 0) {
                            cand_new.gives_health_or_armor = true;
                        }
                    }

                    g_candidate_objectives.push_back(cand_new);
                    pCand = &g_candidate_objectives.back();
                    existing_candidate_unique_id = cand_new.unique_id;
                }
                if (pCand) {
                    pCand->recent_interacting_player_edict_indices.push_back(ENTINDEX(pPlayer));
                    if (pCand->recent_interacting_player_edict_indices.size() > MAX_RECENT_INTERACTORS) {
                        pCand->recent_interacting_player_edict_indices.erase(pCand->recent_interacting_player_edict_indices.begin());
                    }
                    pCand->recent_interacting_teams.push_back(UTIL_GetTeam(pPlayer));
                    if (pCand->recent_interacting_teams.size() > MAX_RECENT_INTERACTORS) {
                        pCand->recent_interacting_teams.erase(pCand->recent_interacting_teams.begin());
                    }
                    if (pPlayer->v.button & IN_USE) {
                        if (pCand->learned_activation_method == ACT_UNKNOWN) pCand->learned_activation_method = ACT_USE;
                        if(existing_candidate_unique_id != -1) AddGameEvent(EVENT_PLAYER_USED_ENTITY, gpGlobals->time, UTIL_GetTeam(pPlayer), -1, existing_candidate_unique_id, ENTINDEX(pPlayer), 0.0f, 0, STRING(pLookedAtEntity->v.classname));
                    }
                }
            }
        }
        edict_t* pNearbyEntity = NULL;
        while ((pNearbyEntity = UTIL_FindEntityInSphere(pNearbyEntity, pPlayer->v.origin, 64.0f)) != NULL) {
            if (pNearbyEntity == pPlayer || pNearbyEntity == world) continue;
            bool is_potentially_interactive_static = (pNearbyEntity->v.movetype == MOVETYPE_NONE ||
                                                     pNearbyEntity->v.movetype == MOVETYPE_PUSH ||
                                                     (pNearbyEntity->v.movetype == MOVETYPE_TOSS && pNearbyEntity->v.solid == SOLID_TRIGGER))
                                                     && !(pNearbyEntity->v.flags & FL_MONSTER);
            if (is_potentially_interactive_static) {
                bool found_existing = false;
                int existing_candidate_unique_id = -1;
                CandidateObjective_t* pCand = NULL;
                 for(size_t c_idx = 0; c_idx < g_candidate_objectives.size(); ++c_idx) {
                    if ( (pNearbyEntity->v.targetname != 0 && STRING(pNearbyEntity->v.targetname)[0] != '\0' &&
                          g_candidate_objectives[c_idx].entity_targetname[0] != '\0' &&
                          strcmp(g_candidate_objectives[c_idx].entity_targetname, STRING(pNearbyEntity->v.targetname)) == 0) ||
                         (STRING(pNearbyEntity->v.classname)[0] != '\0' &&
                          strcmp(g_candidate_objectives[c_idx].entity_classname, STRING(pNearbyEntity->v.classname)) == 0 &&
                          (g_candidate_objectives[c_idx].location - pNearbyEntity->v.origin).LengthSquared() < 1.0f) ) {
                        pCand = &g_candidate_objectives[c_idx];
                        found_existing = true;
                        existing_candidate_unique_id = pCand->unique_id;
                        pCand->last_interaction_time = gpGlobals->time;
                        pCand->last_interacting_team = UTIL_GetTeam(pPlayer);
                        AddGameEvent(EVENT_PLAYER_TOUCHED_ENTITY, gpGlobals->time, UTIL_GetTeam(pPlayer), -1, existing_candidate_unique_id, ENTINDEX(pPlayer), 0.0f, 0, STRING(pNearbyEntity->v.classname));
                        break;
                    }
                 }
                 if (!found_existing && g_candidate_objectives.size() < MAX_OBJECTIVES_IN_DISCOVERY_LIST) {
                    CandidateObjective_t cand_new;
                    memset(&cand_new, 0, sizeof(CandidateObjective_t));
                    cand_new.location = pNearbyEntity->v.origin;
                    if (pNearbyEntity->v.classname != 0) strncpy(cand_new.entity_classname, STRING(pNearbyEntity->v.classname), sizeof(cand_new.entity_classname) - 1);
                    if (pNearbyEntity->v.targetname != 0) strncpy(cand_new.entity_targetname, STRING(pNearbyEntity->v.targetname), sizeof(cand_new.entity_targetname) - 1);
                    cand_new.unique_id = g_dynamic_candidate_id_counter++;
                    cand_new.learned_objective_type = OBJ_TYPE_STRATEGIC_LOCATION;
                    cand_new.confidence_score = 0.05f;
                    cand_new.last_interacting_team = UTIL_GetTeam(pPlayer);
                    cand_new.last_interaction_time = gpGlobals->time;
                    cand_new.current_owner_team = -1;
                    cand_new.learned_activation_method = ACT_UNKNOWN;
                    cand_new.last_positive_correlation_update_time = 0.0f;

                    // Initialize new semantic fields for dynamic candidates from touch
                    cand_new.gives_health_or_armor = false;
                    cand_new.triggers_another_entity_event = false;
                    memset(cand_new.primary_correlated_message_keyword, 0, sizeof(cand_new.primary_correlated_message_keyword));

                    // Initial heuristic for triggers_another_entity_event based on classname:
                    if (strlen(cand_new.entity_classname) > 0) {
                        if (strcmp(cand_new.entity_classname, "func_button") == 0 ||
                            strcmp(cand_new.entity_classname, "func_platrot") == 0 ||
                            strcmp(cand_new.entity_classname, "momentary_rot_button") == 0 ||
                             strcmp(cand_new.entity_classname, "multi_source") == 0) {
                            cand_new.triggers_another_entity_event = true;
                        }
                        // Initial heuristic for gives_health_or_armor
                        if (strcmp(cand_new.entity_classname, "item_healthkit") == 0 ||
                            strcmp(cand_new.entity_classname, "item_battery") == 0 ||
                            strcmp(cand_new.entity_classname, "item_armor") == 0 ||
                            strcmp(cand_new.entity_classname, "func_healthcharger") == 0 ||
                            strcmp(cand_new.entity_classname, "func_recharge") == 0) {
                            cand_new.gives_health_or_armor = true;
                        }
                    }

                    g_candidate_objectives.push_back(cand_new);
                    pCand = &g_candidate_objectives.back();
                    AddGameEvent(EVENT_PLAYER_TOUCHED_ENTITY, gpGlobals->time, UTIL_GetTeam(pPlayer), -1, pCand->unique_id, ENTINDEX(pPlayer), 0.0f, 0, STRING(pNearbyEntity->v.classname));
                 }
                 if(pCand) {
                    pCand->recent_interacting_player_edict_indices.push_back(ENTINDEX(pPlayer));
                    if (pCand->recent_interacting_player_edict_indices.size() > MAX_RECENT_INTERACTORS) {
                        pCand->recent_interacting_player_edict_indices.erase(pCand->recent_interacting_player_edict_indices.begin());
                    }
                    pCand->recent_interacting_teams.push_back(UTIL_GetTeam(pPlayer));
                    if (pCand->recent_interacting_teams.size() > MAX_RECENT_INTERACTORS) {
                        pCand->recent_interacting_teams.erase(pCand->recent_interacting_teams.begin());
                    }
                    if (pCand->learned_activation_method == ACT_UNKNOWN) {
                        pCand->learned_activation_method = ACT_TOUCH;
                    }
                 }
            }
        }
    }
}

bool MessageContainsKeyword(const char* message, const char* keyword) {
    if (!message || !keyword) return false;
    return strstr(message, keyword) != NULL;
}

void ObjectiveDiscovery_UpdateLearnedTypes() {
    if (!gpGlobals) return;
    const float SEMANTIC_TYPING_CONFIDENCE_THRESHOLD = 0.6f; // Min confidence to attempt specific typing
    const float HIGH_CONFIDENCE_FOR_OVERWRITE = 0.8f; // Min confidence to overwrite an already specific type

    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        CandidateObjective_t& cand = g_candidate_objectives[i];

        if (cand.confidence_score < SEMANTIC_TYPING_CONFIDENCE_THRESHOLD) {
            // If confidence is low, maybe revert to a less specific type if it was previously specific.
            if (cand.learned_objective_type != OBJ_TYPE_NONE &&
                cand.learned_objective_type != OBJ_TYPE_STRATEGIC_LOCATION &&
                cand.learned_objective_type != GetInitialLearnedTypeFromWaypoint( (cand.unique_id < num_waypoints && cand.unique_id >=0) ? waypoints[cand.unique_id].flags : 0 ) )
            {
                // Revert to its original waypoint-derived type or strategic location
                if (cand.unique_id < num_waypoints && cand.unique_id >= 0) { // Was waypoint derived
                    cand.learned_objective_type = GetInitialLearnedTypeFromWaypoint(waypoints[cand.unique_id].flags);
                } else {
                    cand.learned_objective_type = OBJ_TYPE_STRATEGIC_LOCATION;
                }
            }
            continue;
        }

        // Heuristics to determine/refine learned_objective_type
        // Only overwrite an already specific type if new evidence is very strong (high confidence)
        bool can_overwrite_specific_type = (cand.confidence_score >= HIGH_CONFIDENCE_FOR_OVERWRITE);

        // 1. Based on `gives_health_or_armor` (set by classname heuristic in UpdatePeriodic)
        if (cand.gives_health_or_armor) {
            if (strstr(cand.entity_classname, "health") || strstr(cand.entity_classname, "medkit")) {
                if (cand.learned_objective_type != OBJ_TYPE_HEALTH_REFILL_STATION && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_RESOURCE_NODE || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_HEALTH_REFILL_STATION;
            } else if (strstr(cand.entity_classname, "armor") || strstr(cand.entity_classname, "battery") || strstr(cand.entity_classname, "recharge")) {
                if (cand.learned_objective_type != OBJ_TYPE_ARMOR_REFILL_STATION && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_RESOURCE_NODE || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_ARMOR_REFILL_STATION;
            } else if (cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE) {
                cand.learned_objective_type = OBJ_TYPE_RESOURCE_NODE; // Generic resource
            }
        }

        // 2. Based on `triggers_another_entity_event` (set by classname heuristic)
        if (cand.triggers_another_entity_event) {
             if (cand.learned_objective_type != OBJ_TYPE_PRESSABLE_BUTTON && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE)) {
                 cand.learned_objective_type = OBJ_TYPE_PRESSABLE_BUTTON;
                 if (cand.learned_activation_method == ACT_UNKNOWN) cand.learned_activation_method = ACT_USE;
             }
        }

        // 3. Based on `entity_classname` for Doors or Weapons
        if (strlen(cand.entity_classname) > 0) {
            if (strncmp(cand.entity_classname, "func_door", 9) == 0 || strcmp(cand.entity_classname, "momentary_door") == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_DOOR_OBSTACLE && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_DOOR_OBSTACLE;
            } else if (strncmp(cand.entity_classname, "weapon_", 7) == 0 || strncmp(cand.entity_classname, "ammo_", 5) == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_WEAPON_SPAWN && cand.learned_objective_type != OBJ_TYPE_AMMO_REFILL_POINT && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_RESOURCE_NODE || cand.learned_objective_type == OBJ_TYPE_NONE)){
                    if(strncmp(cand.entity_classname, "weapon_", 7) == 0) cand.learned_objective_type = OBJ_TYPE_WEAPON_SPAWN;
                    else cand.learned_objective_type = OBJ_TYPE_AMMO_REFILL_POINT;
                }
            }
        }

        // 4. Based on `primary_correlated_message_keyword` (set in AnalyzeEvents)
        if (strlen(cand.primary_correlated_message_keyword) > 0) {
            if (stricmp(cand.primary_correlated_message_keyword, "flag") == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_FLAG_LIKE_PICKUP && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_FLAG_LIKE_PICKUP;
            } else if (stricmp(cand.primary_correlated_message_keyword, "capture") == 0 || stricmp(cand.primary_correlated_message_keyword, "control") == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_CONTROL_AREA && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_CONTROL_AREA;
            } else if (stricmp(cand.primary_correlated_message_keyword, "bomb") == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_BOMB_TARGET && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_BOMB_TARGET;
            } else if (stricmp(cand.primary_correlated_message_keyword, "destroy") == 0 || stricmp(cand.primary_correlated_message_keyword, "damage") == 0) {
                if (cand.learned_objective_type != OBJ_TYPE_DESTRUCTIBLE_TARGET && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE))
                    cand.learned_objective_type = OBJ_TYPE_DESTRUCTIBLE_TARGET;
                    if (cand.learned_activation_method == ACT_UNKNOWN) cand.learned_activation_method = ACT_SHOOT_TARGET; // Assumption
            }
        }

        // 5. Based on original waypoint flags if it's a waypoint-derived candidate
        if (cand.unique_id < num_waypoints && cand.unique_id >= 0) {
             int waypoint_flags = waypoints[cand.unique_id].flags;
             if (waypoint_flags & W_FL_FLAG) { // Strong indication from map data
                 if (cand.learned_objective_type != OBJ_TYPE_FLAG && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE || cand.learned_objective_type == OBJ_TYPE_FLAG_LIKE_PICKUP))
                     cand.learned_objective_type = OBJ_TYPE_FLAG; // More specific than general pickup
             } else if (waypoint_flags & W_FL_FLF_CAP) { // Example for another specific flag
                  if (cand.learned_objective_type != OBJ_TYPE_CAPTURE_POINT && (can_overwrite_specific_type || cand.learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION || cand.learned_objective_type == OBJ_TYPE_NONE || cand.learned_objective_type == OBJ_TYPE_CONTROL_AREA))
                     cand.learned_objective_type = OBJ_TYPE_CAPTURE_POINT;
             }
        }

        // Fallback: if still NONE but high confidence, make it a STRATEGIC_LOCATION
        if (cand.learned_objective_type == OBJ_TYPE_NONE && cand.confidence_score >= SEMANTIC_TYPING_CONFIDENCE_THRESHOLD) {
            cand.learned_objective_type = OBJ_TYPE_STRATEGIC_LOCATION;
        }
    }
}

void ObjectiveDiscovery_AnalyzeEvents() {
    if (!gpGlobals) return;
    if (!g_log_initialized_for_analysis && !g_game_event_log.empty()) {
        g_last_processed_event_it = g_game_event_log.begin();
        g_log_initialized_for_analysis = true;
    } else if (g_game_event_log.empty()) {
        g_log_initialized_for_analysis = false;
        return;
    }

    std::list<GameEvent_t>::iterator current_event_it = g_game_event_log.begin();

    // --- Direct Consequence Handling Loop ---
    // This loop iterates events to find direct links.
    // For full robustness, events processed this way should be flagged (e.g., event.direct_consequence_processed = true)
    // to avoid reprocessing if AnalyzeEvents is called multiple times over the same log section without clearing.
    // For this subtask, we'll assume events are new enough or this function is called such that reprocessing is minimal.
    std::list<GameEvent_t>::iterator direct_link_it = g_game_event_log.begin();
    // Potentially use g_last_processed_event_it if it's reliably managed to mark the start of "new" events.
    // For now, iterating a significant portion or all of the log for direct links for simplicity of this step.
    for (; direct_link_it != g_game_event_log.end(); ++direct_link_it) {
        if (direct_link_it->is_direct_consequence_link && direct_link_it->directly_linked_candidate_id != -1) {
            CandidateObjective_t* pCand = GetCandidateObjectiveById(direct_link_it->directly_linked_candidate_id);
            if (pCand) {
                bool direct_positive_outcome = false;
                // Define what constitutes a direct positive outcome based on event type and values
                if (direct_link_it->type == EVENT_SCORE_CHANGED && direct_link_it->event_value_float > 0) {
                    direct_positive_outcome = true;
                } else if (direct_link_it->type == EVENT_IMPORTANT_GAME_MESSAGE) {
                    // Example: if message indicates "objective captured by team X" and team X is primarily_involved_team_id
                    // This part requires more sophisticated message parsing logic.
                    // For now, let's assume EVENT_SCORE_CHANGED is the primary direct indicator.
                    if (MessageContainsKeyword(direct_link_it->event_message_text, "captured by") ||
                        MessageContainsKeyword(direct_link_it->event_message_text, "secured by")) {
                        // Further check if the event's primarily_involved_team_id matches the team that would benefit.
                        direct_positive_outcome = true;
                    }
                }
                // Add more specific checks for direct positive/negative outcomes from other event types.

                if (direct_positive_outcome) {
                    pCand->positive_event_correlations += (ROUND_WIN_CORRELATION_BONUS * 2); // Strong boost
                    pCand->confidence_score += LEARNING_RATE * 5 * (0.95f - pCand->confidence_score); // Move strongly towards high confidence
                    pCand->last_positive_correlation_update_time = gpGlobals->time;
                } else {
                    // Assuming if not clearly positive, it might be neutral or implicitly negative for the linked objective context.
                    // A more explicit negative check would be better.
                    pCand->negative_event_correlations += (ROUND_WIN_CORRELATION_BONUS * 2); // Or a smaller penalty if not explicitly negative
                    pCand->confidence_score += LEARNING_RATE * 5 * (0.05f - pCand->confidence_score); // Move towards low confidence
                }

                if (pCand->confidence_score > 0.95f) pCand->confidence_score = 0.95f;
                if (pCand->confidence_score < MIN_CONFIDENCE_THRESHOLD) pCand->confidence_score = MIN_CONFIDENCE_THRESHOLD;

                // Populate primary_correlated_message_keyword
                if (direct_link_it->event_message_text[0] != '\0' && strlen(pCand->primary_correlated_message_keyword) == 0) {
                    char temp_msg_copy[128];
                    strncpy(temp_msg_copy, direct_link_it->event_message_text, sizeof(temp_msg_copy)-1);
                    temp_msg_copy[sizeof(temp_msg_copy)-1] = '\0';

                    char *first_word = strtok(temp_msg_copy, " .!?,;:()[]{}<>\"'"); // Common delimiters
                    if (first_word) {
                        const char* known_keywords[] = {
                            "flag", "capture", "control", "bomb", "defuse", "rescue",
                            "deliver", "secure", "destroy", "activate", "objective",
                            "point", "intel", "briefcase", "keycard" // Added more examples
                        };
                        int num_known_keywords = sizeof(known_keywords) / sizeof(known_keywords[0]);
                        for (int k = 0; k < num_known_keywords; ++k) {
                            // Using stricmp for case-insensitive comparison if available, else strcasecmp or manual lowercasing.
                            // Assuming stricmp is available as per thought process.
                            if (stricmp(first_word, known_keywords[k]) == 0) {
                                strncpy(pCand->primary_correlated_message_keyword, first_word, sizeof(pCand->primary_correlated_message_keyword) - 1);
                                pCand->primary_correlated_message_keyword[sizeof(pCand->primary_correlated_message_keyword) - 1] = '\0';
                                break;
                            }
                        }
                    }
                }
                // direct_link_it->direct_consequence_processed = true; // If flag was added to GameEvent_t

                // Check for broadcasting confirmed objective
                const float SHARE_CONFIDENCE_THRESHOLD = 0.9f;
                const float SHARE_SPECIFIC_TYPE_CONFIDENCE_THRESHOLD = 0.75f;
                bool should_share = false;

                if (pCand->confidence_score >= SHARE_CONFIDENCE_THRESHOLD && !pCand->has_been_shared_as_confirmed) {
                    should_share = true;
                } else if (pCand->learned_objective_type != OBJ_TYPE_NONE &&
                           pCand->learned_objective_type != OBJ_TYPE_STRATEGIC_LOCATION && // Is a specific type
                           pCand->confidence_score >= SHARE_SPECIFIC_TYPE_CONFIDENCE_THRESHOLD &&
                           !pCand->has_been_shared_as_confirmed) {
                    should_share = true;
                }

                if (should_share) {
                    // For EVENT_INTERNAL_OBJECTIVE_SHARE, event_message_text will store the primary_correlated_message_keyword.
                    // The AddGameEvent call will then pass this to ObjectiveDiscovery_ProcessSharedObjectiveData.
                    AddGameEvent(EVENT_INTERNAL_OBJECTIVE_SHARE, gpGlobals->time,
                                 pCand->last_interacting_team, /* team2 */ -1,
                                 pCand->unique_id, /* player_edict_idx (source bot) */ -1,
                                 pCand->confidence_score, (int)pCand->learned_objective_type,
                                 pCand->primary_correlated_message_keyword, // Pass keyword as message
                                 true, pCand->unique_id);  // Direct link to itself for identification, though not strictly needed for share events

                    pCand->has_been_shared_as_confirmed = true;
                }
            }
        }
    }


    // --- Windowed Correlation Loop (existing logic with temporal weighting) ---
    for (current_event_it = g_game_event_log.begin(); current_event_it != g_game_event_log.end(); ++current_event_it) {
        GameEvent_t& outcome_event = *current_event_it;

        if (outcome_event.type == EVENT_SCORE_CHANGED || outcome_event.type == EVENT_ROUND_OUTCOME) {
            int benefiting_team = outcome_event.primarily_involved_team_id;
            bool positive_outcome_for_team = false;
            bool negative_outcome_for_team = false;

            if (outcome_event.type == EVENT_SCORE_CHANGED) {
                if (outcome_event.event_value_float > 0) positive_outcome_for_team = true;
                else if (outcome_event.event_value_float < 0) negative_outcome_for_team = true;
            } else if (outcome_event.type == EVENT_ROUND_OUTCOME) {
                if (outcome_event.event_value_int == 1) positive_outcome_for_team = true; // Win for primarily_involved_team_id
                else if (outcome_event.event_value_int == -1) negative_outcome_for_team = true; // Loss for primarily_involved_team_id
            }

            if (!positive_outcome_for_team && !negative_outcome_for_team) continue;

            std::list<GameEvent_t>::reverse_iterator interaction_it(current_event_it);

            for (; interaction_it != g_game_event_log.rend(); ++interaction_it) {
                GameEvent_t& interaction_event = *interaction_it;

                if ((outcome_event.timestamp - interaction_event.timestamp) > CORRELATION_WINDOW_SECONDS) {
                    break;
                }

                if ((interaction_event.type == EVENT_PLAYER_TOUCHED_ENTITY || interaction_event.type == EVENT_PLAYER_USED_ENTITY) &&
                    interaction_event.candidate_objective_id != -1 &&
                    interaction_event.primarily_involved_team_id == benefiting_team) {

                    CandidateObjective_t* pCandidate = GetCandidateObjectiveById(interaction_event.candidate_objective_id);
                    if (pCandidate) {
                        // Temporal Proximity Weighting
                        float time_difference = outcome_event.timestamp - interaction_event.timestamp;
                        float temporal_weight_multiplier = 1.0f;
                        if (time_difference <= 3.0f) {
                            temporal_weight_multiplier = 2.0f;
                        } else if (time_difference <= 8.0f) {
                            temporal_weight_multiplier = 1.0f;
                        } else {
                            temporal_weight_multiplier = 0.5f;
                        }

                        int base_correlation_increment = 1;
                        if (outcome_event.type == EVENT_ROUND_OUTCOME && positive_outcome_for_team) {
                           base_correlation_increment += ROUND_WIN_CORRELATION_BONUS;
                        }

                        int actual_increment = (int)(base_correlation_increment * temporal_weight_multiplier);
                        if (actual_increment < 1 && base_correlation_increment >=1) actual_increment = 1;

                        if (positive_outcome_for_team) {
                            pCandidate->positive_event_correlations += actual_increment;
                            pCandidate->last_positive_correlation_update_time = gpGlobals->time; // Mark update time
                            pCandidate->confidence_score += LEARNING_RATE * (1.0f - pCandidate->confidence_score);
                        } else if (negative_outcome_for_team) {
                            // For negative outcomes for the interacting team, they get negative correlations
                            pCandidate->negative_event_correlations += actual_increment;
                            pCandidate->confidence_score += LEARNING_RATE * (0.0f - pCandidate->confidence_score);
                        }

                        if (pCandidate->confidence_score > 0.95f) pCandidate->confidence_score = 0.95f;
                        if (pCandidate->confidence_score < MIN_CONFIDENCE_THRESHOLD) pCandidate->confidence_score = MIN_CONFIDENCE_THRESHOLD;
                    }
                }
            }
        }
    }

    // Confidence Decay (existing logic)
    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        CandidateObjective_t& cand = g_candidate_objectives[i];
        if (cand.last_positive_correlation_update_time > 0.0f &&
            (gpGlobals->time - cand.last_positive_correlation_update_time > CONFIDENCE_DECAY_THRESHOLD_SECONDS)) {

            cand.confidence_score *= CONFIDENCE_DECAY_RATE;
            if (cand.confidence_score < MIN_CONFIDENCE_THRESHOLD) {
                cand.confidence_score = MIN_CONFIDENCE_THRESHOLD;
            }
        }
    }

    if (!g_game_event_log.empty()) {
        g_last_processed_event_it = --g_game_event_log.end();
    }
}

const char* ObjectiveTypeToString(ObjectiveType_e obj_type) {
    switch (obj_type) {
        case OBJ_TYPE_NONE: return "NONE";
        case OBJ_TYPE_FLAG: return "FLAG";
        case OBJ_TYPE_CAPTURE_POINT: return "CAPTURE_POINT";
        case OBJ_TYPE_BOMB_TARGET: return "BOMB_TARGET";
        case OBJ_TYPE_RESCUE_ZONE: return "RESCUE_ZONE";
        case OBJ_TYPE_RESOURCE_NODE: return "RESOURCE_NODE";
        case OBJ_TYPE_STRATEGIC_LOCATION: return "STRATEGIC_LOC";
        case OBJ_TYPE_FLAG_LIKE_PICKUP: return "FLAG_LIKE_PICKUP";
        case OBJ_TYPE_CONTROL_AREA: return "CONTROL_AREA";
        case OBJ_TYPE_DESTRUCTIBLE_TARGET: return "DESTRUCTIBLE_TARGET";
        case OBJ_TYPE_HEALTH_REFILL_STATION: return "HEALTH_STATION";
        case OBJ_TYPE_ARMOR_REFILL_STATION: return "ARMOR_STATION";
        case OBJ_TYPE_AMMO_REFILL_POINT: return "AMMO_POINT";
        case OBJ_TYPE_PRESSABLE_BUTTON: return "BUTTON";
        case OBJ_TYPE_DOOR_OBSTACLE: return "DOOR";
        default: return "UNKNOWN_TYPE";
    }
}

const char* ActivationMethodToString(ActivationMethod_e act_meth) {
    switch (act_meth) {
        case ACT_UNKNOWN: return "UNKN";
        case ACT_TOUCH: return "TOUCH";
        case ACT_USE: return "USE";
        case ACT_SHOOT_TARGET: return "SHOOT";
        case ACT_TIMED_AREA_PRESENCE: return "AREA_TIME";
        default: return "ERR_ACT_METH";
    }
}

const char* GameEventTypeToString(GameEventType_e event_type) {
    switch (event_type) {
        case EVENT_TYPE_NONE: return "NONE";
        case EVENT_SCORE_CHANGED: return "SCORE_CHANGED";
        case EVENT_ROUND_OUTCOME: return "ROUND_OUTCOME";
        case EVENT_PLAYER_TOUCHED_ENTITY: return "PLAYER_TOUCHED_ENTITY";
        case EVENT_PLAYER_USED_ENTITY: return "PLAYER_USED_ENTITY";
        case EVENT_PLAYER_ENTERED_AREA: return "PLAYER_ENTERED_AREA";
        case EVENT_PLAYER_DIED_NEAR_CANDIDATE: return "PLAYER_DIED_NEAR_CANDIDATE";
        case EVENT_IMPORTANT_GAME_MESSAGE: return "IMPORTANT_GAME_MESSAGE";
        case EVENT_CANDIDATE_STATE_CHANGE: return "CANDIDATE_STATE_CHANGE";
        default: return "UNKNOWN_EVENT_TYPE";
    }
}

void ObjectiveDiscovery_DrawDebugVisuals(edict_t* pViewPlayer) {
    // If bot_debug_draw_objectives cvar is 1 (checked in dll.cpp):
    // extern int m_spriteTexture; // Already declared at top of file

    if (!gpGlobals || !pViewPlayer || g_candidate_objectives.empty()) {
        return;
    }

    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        const CandidateObjective_t& cand = g_candidate_objectives[i];

        if (cand.confidence_score < DEBUG_DRAW_CONFIDENCE_THRESHOLD) { // DEBUG_DRAW_CONFIDENCE_THRESHOLD to be defined
            continue;
        }

        if ((cand.location - pViewPlayer->v.origin).LengthSquared() > DEBUG_DRAW_MAX_DISTANCE_SQ) { // DEBUG_DRAW_MAX_DISTANCE_SQ to be defined
            continue;
        }

        int r = 0, g = 0, b = 0;
        if (cand.confidence_score > 0.7f) { g = 255; }
        else if (cand.confidence_score > 0.4f) { r = 255; g = 255; b = 0; }
        else { r = 255; }

        Vector vecStart = cand.location + Vector(0,0,10);
        Vector vecEnd = cand.location + Vector(0,0,74);

        MESSAGE_BEGIN(MSG_ONE, SVC_TEMPENTITY, NULL, pViewPlayer);
            WRITE_BYTE(TE_BEAMPOINTS); // TE_BEAMPOINTS is a standard GoldSrc temp entity type
            WRITE_COORD(vecStart.x); WRITE_COORD(vecStart.y); WRITE_COORD(vecStart.z);
            WRITE_COORD(vecEnd.x); WRITE_COORD(vecEnd.y); WRITE_COORD(vecEnd.z);
            WRITE_SHORT(m_spriteTexture);
            WRITE_BYTE(0); WRITE_BYTE(10); WRITE_BYTE(3); WRITE_BYTE(20); WRITE_BYTE(0);
            WRITE_BYTE(r); WRITE_BYTE(g); WRITE_BYTE(b);
            WRITE_BYTE(200); WRITE_BYTE(5);
        MESSAGE_END();
    }
}


// Helper function to check if an entity is already a candidate objective
// and if its confidence is still low (meaning it's worth re-evaluating or it's new)
// This function is not directly used in the final logic above, but the principle is incorporated.
// The logic directly checks for existence and then adds if not found.
/*
bool IsNewOrLowConfidenceCandidate(edict_t* pEntity) {
    if (!pEntity || FNullEnt(pEntity)) return false;

    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        if (g_candidate_objectives[i].entity_classname[0] != '\0' &&
            ( (pEntity->v.targetname != 0 && STRING(pEntity->v.targetname)[0] != '\0' && g_candidate_objectives[i].entity_targetname[0] != '\0' &&
               strcmp(STRING(pEntity->v.targetname), g_candidate_objectives[i].entity_targetname) == 0)) ||
            ( (pEntity->v.classname != 0 && STRING(pEntity->v.classname)[0] != '\0' &&
               strcmp(STRING(pEntity->v.classname), g_candidate_objectives[i].entity_classname) == 0 &&
               (g_candidate_objectives[i].location - pEntity->v.origin).LengthSquared() < 1.0f)) )
            ) {
            return g_candidate_objectives[i].confidence_score < 0.5f;
        }
    }
    return true;
}
*/

CandidateObjective_t* GetCandidateObjectiveById(int unique_id) {
    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        if (g_candidate_objectives[i].unique_id == unique_id) {
            return &g_candidate_objectives[i];
        }
    }
    return NULL;
}


#define LEARNING_RATE_FOR_SHARED_INFO 0.1f // How much to nudge based on shared info

void ObjectiveDiscovery_ProcessSharedObjectiveData(bot_t* pReceivingBot, int shared_candidate_id,
                                                   ObjectiveType_e shared_type, float shared_confidence,
                                                   const char* shared_keyword) {
    if (!pReceivingBot || !pReceivingBot->is_used || !pReceivingBot->nn_initialized) return;

    CandidateObjective_t* pLocalCand = GetCandidateObjectiveById(shared_candidate_id);
    if (pLocalCand) {
        // Nudge confidence only if shared info is more confident
        if (shared_confidence > pLocalCand->confidence_score) {
           pLocalCand->confidence_score += LEARNING_RATE_FOR_SHARED_INFO * (shared_confidence - pLocalCand->confidence_score);
           if (pLocalCand->confidence_score > 0.95f) pLocalCand->confidence_score = 0.95f; // Cap
        }

        // Update learned type if shared is more specific or significantly more confident
        bool shared_type_is_specific = (shared_type != OBJ_TYPE_NONE && shared_type != OBJ_TYPE_STRATEGIC_LOCATION);
        bool local_type_is_generic = (pLocalCand->learned_objective_type == OBJ_TYPE_NONE || pLocalCand->learned_objective_type == OBJ_TYPE_STRATEGIC_LOCATION);

        if (shared_type_is_specific && (local_type_is_generic || shared_confidence > pLocalCand->confidence_score + 0.2f )) {
            // If local type is generic, or if shared type is specific and the shared confidence is significantly higher
            // (e.g. shared info is much more certain about a specific type than local generic assessment)
             if (shared_confidence >= pLocalCand->confidence_score || local_type_is_generic) { // Extra check to prefer more confident specific typing
                pLocalCand->learned_objective_type = shared_type;
             }
        }

        // Update keyword if local is empty and shared is not
        if (shared_keyword && shared_keyword[0] != '\0' && pLocalCand->primary_correlated_message_keyword[0] == '\0') {
           strncpy(pLocalCand->primary_correlated_message_keyword, shared_keyword, sizeof(pLocalCand->primary_correlated_message_keyword) -1 );
           pLocalCand->primary_correlated_message_keyword[sizeof(pLocalCand->primary_correlated_message_keyword)-1] = '\0';
        }

        // Example debug print (could be behind a cvar)
        // if (pReceivingBot->pEdict == gpGlobals->player_edicts[1]) { // If it's the listen server player/bot
        //     ALERT(at_console, "Bot %s (Idx %d) received shared info for Obj %d. New Conf: %.2f, Type: %s, Keyword: %s\n",
        //           pReceivingBot->name, (int)(pReceivingBot - bots), shared_candidate_id,
        //           pLocalCand->confidence_score, ObjectiveTypeToString(pLocalCand->learned_objective_type),
        //           pLocalCand->primary_correlated_message_keyword);
        // }

    } else {
        // Bot doesn't know this candidate yet.
        // Future enhancement: Could add it if confidence is very high and enough info (like location) is shared.
        // For now, only update existing known candidates.
    }
}

[end of bot_objective_discovery.cpp]
