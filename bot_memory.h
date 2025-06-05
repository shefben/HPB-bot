#ifndef BOT_MEMORY_H
#define BOT_MEMORY_H

#include "bot.h"                 // For BOT_NAME_LEN, BOT_SKIN_LEN, etc.
#include "bot_neuro_evolution.h" // For MAX_NN_WEIGHT_SIZE, TacticalNeuralNetwork_t (needed if used directly, for now just MAX_NN_WEIGHT_SIZE)
#include "bot_objective_discovery.h" // For ObjectiveType_e, ActivationMethod_e (used in SavedDiscoveredObjective_t)
                                     // This also implies bot_tactical_ai.h is included by one of these, for the base ObjectiveType_e
#include "extdll.h"              // For Vector
#include "bot_rl_aiming.h"       // For MAX_RL_AIMING_NN_WEIGHT_SIZE

// Header for the consolidated bot memory file
typedef struct {
    char file_signature[16];      // e.g., "HPB_BOT_MEM_V1" (Consider V2 or V3 now)
    int file_version;             // For future format changes
    char map_name[32];            // Name of the map this memory is associated with
    int num_waypoints_in_file;    // Number of waypoints saved
    int num_bot_slots_in_file;    // Number of bot slots (e.g., 32)
    int num_discovered_objectives; // Number of discovered objectives saved
} bot_memory_file_hdr_t;

// Structure to hold persistent data for a single bot
typedef struct {
    // Original personality fields
    char name[BOT_NAME_LEN + 1];
    char skin[BOT_SKIN_LEN + 1];
    int bot_skill;
    int chat_percent;
    int taunt_percent;
    int whine_percent;
    int logo_percent;
    int chat_tag_percent;
    int chat_drop_percent;
    int chat_swap_percent;
    int chat_lower_percent;
    int reaction_time;
    int top_color;
    int bottom_color;
    char logo_name[16];
    int weapon_points[6];
    int sentrygun_waypoint;
    int dispenser_waypoint;
    int bot_team;
    int bot_class;
    bool is_used_in_save;

    // Added for NN persistence (Neuro-Evolution for Tactics - Phase 1, Step 5)
    bool has_saved_nn_weights;
    float tactical_nn_weights[MAX_NN_WEIGHT_SIZE]; // MAX_NN_WEIGHT_SIZE from bot_neuro_evolution.h

    // Added for RL Aiming NN persistence
    bool has_saved_aiming_nn;
    float aiming_rl_nn_weights[MAX_RL_AIMING_NN_WEIGHT_SIZE];

} persistent_bot_data_t;


// New struct for saving discovered objectives (Objective Learning - Phase 2, Step 4)
typedef struct {
    Vector location;
    char entity_classname[64];
    char entity_targetname[64];
    int unique_id;

    ObjectiveType_e learned_objective_type;
    float confidence_score;

    int positive_event_correlations;
    int negative_event_correlations;

    // Added in Objective Learning - Phase 2, Step 5 (via subtask for that step)
    int current_owner_team;
    ActivationMethod_e learned_activation_method;

} SavedDiscoveredObjective_t;


// Function Prototypes for save/load functions (defined in bot_memory.cpp)
#ifndef BOT_MEMORY_FUNCS_H
#define BOT_MEMORY_FUNCS_H
void SaveBotMemory(const char *filename);
void LoadBotMemory(const char *filename);
#endif

#endif // BOT_MEMORY_H
