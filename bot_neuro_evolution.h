#ifndef BOT_NEURO_EVOLUTION_H
#define BOT_NEURO_EVOLUTION_H

// This header is primarily for documenting the NN input/output formats
// and defining any constants related to the neural network structure if needed.
// Actual NN implementation might be in a separate .cpp or linked as a library.

#include <vector> // For std::vector for weights/biases in TacticalNeuralNetwork_t
#include "bot_tactical_ai.h" // For MAX_CLASSES_PER_MOD, MAX_TEAMS (if used here)


// Tactical Directives Enum
// These are the high-level actions the NN can decide upon.
typedef enum {
    // Directives related to Top N objectives (e.g., N=3)
    TACTICS_ATTACK_OBJECTIVE_1 = 0, // Attack the highest priority discovered objective
    TACTICS_DEFEND_OBJECTIVE_1,     // Defend the highest priority discovered objective
    TACTICS_ATTACK_OBJECTIVE_2,
    TACTICS_DEFEND_OBJECTIVE_2,
    TACTICS_ATTACK_OBJECTIVE_3,
    TACTICS_DEFEND_OBJECTIVE_3,

    // More generic team/individual tactics
    TACTICS_GATHER_RESOURCES,       // If applicable to mod (e.g., CS money rounds, TFC engineer focus)
    TACTICS_GROUP_UP_TEAM,          // Attempt to move towards teammates
    TACTICS_HOLD_DEFENSIVE_AREA,    // Find a defensible spot and hold, not tied to a specific objective
    TACTICS_SEARCH_FOR_OBJECTIVES,  // If current objectives have low confidence or are all friendly
    TACTICS_FALLBACK_REGROUP,       // Tactical retreat if overwhelmed or to consolidate
    TACTICS_PURSUE_WEAK_ENEMIES,    // Focus on picking off vulnerable targets

    NUM_TACTICAL_DIRECTIVES // Automatically gives the count of directives
} TacticalDirective;


// Structure for the Tactical Neural Network
typedef struct {
    int num_input_neurons;
    int num_hidden_neurons;
    int num_output_neurons; // Should match NUM_TACTICAL_DIRECTIVES

    std::vector<float> weights_input_hidden;  // Size: num_input_neurons * num_hidden_neurons
    std::vector<float> bias_hidden;           // Size: num_hidden_neurons

    std::vector<float> weights_hidden_output; // Size: num_hidden_neurons * num_output_neurons
    std::vector<float> bias_output;           // Size: num_output_neurons

    std::vector<float> hidden_layer_output_activations; // Size: num_hidden_neurons

} TacticalNeuralNetwork_t;

// Helper function prototype for logging/debugging
const char* TacticalDirectiveToString(TacticalDirective directive);

// NN Manipulation functions
void NN_Initialize(TacticalNeuralNetwork_t* nn, int num_inputs, int num_hidden, int num_outputs,
                   bool initialize_with_random_weights, const float* initial_weights_data);
void NN_FeedForward(TacticalNeuralNetwork_t* nn, const float* inputs, float* raw_outputs_buffer);
TacticalDirective NN_GetBestDirective(const float* nn_raw_outputs, int num_outputs);
void NN_FlattenWeights(const TacticalNeuralNetwork_t* nn, float* flat_array);


/*
    NN Input Vector Definition
    --------------------------
    Normalization:
        - Ratios/flags: Generally [0, 1] or [-1, 1] if applicable.
        - Counts: Normalized by a typical maximum expected value for that count.
        - Positions/Distances: Normalized by map diagonal or a max expected engagement range.
                        Alternatively, use relative positions (dx, dy, dz) from bot to target,
                        then normalize these components.
        - Enums (like game phase, objective type): Can be one-hot encoded or mapped to a normalized float range.

    SECTION 1: Global Game State (Approx. 5-10 floats)
    ------------------------------
    1.  current_game_phase (float): Mapped from GamePhase_e.
        Example: WARMUP=0.0, SETUP=0.2, ROUND_ACTIVE=0.5, ROUND_ENDING=0.8, POST_ROUND/GAME_OVER=1.0.
    2.  round_time_normalized (float):
        If round has a time limit: (time_remaining / initial_max_round_time).
        If no time limit or for general elapsed time: (time_elapsed_in_round / typical_full_round_duration).
        Could also be two separate inputs: time_elapsed_norm, time_remaining_norm.
    3.  my_team_score_normalized (float): (my_team_score / typical_win_score_or_max_score).
    4.  enemy1_score_normalized (float): (enemy1_score / typical_win_score_or_max_score).
    5.  enemy2_score_normalized (float): (If 3+ teams, else 0 or omit).
    6.  enemy3_score_normalized (float): (If 4 teams, else 0 or omit).

    SECTION 2: Bot's Own State (Approx. 10-15 floats)
    ---------------------------
    (Data primarily from bot_t pBot)
    1.  health_ratio (float): (pBot->pEdict->v.health / pBot->pEdict->v.max_health)
    2.  armor_ratio (float): (pBot->pEdict->v.armorvalue / MAX_NORMAL_ARMOR) (e.g., 100.0f)
    3.  is_carrying_flag_or_objective (float): 0.0 or 1.0 (e.g. pBot->bot_has_flag, or if carrying a specific mission item)
    4.  primary_weapon_ammo_ratio (float): (current_clip / max_clip) or (total_ammo / typical_max_ammo_for_weapon)
    5.  secondary_weapon_ammo_ratio (float): (similar to primary)
    6.  num_grenades1_normalized (float): (pBot->gren1 / max_gren1_tfc)
    7.  num_grenades2_normalized (float): (pBot->gren2 / max_gren2_tfc)
    8.  current_velocity_magnitude_normalized (float): (current_speed / pBot->f_max_speed)
    9.  is_ducking (float): 0.0 or 1.0 (pBot->pEdict->v.flags & FL_DUCKING)
    10. is_on_ground (float): 0.0 or 1.0 (pBot->pEdict->v.flags & FL_ONGROUND)
    11. time_since_last_enemy_seen_normalized (float): ( (gpGlobals->time - pBot->f_bot_see_enemy_time) / max_relevant_time_enemy_ unseen (e.g. 30s) ) -> capped at 1.0
    12. time_since_last_took_damage_normalized (float): ( (gpGlobals->time - pBot->f_last_dmg_time_placeholder) / max_relevant_time (e.g. 20s) ) -> capped at 1.0 (needs f_last_dmg_time)

    SECTION 3: My Team's State (Approx. 5 + MAX_CLASSES_PER_MOD floats)
    --------------------------
    (From g_tactical_state.team_info[my_bot_team_id])
    1.  num_alive_players_ratio (float): (num_alive_players / num_total_players) or (num_alive_players / max_team_size_for_mod)
    2.  class_ratios[MAX_CLASSES_PER_MOD] (array of floats): (count_class_X / num_total_players)
    3.  aggregate_health_ratio (float)
    4.  aggregate_armor_ratio (float)
    5.  team_resource_A_normalized (float): (e.g., for CS: total_team_money / (max_players * typical_max_money_per_player) )

    SECTION 4: Enemy Team 1's State (Approx. 5 + MAX_CLASSES_PER_MOD floats)
    ----------------------------
    (From g_tactical_state.team_info[enemy1_team_id])
    - (Similar fields as My Team's State)

    // SECTION X: (Optional) Other Enemy Teams' States ...

    SECTION Y: Top N Discovered/Tracked Objectives (N * Approx. 10-12 floats each)
    --------------------------------------------
    (Data from g_tactical_state.objective_points[], sorted by relevance/confidence for this bot)
    For each of the top N objectives (e.g., N=3 or 5):
    1.  objN_type (float): Mapped from ObjectiveType_e (e.g., one-hot encode, or map to normalized range like 0.0 to 1.0 over enum size).
           If one-hot: K floats where K is number of ObjectiveType_e values.
    2.  objN_owner_status (float): (e.g., -1.0 for neutral, 0.0 for my_team, 1.0 for enemy_team. If >2 teams, map accordingly e.g. my_team=0, enemy1=0.5, enemy2=1.0).
    3.  objN_is_contested (float): 0.0 or 1.0.
    4.  objN_relative_position_x_normalized (float): (obj_pos.x - bot_pos.x) / typical_map_dimension_or_view_distance
    5.  objN_relative_position_y_normalized (float): (obj_pos.y - bot_pos.y) / typical_map_dimension_or_view_distance
    6.  objN_relative_position_z_normalized (float): (obj_pos.z - bot_pos.z) / typical_map_dimension_or_view_distance
    7.  objN_distance_normalized (float): (distance_to_obj / typical_map_diagonal)
    // Specific to OBJ_TYPE_FLAG or OBJ_TYPE_FLAG_LIKE_PICKUP (provide default values like 0 if not applicable)
    8.  objN_flag_is_at_home_base (float): 0.0 or 1.0. (Default 0.0 if not a flag type)
    9.  objN_flag_carrier_status (float):
            0.0 = Not carried / At its base (could be same as is_flag_at_home_base if it's own flag)
            0.5 = Carried by my team
            1.0 = Carried by an enemy team
            (Default 0.0 if not a flag type)
    10. objN_flag_relative_current_pos_x_norm (float): If carried/dropped, (flag_curr_pos.x - bot_pos.x) / map_dim. (Default 0)
    11. objN_flag_relative_current_pos_y_norm (float): (Default 0)
    12. objN_flag_relative_current_pos_z_norm (float): (Default 0)


    SECTION Z: Nearest Threat / Last Seen Enemy (Approx. 5-10 floats)
    -------------------------------------------
    (Data from pBot->pBotEnemy or a tactical assessment of nearest visible threat)
    1.  threat_exists (float): 0.0 or 1.0
    2.  threat_relative_position_x_normalized (float): (Default 0 if no threat)
    3.  threat_relative_position_y_normalized (float): (Default 0 if no threat)
    4.  threat_relative_position_z_normalized (float): (Default 0 if no threat)
    5.  threat_distance_normalized (float): (Default 1.0 if no threat)
    6.  threat_health_ratio (float): (Default 0 if no threat)
    7.  threat_is_aiming_at_me (float): 0.0 or 1.0 (Requires more advanced perception, default 0)


    // Total size calculation needs to be precise based on final selection of fields
    // and one-hot encoding choices.
*/

// Example constants based on a hypothetical structure
#define TOP_N_OBJECTIVES_FOR_NN_INPUT 3

// Assuming one-hot encoding for objective type (e.g., 15 types) -> 15 floats
// Assuming 12 other stats per objective
#define STATS_PER_OBJECTIVE (15 + 12)

// Assuming 5 global stats
#define GLOBAL_STATE_SIZE 5

// Assuming bot's own state size
#define BOT_OWN_STATE_SIZE 12

// Assuming team state size (e.g. 4 base stats + MAX_CLASSES_PER_MOD ratios)
#define TEAM_STATE_BASE_SIZE 4
// const int TEAM_STATE_SIZE = TEAM_STATE_BASE_SIZE + MAX_CLASSES_PER_MOD; // This needs MAX_CLASSES_PER_MOD to be defined earlier

// Assuming threat state size
#define THREAT_STATE_SIZE 7

// Placeholder for total size - MUST BE CALCULATED ACCURATELY
// const int NN_INPUT_SIZE = GLOBAL_STATE_SIZE + BOT_OWN_STATE_SIZE + (2 * TEAM_STATE_SIZE) + (TOP_N_OBJECTIVES_FOR_NN_INPUT * STATS_PER_OBJECTIVE) + THREAT_STATE_SIZE;

#ifndef BOT_NN_CONSTANTS_H
#define BOT_NN_CONSTANTS_H

// These constants define the structure of the Neural Network input and update frequency.
// Ensure MAX_CLASSES_PER_MOD is defined (e.g. from bot_tactical_ai.h)
// Calculation based on documented NN Input Vector Definition:
// Section 1: Global Game State (4 floats: game_phase, round_time, my_team_score, enemy1_score)
// Section 2: Bot's Own State (12 floats) - assuming f_last_dmg_time is available or placeholder used
// Section 3 & 4: My Team & Enemy Team State (2 teams * (4 base + MAX_CLASSES_PER_MOD class_ratios + 1 resource_A))
// Section Y: Top N Objectives (TOP_N_OBJECTIVES_FOR_NN_INPUT * 8 floats per objective: type, owner, contested, rel_pos_x, rel_pos_y, rel_pos_z, dist_norm, flag_is_home, flag_carrier_status, flag_rel_x, flag_rel_y, flag_rel_z - actually 12 floats for flags, 8 for others. Let's use a general size, e.g. 8, and pad/omit flag specific for non-flags for simplicity in this calculation)
// For simplicity, let's use the example counts: Global(4) + BotOwn(12) + MyTeam(5+MAX_CLASSES_PER_MOD) + EnemyTeam(5+MAX_CLASSES_PER_MOD) + TopNObj(TOP_N_OBJECTIVES_FOR_NN_INPUT * 8) + Threat(7)
// This is still an example, a more precise count is needed.
// Let's use the example from the prompt: Global(4) + 2 Teams * (4 base_team_stats + MAX_CLASSES_PER_MOD) + TOP_N_OBJECTIVES_FOR_NN_INPUT * 8 obj_stats
// Global(4) + MyTeam(4+12) + EnemyTeam1(4+12) + Obj1(8) + Obj2(8) + Obj3(8) = 4 + 16 + 16 + 24 = 60. This is an example.
// The prompt's example: 4 + (2 * (4 + MAX_CLASSES_PER_MOD)) + (TOP_N_OBJECTIVES_FOR_NN_INPUT * 8)
// With MAX_CLASSES_PER_MOD = 12, TOP_N_OBJECTIVES_FOR_NN_INPUT = 3:
// 4 + (2 * (4 + 12)) + (3 * 8) = 4 + (2 * 16) + 24 = 4 + 32 + 24 = 60.
const int NN_INPUT_SIZE = 4 + (2 * (4 + MAX_CLASSES_PER_MOD)) + (TOP_N_OBJECTIVES_FOR_NN_INPUT * 8);
const int NN_HIDDEN_SIZE = 25;
// NUM_TACTICAL_DIRECTIVES is derived from the enum in this file.
const int MAX_NN_WEIGHT_SIZE = (NN_INPUT_SIZE * NN_HIDDEN_SIZE) +  // Input-Hidden Weights
                                   NN_HIDDEN_SIZE +                   // Hidden Biases
                                   (NN_HIDDEN_SIZE * NUM_TACTICAL_DIRECTIVES) + // Hidden-Output Weights
                                   NUM_TACTICAL_DIRECTIVES;           // Output Biases

const float TACTICAL_NN_EVAL_INTERVAL = 3.0f; // Seconds

#endif // BOT_NN_CONSTANTS_H


#endif // BOT_NEURO_EVOLUTION_H
