//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// bot.cpp
//

#ifndef _WIN32
#include <string.h>
#endif

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include <meta_api.h>

#include "bot.h"
#include "bot_func.h"
#include "waypoint.h"
#include "bot_weapons.h"
#include "bot_objective_discovery.h"
#include "bot_tactical_ai.h"
#include "bot_neuro_evolution.h"
#include "bot_rl_aiming.h"         // For RL Aiming agent
#include "bot_ngram_functions.h"   // For N-gram chat generation
#include "bot_categorized_chat.h"  // For AdvancedChat_HandleGameEvent (though not directly used in this diff, good for context)

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <cmath>


extern edict_t *clients[32];
extern int mod_id;
extern WAYPOINT waypoints[MAX_WAYPOINTS];
extern int num_waypoints;
extern int default_bot_skill;
extern int bot_strafe_percent;
extern int bot_chat_percent;
extern int bot_taunt_percent;
extern int bot_whine_percent;
extern int bot_logo_percent;
extern int bot_chat_tag_percent;
extern int bot_chat_drop_percent;
extern int bot_chat_swap_percent;
extern int bot_chat_lower_percent;
extern bool b_random_color;
extern edict_t *pent_info_ctfdetect;
extern int bot_reaction_time;
extern int IsDedicatedServer;
extern int holywars_gamemode;
extern edict_t *listenserver_edict;


extern int max_team_players[4];
extern int team_class_limits[4];
extern int team_allies[4];
extern int max_teams;
extern bot_chat_t bot_chat[MAX_BOT_CHAT];
extern bot_chat_t bot_whine[MAX_BOT_CHAT];
extern int bot_chat_count;
extern int bot_whine_count;
extern int recent_bot_chat[];
extern int recent_bot_whine[];
extern bool checked_teamplay;
extern bool is_team_play;

extern int number_skins;
extern skin_t bot_skins[MAX_SKINS];

extern GlobalTacticalState_t g_tactical_state;

// CVars for chat system (defined in dll.cpp)
extern cvar_t bot_advanced_chat_enable;
extern cvar_t bot_ngram_chat_enable;
extern cvar_t bot_ngram_chat_idle_frequency;

// N-gram model (defined in bot_advanced_chat.cpp or bot_nlp_chat.cpp)
extern NgramModel_t g_chat_ngram_model;

// Constants for N-gram idle chat in BotThink
const float NGRAM_BOTTHINK_MIN_INTERVAL = 10.0f;
const float NGRAM_BOTTHINK_MAX_INTERVAL = 25.0f;
const int NGRAM_BOTTHINK_MAX_LEN = 100;


static FILE *fp;


#define PLAYER_SEARCH_RADIUS     40.0
#define FLF_PLAYER_SEARCH_RADIUS 60.0


#define MAX_BOT_NAMES 100

int number_names = 0;
char bot_names[MAX_BOT_NAMES][BOT_NAME_LEN+1];

#define MAX_BOT_LOGOS 100
int num_logos = 0;
char bot_logos[MAX_BOT_LOGOS][16];

bot_t bots[32];
bool b_observer_mode = FALSE;
bool b_botdontshoot = FALSE;


float pause_frequency[5] = {4, 7, 10, 15, 20};
float pause_time[5][2] = {
   {0.2, 0.5}, {0.5, 1.0}, {0.7, 1.3}, {1.0, 1.7}, {1.2, 2.0}};



void BotSpawnInit( bot_t *pBot )
{
   pBot->v_prev_origin = Vector(9999.0, 9999.0, 9999.0);
   pBot->f_speed_check_time = gpGlobals->time;

   pBot->waypoint_origin = Vector(0, 0, 0);
   pBot->f_waypoint_time = 0.0;
   pBot->curr_waypoint_index = -1;
   pBot->prev_waypoint_index[0] = -1;
   pBot->prev_waypoint_index[1] = -1;
   pBot->prev_waypoint_index[2] = -1;
   pBot->prev_waypoint_index[3] = -1;
   pBot->prev_waypoint_index[4] = -1;

   pBot->f_random_waypoint_time = gpGlobals->time;
   pBot->waypoint_goal = -1;
   pBot->f_waypoint_goal_time = 0.0;
   pBot->waypoint_near_flag = FALSE;
   pBot->waypoint_flag_origin = Vector(0, 0, 0);
   pBot->prev_waypoint_distance = 0.0;

   if (!pBot->loaded_from_persistence) {
       pBot->weapon_points[0] = 0;
       pBot->weapon_points[1] = 0;
       pBot->weapon_points[2] = 0;
       pBot->weapon_points[3] = 0;
       pBot->weapon_points[4] = 0;
       pBot->weapon_points[5] = 0;
   }

   pBot->blinded_time = 0.0;
   pBot->f_max_speed = CVAR_GET_FLOAT("sv_maxspeed");
   pBot->f_prev_speed = 0.0;
   pBot->f_find_item = 0.0;
   pBot->ladder_dir = LADDER_UNKNOWN;
   pBot->f_start_use_ladder_time = 0.0;
   pBot->f_end_use_ladder_time = 0.0;
   pBot->waypoint_top_of_ladder = FALSE;
   pBot->f_wall_check_time = 0.0;
   pBot->f_wall_on_right = 0.0;
   pBot->f_wall_on_left = 0.0;
   pBot->f_dont_avoid_wall_time = 0.0;
   pBot->f_look_for_waypoint_time = 0.0;
   pBot->f_jump_time = 0.0;
   pBot->f_drop_check_time = 0.0;

   if (RANDOM_LONG(1, 100) <= 50) pBot->wander_dir = WANDER_LEFT;
   else pBot->wander_dir = WANDER_RIGHT;

   pBot->f_exit_water_time = 0.0;
   pBot->pBotEnemy = NULL;
   pBot->f_bot_see_enemy_time = gpGlobals->time;
   pBot->f_bot_find_enemy_time = gpGlobals->time;
   pBot->f_aim_tracking_time = 0.0;
   pBot->f_aim_x_angle_delta = 0.0;
   pBot->f_aim_y_angle_delta = 0.0;
   pBot->pBotUser = NULL;
   pBot->f_bot_use_time = 0.0;
   pBot->b_bot_say = FALSE;
   pBot->f_bot_say = 0.0;
   pBot->bot_say_msg[0] = 0;
   pBot->f_bot_chat_time = gpGlobals->time;
   pBot->enemy_attack_count = 0;
   pBot->f_duck_time = 0.0;
   pBot->f_sniper_aim_time = 0.0;
   pBot->f_shoot_time = gpGlobals->time;
   pBot->f_primary_charging = -1.0;
   pBot->f_secondary_charging = -1.0;
   pBot->charging_weapon_id = 0;
   pBot->f_gren_throw_time = -1.0;
   pBot->f_gren_check_time = 0.0;
   pBot->b_grenade_primed = FALSE;
   pBot->grenade_type = 0;
   pBot->f_grenade_search_time = 0.0;
   pBot->f_grenade_found_time = 0.0;
   pBot->f_medic_check_time = 0.0;
   pBot->f_medic_pause_time = 0.0;
   pBot->f_medic_yell_time = 0.0;
   pBot->f_pause_time = 0.0;
   pBot->f_sound_update_time = 0.0;
   pBot->bot_has_flag = FALSE;
   pBot->b_see_tripmine = FALSE;
   pBot->b_shoot_tripmine = FALSE;
   pBot->v_tripmine = Vector(0,0,0);
   pBot->b_use_health_station = FALSE;
   pBot->f_use_health_time = 0.0;
   pBot->b_use_HEV_station = FALSE;
   pBot->f_use_HEV_time = 0.0;
   pBot->b_use_button = FALSE;
   pBot->f_use_button_time = 0;
   pBot->b_lift_moving = FALSE;
   pBot->b_use_capture = FALSE;
   pBot->f_use_capture_time = 0.0;
   pBot->pCaptureEdict = NULL;
   pBot->b_spray_logo = FALSE;
   pBot->f_engineer_build_time = 0.0;
   pBot->b_build_sentrygun = FALSE;
   pBot->b_build_dispenser = FALSE;
   pBot->f_other_sentry_time = 0.0;
   pBot->b_upgrade_sentry = FALSE;
   pBot->f_medic_check_health_time = 0.0;
   pBot->f_reaction_target_time = 0.0;

   memset(&(pBot->current_weapon), 0, sizeof(pBot->current_weapon));
   memset(&(pBot->m_rgAmmo), 0, sizeof(pBot->m_rgAmmo));

   pBot->current_discovered_objective_id = -1;
   pBot->current_objective_desirability = 0.0f;
   pBot->last_objective_selection_time = 0.0f;
   pBot->is_interacting_with_objective = false;
   pBot->interaction_timer = 0.0f;

   pBot->f_next_tactical_nn_eval_time = gpGlobals->time + (TACTICAL_NN_EVAL_INTERVAL / 2.0f) + RANDOM_FLOAT(0, TACTICAL_NN_EVAL_INTERVAL / 2.0f);

   // Reset fitness stats for the new spawn/evaluation period
   pBot->current_eval_score_contribution = 0.0f;
   pBot->current_eval_objectives_captured_or_defended = 0;
   pBot->current_eval_kills = 0;
   pBot->current_eval_deaths = 0;
   pBot->current_eval_damage_dealt = 0.0f;
   pBot->current_eval_survival_start_time = gpGlobals->time;
   pBot->last_chosen_directive_for_fitness_eval = NUM_TACTICAL_DIRECTIVES; // A default/invalid state

   // RL Aiming specific initializations
   pBot->current_aiming_episode_data.clear();
   pBot->aiming_episode_step_count = 0;
   pBot->f_next_rl_aim_action_time = gpGlobals->time;
   pBot->has_last_aim_state_and_action = false;
   pBot->last_shot_fired_was_by_rl = false;
}


void PrepareNNInputs(bot_t *pBot, float* nn_input_array) {
    if (!pBot || !nn_input_array || !gpGlobals) {
        if (nn_input_array) memset(nn_input_array, 0, sizeof(float) * NN_INPUT_SIZE);
        return;
    }
    memset(nn_input_array, 0, sizeof(float) * NN_INPUT_SIZE);

    int current_idx = 0;
    const GlobalTacticalState_t& ts = GetGlobalTacticalState();

    nn_input_array[current_idx++] = (float)ts.current_game_phase / (float)(GAME_PHASE_GAME_OVER + 1);
    float max_round_time_estimate = 180.0f;
    if (ts.round_time_remaining > 0.001f && ts.round_time_elapsed >= 0) {
        max_round_time_estimate = ts.round_time_elapsed + ts.round_time_remaining;
        if (max_round_time_estimate <= 0.001f) max_round_time_estimate = 180.0f;
        nn_input_array[current_idx++] = ts.round_time_remaining / max_round_time_estimate;
    } else if (ts.round_time_elapsed >= 0) {
         float normalized_elapsed = ts.round_time_elapsed / max_round_time_estimate;
         nn_input_array[current_idx++] = (1.0f - normalized_elapsed > 0.0f ? (1.0f - normalized_elapsed) : 0.0f);
    } else {
        nn_input_array[current_idx++] = 0.5f;
    }

    int my_team_idx = pBot->bot_team;
    int enemy_team_idx = -1;
    float typical_win_score = (mod_id == CSTRIKE_DLL) ? 16.0f : ( (mod_id == TFC_DLL) ? 100.0f : 50.0f );

    if (my_team_idx >= 0 && my_team_idx < MAX_TEAMS) {
        nn_input_array[current_idx++] = ts.team_scores[my_team_idx] > 0 ? ((float)ts.team_scores[my_team_idx] / typical_win_score) : 0.0f;
        if (ts.num_active_teams >= 2) {
            for(int t=0; t < MAX_TEAMS; ++t) {
                if (t != my_team_idx && ts.team_info[t].is_active_team) {
                    enemy_team_idx = t;
                    break;
                }
            }
        }
    } else {
        nn_input_array[current_idx++] = 0.0f;
    }
    nn_input_array[current_idx++] = (enemy_team_idx != -1 && enemy_team_idx < MAX_TEAMS) ? ((float)ts.team_scores[enemy_team_idx] / typical_win_score) : 0.0f;

    const TeamTacticalInfo_t* team_ptrs[2] = {NULL, NULL};
    if (my_team_idx != -1 && my_team_idx < MAX_TEAMS && ts.team_info[my_team_idx].is_active_team) {
        team_ptrs[0] = &ts.team_info[my_team_idx];
    }
    if (enemy_team_idx != -1 && enemy_team_idx < MAX_TEAMS && ts.team_info[enemy_team_idx].is_active_team) {
        team_ptrs[1] = &ts.team_info[enemy_team_idx];
    }

    for (int team_loop_idx = 0; team_loop_idx < 2; ++team_loop_idx) {
        const TeamTacticalInfo_t* t_info = team_ptrs[team_loop_idx];
        if (t_info) {
            nn_input_array[current_idx++] = t_info->num_total_players > 0 ? (float)t_info->num_alive_players / (float)t_info->num_total_players : 0.0f;
            for (int c = 0; c < MAX_CLASSES_PER_MOD; ++c) {
                nn_input_array[current_idx++] = t_info->num_total_players > 0 ? (float)t_info->class_counts[c] / (float)t_info->num_total_players : 0.0f;
            }
            nn_input_array[current_idx++] = t_info->aggregate_health_ratio;
            nn_input_array[current_idx++] = t_info->aggregate_armor_ratio;
            float max_money_estimate = (mod_id == CSTRIKE_DLL) ? (16000.0f * 5.0f) : 1.0f;
            nn_input_array[current_idx++] = (max_money_estimate > 0.001f) ? (float)t_info->team_resource_A / max_money_estimate : 0.0f;
        } else {
            nn_input_array[current_idx++] = 0.0f;
            for (int c = 0; c < MAX_CLASSES_PER_MOD; ++c) nn_input_array[current_idx++] = 0.0f;
            nn_input_array[current_idx++] = 0.0f;
            nn_input_array[current_idx++] = 0.0f;
            nn_input_array[current_idx++] = 0.0f;
        }
    }

    float map_diagonal_approx = 4096.0f;
    for (int obj_loop_idx = 0; obj_loop_idx < TOP_N_OBJECTIVES_FOR_NN_INPUT; ++obj_loop_idx) {
        if (obj_loop_idx < ts.num_valid_objectives) {
            const ObjectivePointStatus_t* obj_status = &ts.objective_points[obj_loop_idx];
            nn_input_array[current_idx++] = (float)obj_status->type / (float)(OBJ_TYPE_DOOR_OBSTACLE + 1);
            float owner_norm = 0.5f;
            if (obj_status->owner_team != -1 && my_team_idx != -1) {
                 if(obj_status->owner_team == my_team_idx) owner_norm = 0.0f;
                 else if (obj_status->owner_team == enemy_team_idx) owner_norm = 1.0f;
                 else if (enemy_team_idx == -1 && obj_status->owner_team != my_team_idx) owner_norm = 1.0f;
            }
            nn_input_array[current_idx++] = owner_norm;
            nn_input_array[current_idx++] = obj_status->is_contested ? 1.0f : 0.0f;
            Vector rel_pos = obj_status->position - pBot->pEdict->v.origin;
            nn_input_array[current_idx++] = rel_pos.x / map_diagonal_approx;
            nn_input_array[current_idx++] = rel_pos.y / map_diagonal_approx;
            nn_input_array[current_idx++] = rel_pos.z / map_diagonal_approx;
            nn_input_array[current_idx++] = rel_pos.Length() / map_diagonal_approx;
            bool is_flag_type = (obj_status->type == OBJ_TYPE_FLAG || obj_status->type == OBJ_TYPE_FLAG_LIKE_PICKUP);
            nn_input_array[current_idx++] = is_flag_type && obj_status->is_flag_at_home_base ? 1.0f : 0.0f;
        } else {
            for(int f=0; f < 8; ++f) nn_input_array[current_idx++] = 0.0f;
        }
    }

    if (current_idx > NN_INPUT_SIZE) {
       // ALERT(at_console, "CRITICAL: PrepareNNInputs overflow! Wrote %d, NN_INPUT_SIZE is %d\n", current_idx, NN_INPUT_SIZE);
    }
    while(current_idx < NN_INPUT_SIZE) {
        nn_input_array[current_idx++] = 0.0f;
    }
}


#define HIGH_CONFIDENCE_THRESHOLD 0.7f
#define OBJECTIVE_SELECTION_COOLDOWN 10.0f
#define INTERACTION_DURATION 1.5f

void BotSelectAndPursueDiscoveredObjective(bot_t *pBot) {
    if (!gpGlobals || g_candidate_objectives.empty()) {
        pBot->current_discovered_objective_id = -1;
        return;
    }
    if (pBot->is_interacting_with_objective) {
        return;
    }
    if ((gpGlobals->time - pBot->last_objective_selection_time) < OBJECTIVE_SELECTION_COOLDOWN && pBot->current_discovered_objective_id != -1) {
        CandidateObjective_t* currentObj = GetCandidateObjectiveById(pBot->current_discovered_objective_id);
        if (currentObj && currentObj->confidence_score >= HIGH_CONFIDENCE_THRESHOLD * 0.9f) {
             return;
        }
    }
    CandidateObjective_t* best_candidate = NULL;
    float max_desirability = -1.0f;
    if (pBot->current_discovered_objective_id != -1 && (gpGlobals->time - pBot->last_objective_selection_time) > (OBJECTIVE_SELECTION_COOLDOWN * 3.0f) ) {
    } else if (pBot->current_discovered_objective_id != -1) {
        CandidateObjective_t* currentObj = GetCandidateObjectiveById(pBot->current_discovered_objective_id);
        if(currentObj) max_desirability = pBot->current_objective_desirability * 0.8f;
    }
    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        CandidateObjective_t* cand = &g_candidate_objectives[i];
        if (cand->confidence_score < HIGH_CONFIDENCE_THRESHOLD) continue;
        float desirability = cand->confidence_score;
        if (cand->learned_objective_type == OBJ_TYPE_FLAG_LIKE_PICKUP || cand->learned_objective_type == OBJ_TYPE_FLAG) {
            desirability *= 1.5f;
        } else if (cand->learned_objective_type == OBJ_TYPE_CAPTURE_POINT) {
            desirability *= 1.2f;
            if (cand->current_owner_team == pBot->bot_team && cand->current_owner_team != -1) desirability *= 0.1f;
        } else if (cand->learned_objective_type == OBJ_TYPE_PRESSABLE_BUTTON) {
            desirability *= 0.8f;
        }
        float dist_to_cand_sq = (cand->location - pBot->pEdict->v.origin).LengthSquared();
        if (dist_to_cand_sq < 1.0f) dist_to_cand_sq = 1.0f;
        desirability = desirability * 10000.0f / (100.0f + dist_to_cand_sq);
        if (desirability > max_desirability) {
            max_desirability = desirability;
            best_candidate = cand;
        }
    }
    if (best_candidate) {
        if (pBot->current_discovered_objective_id != best_candidate->unique_id) {
            // ALERT(at_console, "Bot %s new objective: ID %d (type %s), Des: %.2f\n", pBot->name, best_candidate->unique_id, ObjectiveTypeToString(best_candidate->learned_objective_type), max_desirability);
        }
        pBot->current_discovered_objective_id = best_candidate->unique_id;
        pBot->current_objective_desirability = max_desirability;
        pBot->last_objective_selection_time = gpGlobals->time;
        int waypoint_idx = WaypointFindNearest(best_candidate->location, pBot->pEdict, REACHABLE_RANGE * 2, pBot->bot_team);
        if (waypoint_idx != -1 && (waypoints[waypoint_idx].flags & W_FL_DELETED) == 0) {
            pBot->waypoint_goal = waypoint_idx;
            pBot->waypoint_origin = waypoints[waypoint_idx].origin;
        } else {
            pBot->waypoint_goal = -1;
            pBot->waypoint_origin = best_candidate->location;
        }
        pBot->f_waypoint_goal_time = gpGlobals->time + 60.0f;
        pBot->is_interacting_with_objective = false;
    }
}


void BotNameInit( void )
{
   FILE *bot_name_fp;
   char bot_name_filename[256];
   int str_index;
   char name_buffer[80];
   int length, index;

   UTIL_BuildFileName(bot_name_filename, "HPB_bot_names.txt", NULL);

   bot_name_fp = fopen(bot_name_filename, "r");

   if (bot_name_fp != NULL)
   {
      while ((number_names < MAX_BOT_NAMES) &&
             (fgets(name_buffer, 80, bot_name_fp) != NULL))
      {
         length = strlen(name_buffer);

         if (name_buffer[length-1] == '\n')
         {
            name_buffer[length-1] = 0;  // remove '\n'
            length--;
         }

         str_index = 0;
         while (str_index < length)
         {
            if ((name_buffer[str_index] < ' ') || (name_buffer[str_index] > '~') ||
                (name_buffer[str_index] == '"'))
            for (index=str_index; index < length; index++)
               name_buffer[index] = name_buffer[index+1];

            str_index++;
         }

         if (name_buffer[0] != 0)
         {
            strncpy(bot_names[number_names], name_buffer, BOT_NAME_LEN);

            number_names++;
         }
      }

      fclose(bot_name_fp);
   }
}


void BotPickName( char *name_buffer )
{
   int name_index, index;
   bool used;
   edict_t *pPlayer;
   int attempts = 0;

   name_index = RANDOM_LONG(1, number_names) - 1;  // zero based

   // check make sure this name isn't used
   used = TRUE;

   while (used)
   {
      used = FALSE;

      for (index = 1; index <= gpGlobals->maxClients; index++)
      {
         pPlayer = INDEXENT(index);

         if (pPlayer && !pPlayer->free)
         {
            if (strcmp(bot_names[name_index], STRING(pPlayer->v.netname)) == 0)
            {
               used = TRUE;
               break;
            }
         }
      }

      if (used)
      {
         name_index++;

         if (name_index == number_names)
            name_index = 0;

         attempts++;

         if (attempts == number_names)
            used = FALSE;  // break out of loop even if already used
      }
   }

   strcpy(name_buffer, bot_names[name_index]);
}


void BotCreate( edict_t *pPlayer, const char *arg1, const char *arg2,
                const char *arg3, const char *arg4, const char *arg5 )
{
   edict_t *BotEnt;
   bot_t *pBot;
   char c_skin[BOT_SKIN_LEN+1];
   char c_name[BOT_NAME_LEN+1];
   int skill;
   int index;
   int i, j, length;
   bool found = FALSE;
   int top_color, bottom_color;
   char c_topcolor[4], c_bottomcolor[4];

   top_color = -1;
   bottom_color = -1;

   if ((mod_id == VALVE_DLL) ||
       ((mod_id == GEARBOX_DLL) && (pent_info_ctfdetect == NULL)) ||
       (mod_id == HOLYWARS_DLL) || (mod_id == DMC_DLL))
   {
      int  max_skin_index;

      max_skin_index = number_skins;

      if ((arg1 == NULL) || (*arg1 == 0))
      {
         index = RANDOM_LONG(0, number_skins-1);

         // check if this skin has already been used...
         while (bot_skins[index].skin_used == TRUE)
         {
            index++;

            if (index == max_skin_index)
               index = 0;
         }

         bot_skins[index].skin_used = TRUE;

         // check if all skins are now used...
         for (i = 0; i < max_skin_index; i++)
         {
            if (bot_skins[i].skin_used == FALSE)
               break;
         }

         // if all skins are used, reset used to FALSE for next selection
         if (i == max_skin_index)
         {
            for (i = 0; i < max_skin_index; i++)
               bot_skins[i].skin_used = FALSE;
         }

         strcpy(c_skin, bot_skins[index].model_name);
      }
      else
      {
         strncpy( c_skin, arg1, BOT_SKIN_LEN-1 );
         c_skin[BOT_SKIN_LEN] = 0;  // make sure c_skin is null terminated
      }

      for (i = 0; c_skin[i] != 0; i++)
         c_skin[i] = tolower( c_skin[i] );  // convert to all lowercase

      index = 0;

      while ((!found) && (index < max_skin_index))
      {
         if (strcmp(c_skin, bot_skins[index].model_name) == 0)
            found = TRUE;
         else
            index++;
      }

      if (found == TRUE)
      {
         if ((arg2 != NULL) && (*arg2 != 0))
         {
            strncpy( c_name, arg2, BOT_SKIN_LEN-1 );
            c_name[BOT_SKIN_LEN] = 0;  // make sure c_name is null terminated
         }
         else
         {
            if (number_names > 0)
               BotPickName( c_name );
            else
               strcpy(c_name, bot_skins[index].bot_name);
         }
      }
      else
      {
         char dir_name[32];
         char filename[128];

         struct stat stat_str;

         GetGameDir(dir_name);

         sprintf(filename, "%s/models/player/%s", dir_name, c_skin);

         if (stat(filename, &stat_str) != 0)
         {
            sprintf(filename, "valve/models/player/%s", c_skin);

            if (stat(filename, &stat_str) != 0)
            {
               char err_msg[80];

               sprintf( err_msg, "model \"%s\" is unknown.\n", c_skin );
               if (pPlayer)
                  ClientPrint(pPlayer, HUD_PRINTNOTIFY, err_msg );
               if (IsDedicatedServer)
                  printf(err_msg);

               if (pPlayer)
                  ClientPrint(pPlayer, HUD_PRINTNOTIFY,
                     "use barney, gina, gman, gordon, helmet, hgrunt,\n");
               if (IsDedicatedServer)
                  printf("use barney, gina, gman, gordon, helmet, hgrunt,\n");
               if (pPlayer)
                  ClientPrint(pPlayer, HUD_PRINTNOTIFY,
                     "    recon, robo, scientist, or zombie\n");
               if (IsDedicatedServer)
                  printf("    recon, robo, scientist, or zombie\n");
               return;
            }
         }

         if ((arg2 != NULL) && (*arg2 != 0))
         {
            strncpy( c_name, arg2, BOT_NAME_LEN-1 );
            c_name[BOT_NAME_LEN] = 0;  // make sure c_name is null terminated
         }
         else
         {
            if (number_names > 0)
               BotPickName( c_name );
            else
            {
               // copy the name of the model to the bot's name...
               strncpy( c_name, arg1, BOT_NAME_LEN-1 );
               c_name[BOT_NAME_LEN] = 0;  // make sure c_skin is null terminated
            }
         }
      }

      skill = 0;

      if ((arg3 != NULL) && (*arg3 != 0))
         skill = atoi(arg3);

      if ((skill < 1) || (skill > 5))
         skill = default_bot_skill;

      if ((arg4 != NULL) && (*arg4 != 0))
         top_color = atoi(arg4);

      if ((top_color < 0) || (top_color > 255))
         top_color = -1;
      else
         sprintf(c_topcolor, "%d", top_color);

      if ((arg5 != NULL) && (*arg5 != 0))
         bottom_color = atoi(arg5);

      if ((bottom_color < 0) || (bottom_color > 255))
         bottom_color = -1;
      else
         sprintf(c_bottomcolor, "%d", bottom_color);

      if ((top_color == -1) && (bottom_color == -1) && (b_random_color))
      {
         top_color = RANDOM_LONG(0, 255);
         sprintf(c_topcolor, "%d", top_color);

         bottom_color = RANDOM_LONG(0, 255);
         sprintf(c_bottomcolor, "%d", bottom_color);
      }
   }
   else
   {
      if ((arg3 != NULL) && (*arg3 != 0))
      {
         strncpy( c_name, arg3, BOT_NAME_LEN-1 );
         c_name[BOT_NAME_LEN] = 0;  // make sure c_name is null terminated
      }
      else
      {
         if (number_names > 0)
            BotPickName( c_name );
         else
            strcpy(c_name, "Bot");
      }

      skill = 0;

      if ((arg4 != NULL) && (*arg4 != 0))
         skill = atoi(arg4);

      if ((skill < 1) || (skill > 5))
         skill = default_bot_skill;
   }

   length = strlen(c_name);

   // remove any illegal characters from name...
   for (i = 0; i < length; i++)
   {
      if ((c_name[i] <= ' ') || (c_name[i] > '~') ||
          (c_name[i] == '"'))
      {
         for (j = i; j < length; j++)  // shuffle chars left (and null)
            c_name[j] = c_name[j+1];
         length--;
      }
   }

   BotEnt = (*g_engfuncs.pfnCreateFakeClient)( c_name );

   if (FNullEnt( BotEnt ))
   {
      if (pPlayer)
         ClientPrint( pPlayer, HUD_PRINTNOTIFY, "Max. Players reached.  Can't create HPB bot!\n");
   }
   else
   {
      char ptr[128];  // allocate space for message from ClientConnect
      char *infobuffer;
      int clientIndex;
      int index;

      if (IsDedicatedServer)
         printf("Creating HPB bot...\n");
      else if (pPlayer)
         ClientPrint( pPlayer, HUD_PRINTNOTIFY, "Creating HPB bot...\n");

      index = 0;
      while ((bots[index].is_used) && (index < 32))
         index++;

      if (index == 32)
      {
         ClientPrint( pPlayer, HUD_PRINTNOTIFY, "Can't create HPB bot!\n");
         return;
      }

      // create the player entity by calling MOD's player function
      // (from LINK_ENTITY_TO_CLASS for player object)

      CALL_GAME_ENTITY (PLID, "player", VARS(BotEnt));

      infobuffer = GET_INFOKEYBUFFER( BotEnt );
      clientIndex = ENTINDEX( BotEnt );

      if (!checked_teamplay)  // check for team play...
         BotCheckTeamplay();

      // is this a MOD that supports model colors AND it's not teamplay?
      if (((mod_id == VALVE_DLL) || (mod_id == DMC_DLL) ||
           (mod_id == GEARBOX_DLL) || (mod_id == HOLYWARS_DLL)) &&
          (is_team_play == FALSE))
      {
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "model", c_skin );

         if (top_color != -1)
            SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "topcolor", c_topcolor );

         if (bottom_color != -1)
            SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "bottomcolor", c_bottomcolor );
      }
      else // other mods
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "model", "" ); // bugfix, thanks Whistler

      if (mod_id == CSTRIKE_DLL)
      {
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "rate", "3500.000000");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "cl_updaterate", "20");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "cl_lw", "1");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "cl_lc", "1");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "tracker", "0");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "cl_dlmax", "128");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "lefthand", "1");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "friends", "0");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "dm", "0");
         SET_CLIENT_KEYVALUE( clientIndex, infobuffer, "ah", "1");
      }

      MDLL_ClientConnect( BotEnt, c_name, "127.0.0.1", ptr );

      // HPB_bot metamod fix - START

      // we have to do the ClientPutInServer() hook's job ourselves since calling the MDLL_
      // function calls directly the gamedll one, and not ours. You are allowed to call this
      // an "awful hack".

      while ((i < 32) && (clients[i] != NULL))
         i++;

      if (i < 32)
         clients[i] = BotEnt;  // store this clients edict in the clients array

      // HPB_bot metamod fix - END

      // Pieter van Dijk - use instead of DispatchSpawn() - Hip Hip Hurray!
      MDLL_ClientPutInServer( BotEnt );

      BotEnt->v.flags |= FL_THIRDPARTYBOT;

      // initialize all the variables for this bot...

      pBot = &bots[index];

      pBot->is_used = TRUE;
      pBot->respawn_state = RESPAWN_IDLE;
      pBot->f_create_time = gpGlobals->time;
      // Name handling: c_name is used for CreateFakeClient.
      // If loaded from persistence, pBot->name would have the loaded name.
      // The engine then sets pEdict->v.netname. BotThink ensures pBot->name gets this if pBot->name[0]==0.
      if (pBot->loaded_from_persistence && pBot->name[0] != '\0') {
         // If a name was loaded, ensure c_name uses it for CreateFakeClient if that's desired,
         // but c_name is already determined above based on args or BotPickName.
         // The critical part is that pBot->name itself isn't overwritten by a default *if loaded*.
      } else {
         pBot->name[0] = 0; // Ensure it's clear if not loaded, so BotThink can populate it.
      }

      if (!pBot->loaded_from_persistence) {
          pBot->bot_money = 0; // Example: money is not persistent per this design
          pBot->sentrygun_waypoint = -1;
          pBot->dispenser_waypoint = -1;
      }
      // sentrygun_level and dispenser_built are dynamic.
      pBot->sentrygun_level = 0;
      pBot->dispenser_built = 0;

      // Skin handling
      if (pBot->loaded_from_persistence && pBot->skin[0] != '\0') {
          // If a skin was loaded, ensure c_skin uses it.
          // c_skin is determined above by args or defaults. If args were given, they override.
          // If no args, and persistence is on, c_skin should ideally be pBot->skin.
          // For now, we assume c_skin is set correctly above, and pBot->skin will take that value
          // UNLESS loaded_from_persistence is true, in which case pBot->skin is already loaded.
          // strcpy(c_skin, pBot->skin); // This would force use of loaded skin.
      } else {
          // If not loaded from persistence, pBot->skin will take the value of c_skin.
          strcpy(pBot->skin, c_skin);
      }

      // Top/Bottom color
      if (!pBot->loaded_from_persistence) {
          pBot->top_color = top_color;
          pBot->bottom_color = bottom_color;
      } // else, they are already set from loaded data by LoadBotMemory

      pBot->pEdict = BotEnt;

      if (!pBot->loaded_from_persistence) {
          BotPickLogo(pBot); // Pick a new logo only if not loaded
      } // else, pBot->logo_name is already set by LoadBotMemory

      pBot->not_started = 1;  // hasn't joined game yet

      if (mod_id == TFC_DLL)
         pBot->start_action = MSG_TFC_IDLE;
      else if (mod_id == CSTRIKE_DLL)
         pBot->start_action = MSG_CS_IDLE;
      else if ((mod_id == GEARBOX_DLL) && (pent_info_ctfdetect != NULL))
         pBot->start_action = MSG_OPFOR_IDLE;
      else if (mod_id == FRONTLINE_DLL)
         pBot->start_action = MSG_FLF_IDLE;
      else
         pBot->start_action = 0;  // not needed for non-team MODs


      BotSpawnInit(pBot);

      pBot->need_to_initialize = FALSE;  // don't need to initialize yet

      BotEnt->v.idealpitch = BotEnt->v.v_angle.x;
      BotEnt->v.ideal_yaw = BotEnt->v.v_angle.y;

      // these should REALLY be MOD dependant...
      BotEnt->v.pitch_speed = 270;  // slightly faster than HLDM of 225
      BotEnt->v.yaw_speed = 250; // slightly faster than HLDM of 210

      pBot->warmup = 0;  // for Front Line Force
      pBot->idle_angle = 0.0;
      pBot->idle_angle_time = 0.0;
      pBot->round_end = 0;
      pBot->defender = 0;

      if (!pBot->loaded_from_persistence) {
          pBot->strafe_percent = bot_strafe_percent;
          pBot->chat_percent = bot_chat_percent;
          pBot->taunt_percent = bot_taunt_percent;
          pBot->whine_percent = bot_whine_percent;
          pBot->chat_tag_percent = bot_chat_tag_percent;
          pBot->chat_drop_percent = bot_chat_drop_percent;
          pBot->chat_swap_percent = bot_chat_swap_percent;
          pBot->chat_lower_percent = bot_chat_lower_percent;
          pBot->logo_percent = bot_logo_percent;
          pBot->reaction_time = bot_reaction_time; // Set from global default if not loaded
      }
      // These are dynamic, not persistent in the same way
      pBot->f_strafe_direction = 0.0;  // not strafing
      pBot->f_strafe_time = 0.0;

      // NN Initialization:
      // If LoadBotMemory already initialized it with saved weights, pBot->nn_initialized will be true.
      // Otherwise, initialize with random weights.
      if (!pBot->nn_initialized) {
          NN_Initialize(&pBot->tactical_nn, NN_INPUT_SIZE, NN_HIDDEN_SIZE, NUM_TACTICAL_DIRECTIVES,
                        true,  // initialize_with_random_weights = true
                        NULL); // initial_weights_data = NULL
          pBot->nn_initialized = true;
      }
      // f_next_tactical_nn_eval_time is set in BotSpawnInit, but set here too for first creation before first spawn
      pBot->f_next_tactical_nn_eval_time = gpGlobals->time + TACTICAL_NN_EVAL_INTERVAL + RANDOM_FLOAT(0,1.5f);

      // Initialize RL Aiming specific fields
      pBot->aiming_nn_initialized = false; // Will be true if loaded from persistence or initialized below
      pBot->current_aiming_episode_data.clear();
      pBot->aiming_episode_step_count = 0;
      pBot->f_next_rl_aim_action_time = gpGlobals->time;
      pBot->has_last_aim_state_and_action = false;
      pBot->last_shot_fired_was_by_rl = false;

      if (!pBot->aiming_nn_initialized) { // If persistence didn't load it
          RL_NN_Initialize_Aiming(&pBot->aiming_rl_nn, RL_AIMING_STATE_SIZE, RL_AIMING_HIDDEN_LAYER_SIZE, RL_AIMING_OUTPUT_SIZE, true, NULL);
          pBot->aiming_nn_initialized = true;
      }

      pBot->f_start_vote_time = gpGlobals->time + RANDOM_LONG(120, 600);
      pBot->vote_in_progress = FALSE;
      pBot->f_vote_time = 0.0;

      // Skill, team, class are more complex. They are set by BotCreate args or defaults.
      // If LoadBotMemory ran, these would be populated. If BotCreate runs and then
      // LoadBotMemory runs, LoadBotMemory would overwrite these if is_used_in_save was true for that slot.
      // If BotCreate runs for a bot whose slot *was* populated by LoadBotMemory,
      // the current BotCreate logic would overwrite name/skin/skill/team/class with args or defaults.
      // This is where the interaction is tricky. The loaded_from_persistence flag in BotSpawnInit
      // primarily protects against *resetting* values during spawn.
      // The initial setting of skill/team/class from persistence vs. args needs careful handling
      // in BotCreate or immediately after LoadBotMemory.

      if (!pBot->loaded_from_persistence) {
          pBot->bot_skill = skill - 1;  // 0 based for array indexes
          // Team and class are special, they might be forced by args for this specific creation
          // If args are not given for team/class, *then* persisted team/class should be used if available.
          // The logic below tries to handle this: args override persistence, persistence overrides default -1.
          if (pBot->bot_team == -1) pBot->bot_team = -1; // Ensure it's -1 if not loaded and no args
          if (pBot->bot_class == -1) pBot->bot_class = -1;
      }

      // Handle team/class arguments, potentially overriding loaded persistent values if args are provided
      if ((mod_id == TFC_DLL) || (mod_id == CSTRIKE_DLL) ||
          ((mod_id == GEARBOX_DLL) && (pent_info_ctfdetect != NULL)) ||
          (mod_id == FRONTLINE_DLL))
      {
         if ((arg1 != NULL) && (arg1[0] != 0)) { // arg1 is team
             pBot->bot_team = atoi(arg1); // Creation arg for team overrides loaded value
             if ((arg2 != NULL) && (arg2[0] != 0)) { // arg2 is class
                 pBot->bot_class = atoi(arg2); // Creation arg for class overrides loaded value
             }
         } else {
             // No team/class args. If loaded from persistence, those values are already in pBot.
             // If not loaded from persistence, they were set to -1 above.
         }
      }
   }
}


int BotInFieldOfView(bot_t *pBot, Vector dest)
{
   // find angles from source to destination...
   Vector entity_angles = UTIL_VecToAngles( dest );

   // make yaw angle 0 to 360 degrees if negative...
   if (entity_angles.y < 0)
      entity_angles.y += 360;

   // get bot's current view angle...
   float view_angle = pBot->pEdict->v.v_angle.y;

   // make view angle 0 to 360 degrees if negative...
   if (view_angle < 0)
      view_angle += 360;

   // return the absolute value of angle to destination entity
   // zero degrees means straight ahead,  45 degrees to the left or
   // 45 degrees to the right is the limit of the normal view angle

   // rsm - START angle bug fix
   int angle = abs((int)view_angle - (int)entity_angles.y);

   if (angle > 180)
      angle = 360 - angle;

   return angle;
   // rsm - END
}


bool BotEntityIsVisible( bot_t *pBot, Vector dest )
{
   TraceResult tr;

   // trace a line from bot's eyes to destination...
   UTIL_TraceLine( pBot->pEdict->v.origin + pBot->pEdict->v.view_ofs,
                   dest, ignore_monsters,
                   pBot->pEdict->v.pContainingEntity, &tr );

   // check if line of sight to object is not blocked (i.e. visible)
   if (tr.flFraction >= 1.0)
      return TRUE;
   else
      return FALSE;
}

void BotLogoInit(void)
{
   FILE *bot_logo_fp;
   char bot_logo_filename[256];
   char logo_buffer[80];
   int length, index;

#ifdef _WIN32
   HANDLE h_logo;
   char dir_name[32];
   char decal_filename[256];
   DWORD dwDummy;

   struct stat stat_str;
   wadinfo_t wad_header;
   lumpinfo_t lump_info;

   GetGameDir(dir_name);

   sprintf(decal_filename, "%s/decals.wad", dir_name);

   if (stat(decal_filename, &stat_str) != 0)
   {
      strcpy(decal_filename, "valve/decals.wad");

      if (stat(decal_filename, &stat_str) != 0)
         return;  // file not found
   }

   h_logo = CreateFile(decal_filename, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

   if (h_logo == INVALID_HANDLE_VALUE)
      return;  // can't open file

   if (!ReadFile(h_logo, &wad_header, sizeof(wadinfo_t), &dwDummy, NULL))
   {
      CloseHandle(h_logo);  // can't read wad header
      return;
   }

   if (SetFilePointer(h_logo, wad_header.infotableofs, NULL, FILE_BEGIN) == 0)
   {
      CloseHandle(h_logo);
      return;  // can't seek to lump info table
   }

   for (index=0; index < wad_header.numlumps; index++)
   {
      if (!ReadFile(h_logo, &lump_info, sizeof(lumpinfo_t), &dwDummy, NULL))
      {
         CloseHandle(h_logo);  // can't read lump info
         return;
      }

      if (strncmp(lump_info.name, "{__", 3) == 0)
      {
         strcpy(bot_logos[num_logos], lump_info.name);
         num_logos++;
      }

      if (strncmp(lump_info.name, "__", 2) == 0)
      {
         strcpy(bot_logos[num_logos], lump_info.name);
         num_logos++;
      }
   }

   CloseHandle(h_logo);

#endif

   UTIL_BuildFileName(bot_logo_filename, "HPB_bot_logo.cfg", NULL);

   bot_logo_fp = fopen(bot_logo_filename, "r");

   if (bot_logo_fp != NULL)
   {
      while ((num_logos < MAX_BOT_LOGOS) &&
             (fgets(logo_buffer, 80, bot_logo_fp) != NULL))
      {
         length = strlen(logo_buffer);

         if (logo_buffer[length-1] == '\n')
         {
            logo_buffer[length-1] = 0;  // remove '\n'
            length--;
         }

         if (logo_buffer[0] != 0)
         {
            strncpy(bot_logos[num_logos], logo_buffer, 16);

            num_logos++;
         }
      }

      fclose(bot_logo_fp);
   }
}


void BotPickLogo(bot_t *pBot)
{
   bool used;
   int logo_index;
   int check_count;
   int index;

   pBot->logo_name[0] = 0;

   if (num_logos == 0)
      return;

   logo_index = RANDOM_LONG(1, num_logos) - 1;  // zero based

   // check make sure this name isn't used
   used = TRUE;
   check_count = 0;

   while ((used) && (check_count < MAX_BOT_LOGOS))
   {
      used = FALSE;

      for (index=0; index < 32; index++)
      {
         if (bots[index].is_used)
         {
            if (strcmp(bots[index].logo_name, bot_logos[logo_index]) == 0)
            {
               used = TRUE;
            }
         }
      }

      if (used)
         logo_index++;

      if (logo_index == MAX_BOT_LOGOS)
         logo_index = 0;

      check_count++;
   }

   strcpy(pBot->logo_name, bot_logos[logo_index]);
}


void BotSprayLogo(edict_t *pEntity, char *logo_name)
{
   int index;
   TraceResult pTrace;
   Vector v_src, v_dest;
   MAKE_VECTORS(pEntity->v.v_angle);
   v_src = pEntity->v.origin + pEntity->v.view_ofs;
   v_dest = v_src + gpGlobals->v_forward * 80;
   UTIL_TraceLine( v_src, v_dest, ignore_monsters, pEntity->v.pContainingEntity, &pTrace );

   index = DECAL_INDEX(logo_name);

   if (index < 0)
      return;

   if ((pTrace.pHit) && (pTrace.flFraction < 1.0))
   {
      if (pTrace.pHit->v.solid != SOLID_BSP)
         return;

      MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );

      if (index > 255)
      {
         WRITE_BYTE( TE_WORLDDECALHIGH);
         index -= 256;
      }
      else
         WRITE_BYTE( TE_WORLDDECAL );

      WRITE_COORD( pTrace.vecEndPos.x );
      WRITE_COORD( pTrace.vecEndPos.y );
      WRITE_COORD( pTrace.vecEndPos.z );
      WRITE_BYTE( index );

      MESSAGE_END();

      EMIT_SOUND_DYN2(pEntity, CHAN_VOICE, "player/sprayer.wav", 1.0, ATTN_NORM, 0, 100);
   }
}

void BotFindItem( bot_t *pBot )
{
   edict_t *pent = NULL;
   edict_t *pPickupEntity = NULL;
   Vector pickup_origin;
   Vector entity_origin;
   float radius = 500;
   bool can_pickup;
   float min_distance;
   char item_name[40];
   TraceResult tr;
   Vector vecStart;
   Vector vecEnd;
   int angle_to_entity;
   edict_t *pEdict = pBot->pEdict;

   pBot->pBotPickupItem = NULL;

   // use a MUCH smaller search radius when waypoints are available
   if ((num_waypoints > 0) && (pBot->curr_waypoint_index != -1))
      radius = 100.0;
   else
      radius = 500.0;

   min_distance = radius + 1.0;

   while ((pent = UTIL_FindEntityInSphere( pent, pEdict->v.origin, radius )) != NULL)
   {
      can_pickup = FALSE;  // assume can't use it until known otherwise

      strcpy(item_name, STRING(pent->v.classname));

      // see if this is a "func_" type of entity (func_button, etc.)...
      if (strncmp("func_", item_name, 5) == 0)
      {
         // BModels have 0,0,0 for origin so must use VecBModelOrigin...
         entity_origin = VecBModelOrigin(pent);

         vecStart = pEdict->v.origin + pEdict->v.view_ofs;
         vecEnd = entity_origin;

         angle_to_entity = BotInFieldOfView( pBot, vecEnd - vecStart );

         // check if entity is outside field of view (+/- 45 degrees)
         if (angle_to_entity > 45)
            continue;  // skip this item if bot can't "see" it

         // check if entity is a ladder (ladders are a special case)
         // DON'T search for ladders if there are waypoints in this level...
         if ((strcmp("func_ladder", item_name) == 0) && (num_waypoints == 0))
         {
            // force ladder origin to same z coordinate as bot since
            // the VecBModelOrigin is the center of the ladder.  For
            // LONG ladders, the center MAY be hundreds of units above
            // the bot.  Fake an origin at the same level as the bot...

            entity_origin.z = pEdict->v.origin.z;
            vecEnd = entity_origin;

            // trace a line from bot's eyes to func_ladder entity...
            UTIL_TraceLine( vecStart, vecEnd, dont_ignore_monsters,
                            pEdict->v.pContainingEntity, &tr);

            // check if traced all the way up to the entity (didn't hit wall)
            if (tr.flFraction >= 1.0)
            {
               // find distance to item for later use...
               float distance = (vecEnd - vecStart).Length( );

               // use the ladder about 100% of the time, if haven't
               // used a ladder in at least 5 seconds...
               if ((RANDOM_LONG(1, 100) <= 100) &&
                   ((pBot->f_end_use_ladder_time + 5.0) < gpGlobals->time))
               {
                  // if close to ladder...
                  if (distance < 100)
                  {
                     // don't avoid walls for a while
                     pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;
                  }

                  can_pickup = TRUE;
               }
            }
         }
         else
         {
            // trace a line from bot's eyes to func_ entity...
            UTIL_TraceLine( vecStart, vecEnd, dont_ignore_monsters,
                            pEdict->v.pContainingEntity, &tr);

            // check if traced all the way up to the entity (didn't hit wall)
            if (strcmp(item_name, STRING(tr.pHit->v.classname)) == 0)
            {
               // find distance to item for later use...
               float distance = (vecEnd - vecStart).Length( );

               // check if entity is wall mounted health charger...
               if (strcmp("func_healthcharger", item_name) == 0)
               {
                  // check if the bot can use this item and
                  // check if the recharger is ready to use (has power left)...
                  if ((pEdict->v.health < 100) && (pent->v.frame == 0))
                  {
                     // check if flag not set...
                     if (!pBot->b_use_health_station)
                     {
                        // check if close enough and facing it directly...
                        if ((distance < PLAYER_SEARCH_RADIUS) &&
                            (angle_to_entity <= 10))
                        {
                           pBot->b_use_health_station = TRUE;
                           pBot->f_use_health_time = gpGlobals->time;
                        }

                        // if close to health station...
                        if (distance < 100)
                        {
                           // don't avoid walls for a while
                           pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;
                        }

                        can_pickup = TRUE;
                     }
                  }
                  else
                  {
                     // don't need or can't use this item...
                     pBot->b_use_health_station = FALSE;
                  }
               }

               // check if entity is wall mounted HEV charger...
               else if (strcmp("func_recharge", item_name) == 0)
               {
                  // check if the bot can use this item and
                  // check if the recharger is ready to use (has power left)...
                  if ((pEdict->v.armorvalue < VALVE_MAX_NORMAL_BATTERY) &&
                      (pent->v.frame == 0))
                  {
                     // check if flag not set and facing it...
                     if (!pBot->b_use_HEV_station)
                     {
                        // check if close enough and facing it directly...
                        if ((distance < PLAYER_SEARCH_RADIUS) &&
                            (angle_to_entity <= 10))
                        {
                           pBot->b_use_HEV_station = TRUE;
                           pBot->f_use_HEV_time = gpGlobals->time;
                        }

                        // if close to HEV recharger...
                        if (distance < 100)
                        {
                           // don't avoid walls for a while
                           pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;
                        }

                        can_pickup = TRUE;
                     }
                  }
                  else
                  {
                     // don't need or can't use this item...
                     pBot->b_use_HEV_station = FALSE;
                  }
               }

               // check if entity is a button...
               else if (strcmp("func_button", item_name) == 0)
               {
                  // use the button about 100% of the time, if haven't
                  // used a button in at least 5 seconds...
                  if ((RANDOM_LONG(1, 100) <= 100) &&
                      ((pBot->f_use_button_time + 5) < gpGlobals->time))
                  {
                     // check if flag not set and facing it...
                     if (!pBot->b_use_button)
                     {
                        // check if close enough and facing it directly...
                        if ((distance < PLAYER_SEARCH_RADIUS) &&
                            (angle_to_entity <= 10))
                        {
                           pBot->b_use_button = TRUE;
                           pBot->b_lift_moving = FALSE;
                           pBot->f_use_button_time = gpGlobals->time;
                        }

                        // if close to button...
                        if (distance < 100)
                        {
                           // don't avoid walls for a while
                           pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;
                        }

                        can_pickup = TRUE;
                     }
                  }
                  else
                  {
                     // don't need or can't use this item...
                     pBot->b_use_button = FALSE;
                  }
               }
            }
         }
      }
      else  // everything else...
      {
         entity_origin = pent->v.origin;

         vecStart = pEdict->v.origin + pEdict->v.view_ofs;
         vecEnd = entity_origin;

         // find angles from bot origin to entity...
         angle_to_entity = BotInFieldOfView( pBot, vecEnd - vecStart );

         // check if entity is outside field of view (+/- 45 degrees)
         if (angle_to_entity > 45)
            continue;  // skip this item if bot can't "see" it

         // check if line of sight to object is not blocked (i.e. visible)
         if (BotEntityIsVisible( pBot, vecEnd ))
         {
            // check if entity is a weapon...
            if (strncmp("weapon_", item_name, 7) == 0)
            {
               if (pent->v.effects & EF_NODRAW)
               {
                  // someone owns this weapon or it hasn't respawned yet
                  continue;
               }

               can_pickup = TRUE;
            }

            // check if entity is ammo...
            else if (strncmp("ammo_", item_name, 5) == 0)
            {
               // check if the item is not visible (i.e. has not respawned)
               if (pent->v.effects & EF_NODRAW)
                  continue;

               can_pickup = TRUE;
            }

            // check if entity is a battery...
            else if (strcmp("item_battery", item_name) == 0)
            {
               // check if the item is not visible (i.e. has not respawned)
               if (pent->v.effects & EF_NODRAW)
                  continue;

               // check if the bot can use this item...
               if (pEdict->v.armorvalue < VALVE_MAX_NORMAL_BATTERY)
               {
                  can_pickup = TRUE;
               }
            }

            // check if entity is a healthkit...
            else if (strcmp("item_healthkit", item_name) == 0)
            {
               // check if the item is not visible (i.e. has not respawned)
               if (pent->v.effects & EF_NODRAW)
                  continue;

               // check if the bot can use this item...
               if (pEdict->v.health < 100)
               {
                  can_pickup = TRUE;
               }
            }

            // check if entity is a packed up weapons box...
            else if (strcmp("weaponbox", item_name) == 0)
            {
               can_pickup = TRUE;
            }

            // check if entity is the spot from RPG laser
            else if (strcmp("laser_spot", item_name) == 0)
            {
            }

            // check if entity is an armed tripmine
            else if (strcmp("monster_tripmine", item_name) == 0)
            {
               float distance = (pent->v.origin - pEdict->v.origin).Length( );

               if (pBot->b_see_tripmine)
               {
                  // see if this tripmine is closer to bot...
                  if (distance < (pBot->v_tripmine - pEdict->v.origin).Length())
                  {
                     pBot->v_tripmine = pent->v.origin;
                     pBot->b_shoot_tripmine = FALSE;

                     // see if bot is far enough to shoot the tripmine...
                     if (distance >= 375)
                     {
                        pBot->b_shoot_tripmine = TRUE;
                     }
                  }
               }
               else
               {
                  pBot->b_see_tripmine = TRUE;
                  pBot->v_tripmine = pent->v.origin;
                  pBot->b_shoot_tripmine = FALSE;

                  // see if bot is far enough to shoot the tripmine...
                  if (distance >= 375)  // 375 is damage radius
                  {
                     pBot->b_shoot_tripmine = TRUE;
                  }
               }
            }

            // check if entity is an armed satchel charge
            else if (strcmp("monster_satchel", item_name) == 0)
            {
            }

            // check if entity is a snark (squeak grenade)
            else if (strcmp("monster_snark", item_name) == 0)
            {
            }

            else if ((mod_id == FRONTLINE_DLL) && (!pBot->defender) &&
                     (strcmp("capture_point", item_name) == 0))
            {
               int team = UTIL_GetTeam(pEdict);  // skin and team must match

               // check if flag not set and point not captured...
               if ((!pBot->b_use_capture) && (pent->v.skin == (1 - team)))
               {
                  float distance = (pent->v.origin - pEdict->v.origin).Length( );

                  // check if close enough and facing it directly...
                  if ((distance < FLF_PLAYER_SEARCH_RADIUS) &&
                      (angle_to_entity <= 20))
                  {
                     pBot->b_use_capture = TRUE;
                     pBot->f_use_capture_time = gpGlobals->time + 3.0;
                     pBot->pCaptureEdict = pent;
                  }

                  // if close to capture point...
                  if (distance < 160)
                  {
                     // don't avoid walls for a while
                     pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;
                  }

                  can_pickup = TRUE;
               }
            }

            else if ((mod_id == HOLYWARS_DLL) &&
                     (strcmp("halo", item_name) == 0))
            {
               // make sure halo isn't owned by somebody
               if (pent->v.owner == NULL)
               {
                  can_pickup = TRUE;
               }
            }

         }  // end if object is visible
      }  // end else not "func_" entity

      if (can_pickup) // if the bot found something it can pickup...
      {
         float distance = (entity_origin - pEdict->v.origin).Length( );

         // see if it's the closest item so far...
         if (distance < min_distance)
         {
            min_distance = distance;        // update the minimum distance
            pPickupEntity = pent;        // remember this entity
            pickup_origin = entity_origin;  // remember location of entity
         }
      }
   }  // end while loop

   if (pPickupEntity != NULL)
   {
      // let's head off toward that item...
      Vector v_item = pickup_origin - pEdict->v.origin;

      Vector bot_angles = UTIL_VecToAngles( v_item );

      pEdict->v.ideal_yaw = bot_angles.y;

      BotFixIdealYaw(pEdict);

      pBot->pBotPickupItem = pPickupEntity;  // save the item bot is trying to get
   }
}


bool BotLookForMedic( bot_t *pBot )
{
   int i;
   Vector vecEnd;
   edict_t *pEdict = pBot->pEdict;

   if (mod_id == TFC_DLL)
   {
      // search the world for players...
      for (i = 1; i <= gpGlobals->maxClients; i++)
      {
         edict_t *pPlayer = INDEXENT(i);

         // skip invalid players and skip self (i.e. this bot)
         if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict))
         {
            // skip this player if not alive (i.e. dead or dying)
            if (!IsAlive(pPlayer))
               continue;

            int player_team = UTIL_GetTeam(pPlayer);
            int bot_team = UTIL_GetTeam(pEdict);

            // don't look for your enemies...
            if ((bot_team != player_team) &&
                !(team_allies[bot_team] & (1<<player_team)))
               continue;

            // check if the player is a medic...
            if (pPlayer->v.playerclass != TFC_CLASS_MEDIC)
               continue;  // continue if player not a medic

            vecEnd = pPlayer->v.origin + pPlayer->v.view_ofs;

            // see if bot can see the player...
            if (FInViewCone( &vecEnd, pEdict ) &&
                FVisible( vecEnd, pEdict ))
            {
               float distance = (pPlayer->v.origin - pEdict->v.origin).Length();

               if (distance < 1000)
               {
                  return TRUE;
               }
            }
         }
      }
   }

   return FALSE;
}


bool BotLookForGrenades( bot_t *pBot )
{
   edict_t *pent;
   Vector entity_origin;
   float radius = 500;
   char classname[40];
   edict_t *pEdict = pBot->pEdict;

   pent = NULL;
   while ((pent = UTIL_FindEntityInSphere( pent, pEdict->v.origin, radius )) != NULL)
   {
      strcpy(classname, STRING(pent->v.classname));

      entity_origin = pent->v.origin;

      if (mod_id == VALVE_DLL)
      {
         if (FInViewCone( &entity_origin, pEdict ) &&
             FVisible( entity_origin, pEdict ))
         {
            if (strcmp("grenade", classname) == 0)
               return TRUE;
            if (strcmp("monster_satchel", classname) == 0)
               return TRUE;
            if (strcmp("monster_snark", classname) == 0)
               return TRUE;
         }
      }
      else if (mod_id == TFC_DLL)
      {
         if (FInViewCone( &entity_origin, pEdict ) &&
             FVisible( entity_origin, pEdict ))
         {
            if (strcmp("tf_weapon_normalgrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_nailgrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_mirvgrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_concussiongrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_napalmgrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_gasgrenade", classname) == 0)
               return TRUE;
            if (strcmp("tf_weapon_empgrenade", classname) == 0)
               return TRUE;
         }
      }
      else if (mod_id == FRONTLINE_DLL)
      {
         if (FInViewCone( &entity_origin, pEdict ) &&
             FVisible( entity_origin, pEdict ))
         {
            if (strcmp("grenade", classname) == 0)
               return TRUE;
         }
      }
      else
      {
         return FALSE;  // all other non-supported MODs
      }
   }

   return FALSE;  // no grenades were found
}


void BotThink( bot_t *pBot )
{
   int index = 0;
   Vector v_diff;             // vector from previous to current location
   float pitch_degrees;
   float yaw_degrees;
   float moved_distance;      // length of v_diff vector (distance bot moved)
   TraceResult tr;
   bool found_waypoint;
   bool is_idle;
   float f_strafe_speed;
   bool bCrouchJump;
   char chat_text[81];
   char chat_name[64];
   char temp_name[64];
   const char *bot_name;

   edict_t *pEdict = pBot->pEdict;


   pEdict->v.flags |= FL_THIRDPARTYBOT;

   if (pBot->name[0] == 0)  // name filled in yet?
      strcpy(pBot->name, STRING(pBot->pEdict->v.netname));


// TheFatal - START from Advanced Bot Framework (Thanks Rich!)

   // adjust the millisecond delay based on the frame rate interval...
   if (pBot->msecdel <= gpGlobals->time)
   {
      pBot->msecdel = gpGlobals->time + 0.5;
      if (pBot->msecnum > 0)
         pBot->msecval = 450.0/pBot->msecnum;
      pBot->msecnum = 0;
   }
   else
      pBot->msecnum++;

   if (pBot->msecval < 1) pBot->msecval = 1;
   if (pBot->msecval > 100) pBot->msecval = 100;

// TheFatal - END

   pBot->f_frame_time = pBot->msecval / 1000;  // calculate frame time

   // Tactical NN Evaluation
   if (pBot->nn_initialized && gpGlobals->time >= pBot->f_next_tactical_nn_eval_time && IsAlive(pBot->pEdict) && !pBot->not_started) {
       pBot->f_next_tactical_nn_eval_time = gpGlobals->time + TACTICAL_NN_EVAL_INTERVAL;

       float nn_inputs[NN_INPUT_SIZE];
       PrepareNNInputs(pBot, nn_inputs);

       float nn_outputs[NUM_TACTICAL_DIRECTIVES];
       NN_FeedForward(&pBot->tactical_nn, nn_inputs, nn_outputs);

       TacticalDirective chosen_directive = NN_GetBestDirective(nn_outputs, NUM_TACTICAL_DIRECTIVES);
       pBot->last_chosen_directive_for_fitness_eval = chosen_directive; // Store for fitness evaluation

       if (pBot->pEdict && pBot->name[0] != '\0') { // Ensure pEdict and name are valid
            char log_msg[128];
            sprintf(log_msg, "Bot %s (T%d,C%d) TacNN: %s\n", pBot->name, pBot->bot_team, pBot->bot_class, TacticalDirectiveToString(chosen_directive));
            if (IsDedicatedServer) SERVER_PRINT(log_msg);
            else if (listenserver_edict) ClientPrint(listenserver_edict, HUD_PRINTCONSOLE, log_msg);
       }


       // Optional Basic Action Link:
       // This provides a simple link from NN output to the bot's objective selection logic.
       if (pBot->current_discovered_objective_id == -1 || (gpGlobals->time - pBot->last_objective_selection_time > OBJECTIVE_SELECTION_COOLDOWN / 2.0f) ) {
           int target_obj_gts_idx = -1; // Index within g_tactical_state.objective_points

           if (chosen_directive == TACTICS_ATTACK_OBJECTIVE_1 && GetGlobalTacticalState().num_valid_objectives > 0) target_obj_gts_idx = 0;
           else if (chosen_directive == TACTICS_ATTACK_OBJECTIVE_2 && GetGlobalTacticalState().num_valid_objectives > 1) target_obj_gts_idx = 1;
           else if (chosen_directive == TACTICS_ATTACK_OBJECTIVE_3 && GetGlobalTacticalState().num_valid_objectives > 2) target_obj_gts_idx = 2;
           // TODO: Add similar logic for TACTICS_DEFEND_OBJECTIVE_X

           if (target_obj_gts_idx != -1) {
               const GlobalTacticalState_t& ts = GetGlobalTacticalState();
               if (target_obj_gts_idx < ts.num_valid_objectives) { // Bounds check
                    pBot->current_discovered_objective_id = ts.objective_points[target_obj_gts_idx].id;
                    pBot->current_objective_desirability = 1.5f; // High desirability as NN chose it
                    pBot->last_objective_selection_time = gpGlobals->time;
                    pBot->is_interacting_with_objective = false; // Reset interaction state
               }
           }
       }
   }
   
   pEdict->v.button = 0;
   pBot->f_move_speed = 0.0;

   // if the bot hasn't selected stuff to start the game yet, go do that...
   if (pBot->not_started)
   {
      BotStartGame( pBot );

      g_engfuncs.pfnRunPlayerMove( pEdict, pEdict->v.v_angle, pBot->f_move_speed,
                                   0, 0, pEdict->v.button, 0, pBot->msecval);

      return;
   }

// does bot need to say a message and time to say a message?
   if ((pBot->b_bot_say) && (pBot->f_bot_say < gpGlobals->time))
   {
      pBot->b_bot_say = FALSE;

      UTIL_HostSay(pEdict, 0, pBot->bot_say_msg);
   }

   // if the bot is dead, randomly press fire to respawn...
   if ((pEdict->v.health < 1) || (pEdict->v.deadflag != DEAD_NO))
   {
      if (pBot->need_to_initialize)
      {
         BotSpawnInit(pBot);

         pBot->need_to_initialize = FALSE;

         // Call advanced chat handler for being killed by an enemy
         if (pBot->killer_edict != NULL && !FNullEnt(pBot->killer_edict) && bot_advanced_chat_enable.value > 0) {
             // Check if the killer is not the bot itself (e.g. suicide, world)
             if (pBot->killer_edict != pBot->pEdict) {
                // Check if killer is not a teammate (if teamplay is on)
                bool is_teamkill = false;
                if (is_team_play && UTIL_GetTeam(pBot->killer_edict) == pBot->bot_team && pBot->killer_edict != pBot->pEdict) {
                    is_teamkill = true;
                }

                if (is_teamkill) {
                    AdvancedChat_HandleGameEvent(pBot, CHAT_EVENT_WAS_KILLED_BY_TEAMMATE, pBot->killer_edict, NULL, NULL);
                } else {
                    AdvancedChat_HandleGameEvent(pBot, CHAT_EVENT_WAS_KILLED_BY_ENEMY, pBot->killer_edict, NULL, NULL);
                }
             }
         }

         // did another player kill this bot AND bot whine messages loaded AND
         // has the bot been alive for at least 15 seconds AND (original HPB whine logic)
         if ((pBot->killer_edict != NULL) && (bot_whine_count > 0) &&
             ((pBot->f_bot_spawn_time + 15.0) <= gpGlobals->time))
         {
            int whine_index;
            bool used;
            int i, recent_count;

            if ((RANDOM_LONG(1,100) <= pBot->whine_percent))
            {
               // set chat flag and time to chat...
               pBot->b_bot_say = TRUE;
               pBot->f_bot_say = gpGlobals->time + 5.0 + RANDOM_FLOAT(0.0, 5.0);

               recent_count = 0;

               while (recent_count < 5)
               {
                  whine_index = RANDOM_LONG(0, bot_whine_count-1);

                  used = FALSE;

                  for (i=0; i < 5; i++)
                  {
                     if (recent_bot_whine[i] == whine_index)
                        used = TRUE;
                  }

                  if (used)
                     recent_count++;
                  else
                     break;
               }

               for (i=4; i > 0; i--)
                  recent_bot_whine[i] = recent_bot_whine[i-1];

               recent_bot_whine[0] = whine_index;

               if (bot_whine[whine_index].can_modify)
                  BotChatText(bot_whine[whine_index].text, chat_text);
               else
                  strcpy(chat_text, bot_whine[whine_index].text);

               if (pBot->killer_edict->v.netname)
               {
                  strncpy(temp_name, STRING(pBot->killer_edict->v.netname), 31);
                  temp_name[31] = 0;

                  BotChatName(temp_name, chat_name);
               }
               else
                  strcpy(chat_name, "NULL");

               bot_name = STRING(pEdict->v.netname);

               BotChatFillInName(pBot->bot_say_msg, chat_text, chat_name, bot_name);
            }
         }
      }

      if (RANDOM_LONG(1, 100) > 50)
         pEdict->v.button = IN_ATTACK;

      g_engfuncs.pfnRunPlayerMove( pEdict, pEdict->v.v_angle, pBot->f_move_speed,
                                   0, 0, pEdict->v.button, 0, pBot->msecval);

      return;
   }

      if ((bot_chat_count > 0) && (pBot->f_bot_chat_time < gpGlobals->time))
   {
      pBot->f_bot_chat_time = gpGlobals->time + 30.0;

      if (RANDOM_LONG(1,100) <= pBot->chat_percent)
      {
         int chat_index;
         bool used;
         int i, recent_count;

         // set chat flag and time to chat...
         pBot->b_bot_say = TRUE;
         pBot->f_bot_say = gpGlobals->time + 5.0 + RANDOM_FLOAT(0.0, 5.0);

         recent_count = 0;

         while (recent_count < 5)
         {
            chat_index = RANDOM_LONG(0, bot_chat_count-1);

            used = FALSE;

            for (i=0; i < 5; i++)
            {
               if (recent_bot_chat[i] == chat_index)
                  used = TRUE;
            }

            if (used)
               recent_count++;
            else
               break;
         }

         for (i=4; i > 0; i--)
            recent_bot_chat[i] = recent_bot_chat[i-1];

         recent_bot_chat[0] = chat_index;

         if (bot_chat[chat_index].can_modify)
            BotChatText(bot_chat[chat_index].text, chat_text);
         else
            strcpy(chat_text, bot_chat[chat_index].text);

         strcpy(chat_name, STRING(pBot->pEdict->v.netname));

         bot_name = STRING(pEdict->v.netname);

         BotChatFillInName(pBot->bot_say_msg, chat_text, chat_name, bot_name);
      }
   }
   // N-gram based idle chat addition
   else if (IsAlive(pEdict) && // Only try to chat if alive
            bot_advanced_chat_enable.value > 0 &&
            bot_ngram_chat_enable.value > 0 &&
            g_chat_ngram_model.n_value > 0 && !g_chat_ngram_model.model_data.empty() &&
            RANDOM_LONG(1, 100) <= (int)bot_ngram_chat_idle_frequency.value) {

       if (gpGlobals->time >= pBot->f_bot_chat_time) { // Respect general cooldown
           std::string seed_phrase = "";
           if (g_chat_ngram_model.n_value > 1 && RANDOM_LONG(0, 2) == 0) {
               const char* common_starters[] = {"the", "i", "it", "enemy", "team", "this", "that"}; // Added more
               seed_phrase = common_starters[RANDOM_LONG(0, (sizeof(common_starters)/sizeof(char*)) -1 )];
           }

           std::string ngram_sentence = AdvancedChat_GenerateNgramSentence(&g_chat_ngram_model, seed_phrase, 12);

           if (!ngram_sentence.empty() && ngram_sentence.length() < NGRAM_BOTTHINK_MAX_LEN && ngram_sentence.length() > 3) {
               // Use UTIL_HostSay directly, as b_bot_say and bot_say_msg are for the older system.
               // Ensure UTIL_HostSay is safe to call with potentially formatted strings if any placeholders were used.
               // For N-gram, usually raw text is fine.
               UTIL_HostSay(pEdict, 0, (char*)ngram_sentence.c_str());
               pBot->f_bot_chat_time = gpGlobals->time + RANDOM_FLOAT(NGRAM_BOTTHINK_MIN_INTERVAL, NGRAM_BOTTHINK_MAX_INTERVAL);
           }
       }
   }


   // set this for the next time the bot dies so it will initialize stuff
   if (pBot->need_to_initialize == FALSE)
   {
      pBot->need_to_initialize = TRUE;
      pBot->f_bot_spawn_time = gpGlobals->time;
   }

   is_idle = FALSE;

   if ((mod_id == FRONTLINE_DLL) && (pBot->round_end))
   {
      if (pBot->warmup)  // has warmup started (i.e. start of round?)
      {
         pBot->round_end = 0;

         BotSpawnInit(pBot);
      }

      is_idle = TRUE;
   }

   if ((mod_id == FRONTLINE_DLL) && (pBot->warmup) && (!pBot->defender))
   {
      if (pBot->curr_waypoint_index == -1)
      {
         // find the nearest visible waypoint
         int i = WaypointFindNearest(pEdict, REACHABLE_RANGE, pBot->defender);

         if (i != -1)
         {
            pBot->curr_waypoint_index = i;
            pBot->f_waypoint_time = gpGlobals->time;

            Vector v_direction = waypoints[i].origin - pEdict->v.origin;

            Vector bot_angles = UTIL_VecToAngles( v_direction );

            pBot->idle_angle = bot_angles.y;
         }
         else
            pBot->idle_angle = pEdict->v.v_angle.y;
      }

      is_idle = TRUE;
   }

   // is a vote in progress and it's time to vote now?
   if ((mod_id == HOLYWARS_DLL) && (pBot->vote_in_progress) &&
       (pBot->f_vote_time <= gpGlobals->time))
   {
      pBot->vote_in_progress = FALSE;

      if (RANDOM_LONG(1, 100) <= 80)  // only vote 80% of the time
      {
         if (RANDOM_LONG(1, 100) <= 50)
            FakeClientCommand(pEdict, "vote_yes", NULL, NULL);
         else
            FakeClientCommand(pEdict, "vote_no", NULL, NULL);
      }

      pBot->f_start_vote_time = gpGlobals->time + RANDOM_LONG(120, 600);
   }

   if ((mod_id == HOLYWARS_DLL) &&
       (pBot->f_start_vote_time < gpGlobals->time))
   {
      if (holywars_gamemode == 0)  // currently deathmatch?
          FakeClientCommand(pEdict, "callvote_instagib", NULL, NULL);
      else
          FakeClientCommand(pEdict, "callvote_deathmatch", NULL, NULL);

      pBot->f_start_vote_time = gpGlobals->time + RANDOM_LONG(120, 600);
   }

   if (pBot->blinded_time > gpGlobals->time)
   {
      is_idle = TRUE;  // don't do anything while blinded
   }

   if (is_idle)
   {
      if (pBot->idle_angle_time <= gpGlobals->time)
      {
         pBot->idle_angle_time = gpGlobals->time + RANDOM_FLOAT(0.5, 2.0);

         pEdict->v.ideal_yaw = pBot->idle_angle + RANDOM_FLOAT(0.0, 60.0) - 30.0;

         BotFixIdealYaw(pEdict);
      }

      // turn towards ideal_yaw by yaw_speed degrees (slower than normal)
      BotChangeYaw( pBot, pEdict->v.yaw_speed / 2 );

      g_engfuncs.pfnRunPlayerMove( pEdict, pEdict->v.v_angle, pBot->f_move_speed,
                                   0, 0, pEdict->v.button, 0, pBot->msecval);

      return;
   }
   else
   {
      pBot->idle_angle = pEdict->v.v_angle.y;
   }

   // check if time to check for player sounds (if don't already have enemy)
   if ((pBot->f_sound_update_time <= gpGlobals->time) &&
       (pBot->pBotEnemy == NULL))
   {
      int ind;
      edict_t *pPlayer;

      pBot->f_sound_update_time = gpGlobals->time + 1.0;

      for (ind = 1; ind <= gpGlobals->maxClients; ind++)
      {
         pPlayer = INDEXENT(ind);

         // is this player slot is valid and it's not this bot...
         if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict))
         {
            // if observer mode enabled, don't listen to this player...
            if ((b_observer_mode) && !(pPlayer->v.flags & FL_FAKECLIENT) && !(pPlayer->v.flags & FL_THIRDPARTYBOT))
               continue;

            if (IsAlive(pPlayer) &&
                (FBitSet(pPlayer->v.flags, FL_CLIENT)))
            {
               // check for sounds being made by other players...
               if (UpdateSounds(pEdict, pPlayer))
               {
                  // don't check for sounds for another 30 seconds
                  pBot->f_sound_update_time = gpGlobals->time + 30.0;
               }
            }
         }
      }
   }

   pBot->f_move_speed = pBot->f_max_speed;  // set to max speed

   if (pBot->f_speed_check_time <= gpGlobals->time)
   {
      // see how far bot has moved since the previous position...
      v_diff = pBot->v_prev_origin - pEdict->v.origin;
      moved_distance = v_diff.Length();

      // save current position as previous
      pBot->v_prev_origin = pEdict->v.origin;
      pBot->f_speed_check_time = gpGlobals->time + 0.2;
   }
   else
   {
      moved_distance = 2.0;
   }

   // if the bot is not underwater AND not in the air (or on ladder),
   // check if the bot is about to fall off high ledge or into water...
   if ((pEdict->v.waterlevel != 3) &&
       (pEdict->v.flags & FL_ONGROUND) &&
       (pEdict->v.movetype != MOVETYPE_FLY) &&
       (pBot->f_drop_check_time < gpGlobals->time))
   {
      pBot->f_drop_check_time = gpGlobals->time + 0.05;

      BotLookForDrop( pBot );
   }

   // turn towards ideal_pitch by pitch_speed degrees
   pitch_degrees = BotChangePitch( pBot, pEdict->v.pitch_speed );

   // turn towards ideal_yaw by yaw_speed degrees
   yaw_degrees = BotChangeYaw( pBot, pEdict->v.yaw_speed );

   if ((pitch_degrees >= 45) || (yaw_degrees >= 45))
   {
      pBot->f_move_speed = 0.0;  // don't move while turning a lot
   }
   else if ((pitch_degrees >= 30) || (yaw_degrees >= 30))
   {
      pBot->f_move_speed = pBot->f_move_speed / 4;  // slow down while turning
   }
   else if ((pitch_degrees >= 20) || (yaw_degrees >= 20))
   {
      pBot->f_move_speed = pBot->f_move_speed / 3;  // slow down while turning
   }

//   else  // else handle movement related actions...

   {
      if (b_botdontshoot == 0)
      {
         if ((mod_id == TFC_DLL) && (pBot->bot_has_flag == TRUE))
         {
            // is it time to check whether bot should look for enemies yet?
            if (pBot->f_bot_find_enemy_time <= gpGlobals->time)
            {
               pBot->f_bot_find_enemy_time = gpGlobals->time + 5.0;

               if (RANDOM_LONG(1, 100) <= 40)
                  pBot->pBotEnemy = BotFindEnemy( pBot );
            }
         }
         else
         {
            pBot->pBotEnemy = BotFindEnemy( pBot );
         }
      }
      else
         pBot->pBotEnemy = NULL;  // clear enemy pointer (no ememy for you!)

      if (pBot->pBotEnemy != NULL)  // does an enemy exist?
      {
         BotShootAtEnemy( pBot );  // shoot at the enemy

         pBot->f_pause_time = 0;  // dont't pause if enemy exists
      }

      else if (pBot->f_pause_time > gpGlobals->time)  // is bot "paused"?
      {
         // you could make the bot look left then right, or look up
         // and down, to make it appear that the bot is hunting for
         // something (don't do anything right now)
      }

      // is bot being "used" and can still follow "user"?
      else if ((pBot->pBotUser != NULL) && BotFollowUser( pBot ))
      {
         // do nothing here!
         ;
      }

      else
      {
         // no enemy, let's just wander around...

           // If interacting, handle that (this comes before selecting new objective)
           if (pBot->is_interacting_with_objective) {
               pBot->f_move_speed = 0; // Stand still while interacting
               pBot->pEdict->v.button |= IN_USE; // Hold +use
               if (gpGlobals->time >= pBot->interaction_timer) {
                   pBot->is_interacting_with_objective = false; // Interaction done
                   pBot->current_discovered_objective_id = -1; // Clear objective to allow re-evaluation
                   pBot->current_objective_desirability = 0.0f;
                   pBot->last_objective_selection_time = gpGlobals->time; // Allow new selection soon
                   // ALERT(at_console, "Bot %s finished interaction attempt.\n", pBot->name);
               }
           }
           // If not interacting, and not paused, then consider objectives or waypoints
           else if (pBot->f_pause_time <= gpGlobals->time)
           {
               BotSelectAndPursueDiscoveredObjective(pBot); // THIS IS THE NEW CALL

               // Existing waypoint logic will use pBot->waypoint_goal / pBot->waypoint_origin
               found_waypoint = FALSE;
               if ((pBot->pBotPickupItem == NULL) &&
                   (pBot->f_look_for_waypoint_time <= gpGlobals->time) &&
                   (num_waypoints != 0 || pBot->current_discovered_objective_id != -1))
               {
                   // Check if near current discovered objective's location
                   if (pBot->current_discovered_objective_id != -1 && !pBot->is_interacting_with_objective) { // Don't re-check if already decided to interact this frame
                       CandidateObjective_t* pObj = GetCandidateObjectiveById(pBot->current_discovered_objective_id);
                       if (pObj) {
                           float dist_sq_to_obj_loc = (pObj->location - pBot->pEdict->v.origin).LengthSquared();
                           if (dist_sq_to_obj_loc < (64.0f * 64.0f)) { // Within 64 units
                               if (pObj->learned_activation_method == ACT_TOUCH ||
                                   pObj->learned_activation_method == ACT_USE ||
                                   pObj->learned_activation_method == ACT_UNKNOWN) {

                                   pBot->is_interacting_with_objective = true;
                                   pBot->interaction_timer = gpGlobals->time + INTERACTION_DURATION;
                                   pBot->f_move_speed = 0;
                                   pBot->pEdict->v.button |= IN_USE;
                                   // ALERT(at_console, "Bot %s starting interaction with obj ID %d\n", pBot->name, pObj->unique_id);
                               } else {
                                    pBot->current_discovered_objective_id = -1;
                                    pBot->current_objective_desirability = 0.0f;
                               }
                               pBot->last_objective_selection_time = gpGlobals->time;
                           }
                       }
                   }

                   if (!pBot->is_interacting_with_objective) { // Only navigate if not currently interacting
                        found_waypoint = BotHeadTowardWaypoint(pBot);
                   } else {
                        found_waypoint = true; // تعتبر متجهة نحو الهدف اثناء التفاعل
                   }
               }
            // took too long trying to spray logo?...
         if ((pBot->b_spray_logo) &&
             ((pBot->f_spray_logo_time + 3.0) < gpGlobals->time))
         {
            pBot->b_spray_logo = FALSE;
            pEdict->v.idealpitch = 0.0f;
         }

         if (pBot->b_spray_logo)  // trying to spray a logo?
         {
            Vector v_src, v_dest, angle;
            TraceResult tr;

            // find the nearest wall to spray logo on (or floor)...

            angle = pEdict->v.v_angle;
            angle.x = 0;  // pitch is level horizontally

            MAKE_VECTORS( angle );

            v_src = pEdict->v.origin + pEdict->v.view_ofs;
            v_dest = v_src + gpGlobals->v_forward * 100;

            UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                            pEdict->v.pContainingEntity, &tr);

            if (tr.flFraction < 1.0)
            {
               // already facing the correct yaw, just set pitch...
               pEdict->v.idealpitch = RANDOM_FLOAT(0.0, 30.0) - 15.0;
            }
            else
            {
               v_dest = v_src + gpGlobals->v_right * 100;  // to the right

               UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                               pEdict->v.pContainingEntity, &tr);

               if (tr.flFraction < 1.0)
               {
                  // set the ideal yaw and pitch...
                  Vector bot_angles = UTIL_VecToAngles(v_dest - v_src);

                  pEdict->v.ideal_yaw = bot_angles.y;

                  BotFixIdealYaw(pEdict);

                  pEdict->v.idealpitch = RANDOM_FLOAT(0.0, 30.0) - 15.0;
               }
               else
               {
                  v_dest = v_src + gpGlobals->v_right * -100;  // to the left

                  UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                                  pEdict->v.pContainingEntity, &tr);

                  if (tr.flFraction < 1.0)
                  {
                     // set the ideal yaw and pitch...
                     Vector bot_angles = UTIL_VecToAngles(v_dest - v_src);

                     pEdict->v.ideal_yaw = bot_angles.y;

                     BotFixIdealYaw(pEdict);

                     pEdict->v.idealpitch = RANDOM_FLOAT(0.0, 30.0) - 15.0;
                  }
                  else
                  {
                     v_dest = v_src + gpGlobals->v_forward * -100;  // behind

                     UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                                     pEdict->v.pContainingEntity, &tr);

                     if (tr.flFraction < 1.0)
                     {
                        // set the ideal yaw and pitch...
                        Vector bot_angles = UTIL_VecToAngles(v_dest - v_src);

                        pEdict->v.ideal_yaw = bot_angles.y;

                        BotFixIdealYaw(pEdict);

                        pEdict->v.idealpitch = RANDOM_FLOAT(0.0, 30.0) - 15.0;
                     }
                     else
                     {
                        // on the ground...

                        angle = pEdict->v.v_angle;
                        angle.x = 85.0f;  // 85 degrees is downward

                        MAKE_VECTORS( angle );

                        v_src = pEdict->v.origin + pEdict->v.view_ofs;
                        v_dest = v_src + gpGlobals->v_forward * 80;

                        UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                                        pEdict->v.pContainingEntity, &tr);

                        if (tr.flFraction < 1.0)
                        {
                           // set the pitch...

                           pEdict->v.idealpitch = 85.0f;

                           BotFixIdealPitch(pEdict);
                        }
                     }
                  }
               }
            }

            pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;

            // is there a wall close to us?

            MAKE_VECTORS( pEdict->v.v_angle );

            v_src = pEdict->v.origin + pEdict->v.view_ofs;
            v_dest = v_src + gpGlobals->v_forward * 80;

            UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                            pEdict->v.pContainingEntity, &tr);

            if (tr.flFraction < 1.0)
            {
               BotSprayLogo(pEdict, pBot->logo_name);

               pBot->b_spray_logo = FALSE;

               pEdict->v.idealpitch = 0.0f;
            }
         }

         if ((pEdict->v.waterlevel != 2) &&  // is bot NOT under water?
             (pEdict->v.waterlevel != 3) &&
             (!pBot->b_spray_logo))          // AND not trying to spray a logo
         {
            // reset pitch to 0 (level horizontally)
            pEdict->v.idealpitch = 0;
            pEdict->v.v_angle.x = 0;
         }

         // check if bot should look for items now or not...
         if (pBot->f_find_item <= gpGlobals->time)
         {
            pBot->f_find_item = gpGlobals->time + 0.1;

            BotFindItem( pBot );  // see if there are any visible items
         }

         // check if bot sees a tripmine...
         if (pBot->b_see_tripmine)
         {
            // check if bot can shoot the tripmine...
            if ((pBot->b_shoot_tripmine) && BotShootTripmine( pBot ))
            {
               // shot at tripmine, may or may not have hit it, clear
               // flags anyway, next BotFindItem will see it again if
               // it is still there...

               pBot->b_shoot_tripmine = FALSE;
               pBot->b_see_tripmine = FALSE;

               // pause for a while to allow tripmine to explode...
               pBot->f_pause_time = gpGlobals->time + 0.5;
            }
            else  // run away!!!
            {
               Vector tripmine_angles;

               tripmine_angles = UTIL_VecToAngles( pBot->v_tripmine - pEdict->v.origin );

               // face away from the tripmine
               pEdict->v.ideal_yaw += 180;  // rotate 180 degrees

               BotFixIdealYaw(pEdict);

               pBot->b_see_tripmine = FALSE;

               pBot->f_move_speed = 0;  // don't run while turning
            }
         }

         // check if should use wall mounted health station...
         else if (pBot->b_use_health_station)
         {
            if ((pBot->f_use_health_time + 10.0) > gpGlobals->time)
            {
               pBot->f_move_speed = 0;  // don't move while using health station

               pEdict->v.button = IN_USE;
            }
            else
            {
               // bot is stuck trying to "use" a health station...

               pBot->b_use_health_station = FALSE;

               // don't look for items for a while since the bot
               // could be stuck trying to get to an item
               pBot->f_find_item = gpGlobals->time + 0.5;
            }
         }

         // check if should use wall mounted HEV station...
         else if (pBot->b_use_HEV_station)
         {
            if ((pBot->f_use_HEV_time + 10.0) > gpGlobals->time)
            {
               pBot->f_move_speed = 0;  // don't move while using HEV station

               pEdict->v.button = IN_USE;
            }
            else
            {
               // bot is stuck trying to "use" a HEV station...

               pBot->b_use_HEV_station = FALSE;

               // don't look for items for a while since the bot
               // could be stuck trying to get to an item
               pBot->f_find_item = gpGlobals->time + 0.5;
            }
         }

         // check if should capture a point by using it...
         else if (pBot->b_use_capture)
         {
            int team = UTIL_GetTeam(pEdict);  // skin and team must match

            // still capturing and hasn't captured yet...
            if ((pBot->f_use_capture_time > gpGlobals->time) &&
                (pBot->pCaptureEdict->v.skin == (1 - team)))
            {
               pBot->f_move_speed = 0;  // don't move while capturing

               pEdict->v.button = IN_USE;
            }
            else
            {
               // bot is stuck trying to "use" a capture point...

               pBot->b_use_capture = FALSE;
               pBot->f_use_capture_time = 0.0;

               // don't look for items for a while since the bot
               // could be stuck trying to get to an item
               pBot->f_find_item = gpGlobals->time + 0.5;
            }
         }

         else if (pBot->b_build_sentrygun)
         {
            if (pBot->sentrygun_level == 0)
            {
               pBot->f_move_speed = 0;  // don't move while building

               // check if the bot is facing the correct direction yet...
               if (yaw_degrees <= 1.0)
               {
                  FakeClientCommand(pEdict, "build", "2", NULL);

                  pBot->b_build_sentrygun = FALSE;

                  pBot->f_engineer_build_time = gpGlobals->time + 20.0;
                  pBot->f_pause_time = gpGlobals->time + 5.0;
               }
            }
            else  // need to "attack" this sentrygun to upgrade it...
            {
               edict_t *pent = NULL;

               pBot->b_build_sentrygun = FALSE;

               while ((pent = UTIL_FindEntityInSphere( pent, pEdict->v.origin, 150.0 )) != NULL)
               {
                  if (strcmp(STRING(pent->v.classname), "building_sentrygun") == 0)
                  {
                     pBot->pBotEnemy = pent;

                     pBot->enemy_attack_count = 3;  // give it 3 whacks
                     pBot->f_engineer_build_time = gpGlobals->time + 20.0;

                     // face the enemy
                     Vector v_enemy = pent->v.origin - pEdict->v.origin;
                     Vector bot_angles = UTIL_VecToAngles( v_enemy );

                     pEdict->v.ideal_yaw = bot_angles.y;

                     BotFixIdealYaw(pEdict);
                  }
               }
            }

            pBot->curr_waypoint_index = -1;  // forget this waypoint
         }

         else if (pBot->b_build_dispenser)
         {
            if (pBot->dispenser_built == 0)
            {
               pBot->f_move_speed = 0;  // don't move while building

               // check if the bot is facing the correct direction yet...
               if (yaw_degrees <= 1.0)
               {
                  FakeClientCommand(pEdict, "build", "1", NULL);

                  pBot->b_build_dispenser = FALSE;

                  pBot->f_engineer_build_time = gpGlobals->time + 20.0;
                  pBot->f_pause_time = gpGlobals->time + 3.0;
               }
            }
            else  // need to "attack" this dispenser to refill it...
            {
               edict_t *pent = NULL;

               pBot->b_build_dispenser = FALSE;

               while ((pent = UTIL_FindEntityInSphere( pent, pEdict->v.origin, 150.0 )) != NULL)
               {
                  if (strcmp(STRING(pent->v.classname), "building_dispenser") == 0)
                  {
                     pBot->pBotEnemy = pent;

                     pBot->enemy_attack_count = 3;  // give it 3 whacks
                     pBot->f_engineer_build_time = gpGlobals->time + 20.0;

                     // face the enemy
                     Vector v_enemy = pent->v.origin - pEdict->v.origin;
                     Vector bot_angles = UTIL_VecToAngles( v_enemy );

                     pEdict->v.ideal_yaw = bot_angles.y;

                     BotFixIdealYaw(pEdict);
                  }
               }
            }

            pBot->curr_waypoint_index = -1;  // forget this waypoint
         }

         else if (pBot->b_use_button)
         {
            pBot->f_move_speed = 0;  // don't move while using elevator

            BotUseLift( pBot, moved_distance );
         }

         else
         {
            if (pEdict->v.waterlevel == 3)  // check if the bot is underwater...
            {
               BotUnderWater( pBot );
            }

            found_waypoint = FALSE;

            // if the bot is not trying to get to something AND
            // it is time to look for a waypoint AND
            // there are waypoints in this level...

            if ((pBot->pBotPickupItem == NULL) &&
                (pBot->f_look_for_waypoint_time <= gpGlobals->time) &&
                (num_waypoints != 0))
            {
               found_waypoint = BotHeadTowardWaypoint(pBot);
            }

            // check if the bot is on a ladder...
            if (pEdict->v.movetype == MOVETYPE_FLY)
            {
               // check if bot JUST got on the ladder...
               if ((pBot->f_end_use_ladder_time + 1.0) < gpGlobals->time)
                  pBot->f_start_use_ladder_time = gpGlobals->time;

               // go handle the ladder movement
               BotOnLadder( pBot, moved_distance );

               pBot->f_dont_avoid_wall_time = gpGlobals->time + 2.0;
               pBot->f_end_use_ladder_time = gpGlobals->time;
            }
            else
            {
               // check if the bot JUST got off the ladder...
               if ((pBot->f_end_use_ladder_time + 1.0) > gpGlobals->time)
               {
                  pBot->ladder_dir = LADDER_UNKNOWN;
               }
            }

            // if the bot isn't headed toward a waypoint...
            if (found_waypoint == FALSE)
            {
               TraceResult tr;

               // check if we should be avoiding walls
               if (pBot->f_dont_avoid_wall_time <= gpGlobals->time)
               {
                  // let's just randomly wander around
                  if (BotStuckInCorner( pBot ))
                  {
                     pEdict->v.ideal_yaw += 180;  // turn 180 degrees

                     BotFixIdealYaw(pEdict);

                     pBot->f_move_speed = 0;  // don't move while turning
                     pBot->f_dont_avoid_wall_time = gpGlobals->time + 1.0;

                     moved_distance = 2.0;  // dont use bot stuck code
                  }
                  else
                  {
                     // check if there is a wall on the left...
                     if (!BotCheckWallOnLeft( pBot ))
                     {
                        // if there was a wall on the left over 1/2 a second ago then
                        // 20% of the time randomly turn between 45 and 60 degrees

                        if ((pBot->f_wall_on_left != 0) &&
                            (pBot->f_wall_on_left <= gpGlobals->time - 0.5) &&
                            (RANDOM_LONG(1, 100) <= 20))
                        {
                           pEdict->v.ideal_yaw += RANDOM_LONG(45, 60);

                           BotFixIdealYaw(pEdict);

                           pBot->f_move_speed = 0;  // don't move while turning
                           pBot->f_dont_avoid_wall_time = gpGlobals->time + 1.0;
                        }

                        pBot->f_wall_on_left = 0;  // reset wall detect time
                     }
                     else if (!BotCheckWallOnRight( pBot ))
                     {
                        // if there was a wall on the right over 1/2 a second ago then
                        // 20% of the time randomly turn between 45 and 60 degrees

                        if ((pBot->f_wall_on_right != 0) &&
                            (pBot->f_wall_on_right <= gpGlobals->time - 0.5) &&
                            (RANDOM_LONG(1, 100) <= 20))
                        {
                           pEdict->v.ideal_yaw -= RANDOM_LONG(45, 60);

                           BotFixIdealYaw(pEdict);

                           pBot->f_move_speed = 0;  // don't move while turning
                           pBot->f_dont_avoid_wall_time = gpGlobals->time + 1.0;
                        }

                        pBot->f_wall_on_right = 0;  // reset wall detect time
                     }
                  }
               }

               // check if bot is about to hit a wall.  TraceResult gets returned
               if ((pBot->f_dont_avoid_wall_time <= gpGlobals->time) &&
                   BotCantMoveForward( pBot, &tr ))
               {
                  // ADD LATER
                  // need to check if bot can jump up or duck under here...
                  // ADD LATER

                  BotTurnAtWall( pBot, &tr, TRUE );
               }
            }

            // check if bot is on a ladder and has been on a ladder for
            // more than 5 seconds...
            if ((pEdict->v.movetype == MOVETYPE_FLY) &&
                (pBot->f_start_use_ladder_time > 0.0) &&
                ((pBot->f_start_use_ladder_time + 5.0) <= gpGlobals->time))
            {
               // bot is stuck on a ladder...

               BotRandomTurn(pBot);

               // don't look for items for 2 seconds
               pBot->f_find_item = gpGlobals->time + 2.0;

               pBot->f_start_use_ladder_time = 0.0;  // reset start ladder time
            }

            // check if the bot hasn't moved much since the last location
            // (and NOT on a ladder since ladder stuck handled elsewhere)
            if ((moved_distance <= 1.0) && (pBot->f_prev_speed >= 1.0) &&
                (pEdict->v.movetype != MOVETYPE_FLY))
            {
               // the bot must be stuck!

               pBot->f_dont_avoid_wall_time = gpGlobals->time + 1.0;
               pBot->f_look_for_waypoint_time = gpGlobals->time + 1.0;

               if (BotCanJumpUp( pBot, &bCrouchJump ))  // can the bot jump onto something?
               {
                  if ((pBot->f_jump_time + 2.0) <= gpGlobals->time)
                  {
                     pBot->f_jump_time = gpGlobals->time;
                     pEdict->v.button |= IN_JUMP;  // jump up and move forward

                     if (bCrouchJump)
                        pEdict->v.button |= IN_DUCK;  // also need to duck
                  }
                  else
                  {
                     // bot already tried jumping less than two seconds ago, just turn
                     BotRandomTurn(pBot);
                  }
               }
               else if (BotCanDuckUnder( pBot ))  // can the bot duck under something?
               {
                  pEdict->v.button |= IN_DUCK;  // duck down and move forward
               }
               else
               {
                  BotRandomTurn(pBot);

                  // is the bot trying to get to an item?...
                  if (pBot->pBotPickupItem != NULL)
                  {
                     // don't look for items for a while since the bot
                     // could be stuck trying to get to an item
                     pBot->f_find_item = gpGlobals->time + 0.5;
                  }
               }
            }

            // should the bot pause for a while here?
            // (don't pause on ladders or while being "used"...
            if ((RANDOM_LONG(1, 1000) <= pause_frequency[pBot->bot_skill]) &&
                (pEdict->v.movetype != MOVETYPE_FLY) &&
                (pBot->pBotUser == NULL))
            {
               // set the time that the bot will stop "pausing"
               pBot->f_pause_time = gpGlobals->time +
                  RANDOM_FLOAT(pause_time[pBot->bot_skill][0],
                               pause_time[pBot->bot_skill][1]);
            }
         }
      }
   }

   if (pBot->curr_waypoint_index != -1)  // does the bot have a waypoint?
   {
      // check if the next waypoint is a door waypoint...
      if (waypoints[pBot->curr_waypoint_index].flags & W_FL_DOOR)
      {
         pBot->f_move_speed = pBot->f_max_speed / 3;  // slow down for doors
      }

      // check if the next waypoint is a ladder waypoint...
      if (waypoints[pBot->curr_waypoint_index].flags & W_FL_LADDER)
      {
         // check if the waypoint is at the top of a ladder AND
         // the bot isn't currenly on a ladder...
         if ((pBot->waypoint_top_of_ladder) &&
             (pEdict->v.movetype != MOVETYPE_FLY))
         {
            // is the bot on "ground" above the ladder?
            if (pEdict->v.flags & FL_ONGROUND)
            {
               float waypoint_distance = (pEdict->v.origin - pBot->waypoint_origin).Length();

               if (waypoint_distance <= 20.0)  // if VERY close...
                  pBot->f_move_speed = 20.0;  // go VERY slow
               else if (waypoint_distance <= 100.0)  // if fairly close...
                  pBot->f_move_speed = 50.0;  // go fairly slow

               pBot->ladder_dir = LADDER_DOWN;
            }
            else  // bot must be in mid-air, go BACKWARDS to touch ladder...
            {
               pBot->f_move_speed = -pBot->f_max_speed;
            }
         }
         else
         {
            // don't avoid walls for a while
            pBot->f_dont_avoid_wall_time = gpGlobals->time + 5.0;

            pBot->waypoint_top_of_ladder = FALSE;
         }
      }

      // check if the next waypoint is a crouch waypoint...
      if (waypoints[pBot->curr_waypoint_index].flags & W_FL_CROUCH)
         pEdict->v.button |= IN_DUCK;  // duck down while moving forward

      // check if the waypoint is a sniper waypoint AND
      // bot isn't currently aiming at an ememy...
      if ((waypoints[pBot->curr_waypoint_index].flags & W_FL_SNIPER) &&
          (pBot->pBotEnemy == NULL))
      {
         if ((mod_id != TFC_DLL) ||
             ((mod_id == TFC_DLL) && (pEdict->v.playerclass == TFC_CLASS_SNIPER)))
         {
            // check if the bot need to move back closer to the waypoint...

            float distance = (pEdict->v.origin - waypoints[pBot->curr_waypoint_index].origin).Length();

            if (distance > 40)
            {
               // turn towards the sniper waypoint and move there...

               Vector v_direction = waypoints[pBot->curr_waypoint_index].origin - pEdict->v.origin;

               Vector bot_angles = UTIL_VecToAngles( v_direction );

               pEdict->v.ideal_yaw = bot_angles.y;

               BotFixIdealYaw(pEdict);

               // go slow to prevent the "loop the loop" problem...
               pBot->f_move_speed = pBot->f_max_speed / 3;

               pBot->f_sniper_aim_time = 0.0;  // reset aiming time

               pEdict->v.v_angle.z = 0;  // reset roll to 0 (straight up and down)

               // set the body angles the same way the bot is looking/aiming
               pEdict->v.angles.x = -pEdict->v.v_angle.x / 3;
               pEdict->v.angles.y = pEdict->v.v_angle.y;
               pEdict->v.angles.z = pEdict->v.v_angle.z;

               // save the previous speed (for checking if stuck)
               pBot->f_prev_speed = pBot->f_move_speed;

               f_strafe_speed = 0.0;

               g_engfuncs.pfnRunPlayerMove( pEdict, pEdict->v.v_angle, pBot->f_move_speed,
                                            f_strafe_speed, 0, pEdict->v.button, 0, pBot->msecval);

               return;
            }

            // check if it's time to adjust aim yet...
            if (pBot->f_sniper_aim_time <= gpGlobals->time)
            {
               int aim_index;

               aim_index = WaypointFindNearestAiming(waypoints[pBot->curr_waypoint_index].origin);

               if (aim_index != -1)
               {
                  Vector v_aim = waypoints[aim_index].origin - waypoints[pBot->curr_waypoint_index].origin;

                  Vector aim_angles = UTIL_VecToAngles( v_aim );

                  aim_angles.y += RANDOM_LONG(0, 30) - 15;

                  pEdict->v.ideal_yaw = aim_angles.y;

                  BotFixIdealYaw(pEdict);
               }

               // don't adjust aim again until after a few seconds...
               pBot->f_sniper_aim_time = gpGlobals->time + RANDOM_FLOAT(3.0, 5.0);
            }
         }
      }

      // check if the waypoint is a sentry gun waypoint and this waypoint
      // is the bot's goal...
      if ((waypoints[pBot->curr_waypoint_index].flags & W_FL_SENTRYGUN) &&
          (pBot->waypoint_goal == pBot->curr_waypoint_index) &&
          (pEdict->v.playerclass == TFC_CLASS_ENGINEER))
      {
         // go slowly when approaching sentry gun waypoints
         pBot->f_move_speed = pBot->f_max_speed / 3;
      }

      // check if the waypoint is a dispenser waypoint and this waypoint
      // is the bot's goal...
      if ((waypoints[pBot->curr_waypoint_index].flags & W_FL_DISPENSER) &&
          (pBot->waypoint_goal == pBot->curr_waypoint_index) &&
          (pEdict->v.playerclass == TFC_CLASS_ENGINEER))
      {
         // go slowly when approaching dispenser waypoints
         pBot->f_move_speed = pBot->f_max_speed / 3;
      }
   }

   if (pBot->f_pause_time > gpGlobals->time)  // is the bot "paused"?
      pBot->f_move_speed = 0;  // don't move while pausing

   if (pBot->f_strafe_time < gpGlobals->time)  // time to strafe yet?
   {
      pBot->f_strafe_time = gpGlobals->time + RANDOM_FLOAT(0.1, 1.0);

      if (RANDOM_LONG(1, 100) <= pBot->strafe_percent)
      {
         if (RANDOM_LONG(1, 100) <= 50)
            pBot->f_strafe_direction = -1 * RANDOM_FLOAT(0.5, 1.0);
         else
            pBot->f_strafe_direction = RANDOM_FLOAT(0.5, 1.0);
      }
      else
         pBot->f_strafe_direction = 0.0;
   }

   if (pBot->f_duck_time > gpGlobals->time)
      pEdict->v.button |= IN_DUCK;  // need to duck (crowbar attack)

   if ((mod_id == TFC_DLL)  &&
       (pEdict->v.playerclass != TFC_CLASS_MEDIC))
   {
      if (pBot->f_medic_check_time <= gpGlobals->time)
      {
         pBot->f_medic_check_time = gpGlobals->time + 0.1;

         // is bot's health less than 50% AND bot can see a medic?
         if (((pEdict->v.health / pEdict->v.max_health) < 0.50) &&
             BotLookForMedic( pBot ))
         {
            if (RANDOM_LONG(1, 100) <= 50)
            {
               // yell "saveme" and stand still for 3.0 seconds
               FakeClientCommand(pEdict, "saveme", NULL, NULL);
               pBot->f_medic_pause_time = gpGlobals->time + 3.0;
            }

            pBot->f_medic_check_time = gpGlobals->time + 3.0;
         }
         else
         {
            // is the bot's health less than 20%?
            if ((pEdict->v.health / pEdict->v.max_health) < 0.20)
            {
               if ((RANDOM_LONG(1, 100) <= 20) &&
                   (pBot->f_medic_yell_time <= gpGlobals->time))
               {
                  // yell "saveme" in case there's a medic within earshot
                  FakeClientCommand(pEdict, "saveme", NULL, NULL);

                  pBot->f_medic_yell_time = gpGlobals->time + 10.0;
               }
            }
         }
      }
   }

   if (pBot->f_medic_pause_time > gpGlobals->time)
   {
      pBot->f_move_speed = 0.0;
      pBot->f_strafe_direction = 0.0;
   }

   if (pBot->f_grenade_search_time <= gpGlobals->time)
   {
      pBot->f_grenade_search_time = gpGlobals->time + 0.1;

      // does the bot see any grenades laying about?
      if (BotLookForGrenades( pBot ))
         pBot->f_grenade_found_time = gpGlobals->time;
   }

   // do we have a grenade primed and ready to throw..
   if (pBot->f_gren_throw_time > gpGlobals->time)
   {
      pBot->f_move_speed = 0.0;  // don't move while priming
   }
   else if (pBot->f_gren_throw_time + 5.0 > gpGlobals->time)
   {
      // move backwards for 5.0 seconds after throwing grenade...
      pBot->f_move_speed = -1.0 * pBot->f_move_speed;
   }
   else if (pBot->f_grenade_found_time + 1.0 > gpGlobals->time)
   {
      // move backwards for 1.0 second after seeing a grenade...
      pBot->f_move_speed = -1.0 * pBot->f_move_speed;
   }

   pEdict->v.v_angle.z = 0;  // reset roll to 0 (straight up and down)

   // set the body angles the same way the bot is looking/aiming
   pEdict->v.angles.x = -pEdict->v.v_angle.x / 3;
   pEdict->v.angles.y = pEdict->v.v_angle.y;
   pEdict->v.angles.z = pEdict->v.v_angle.z;

   // save the previous speed (for checking if stuck)
   pBot->f_prev_speed = pBot->f_move_speed;

   f_strafe_speed = pBot->f_strafe_direction * (pBot->f_move_speed / 2.0);

   g_engfuncs.pfnRunPlayerMove( pEdict, pEdict->v.v_angle, pBot->f_move_speed,
                                f_strafe_speed, 0, pEdict->v.button, 0, pBot->msecval);

   return;
}

[end of bot.cpp]
