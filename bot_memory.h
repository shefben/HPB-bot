#ifndef BOT_MEMORY_H
#define BOT_MEMORY_H

#include "bot.h" // For BOT_NAME_LEN, BOT_SKIN_LEN, etc.

// Header for the consolidated bot memory file
typedef struct {
    char file_signature[16];      // e.g., "HPB_BOT_MEM_V1"
    int file_version;             // For future format changes
    char map_name[32];            // Name of the map this memory is associated with
    int num_waypoints_in_file;    // Number of waypoints saved
    int num_bot_slots_in_file;    // Number of bot slots (e.g., 32)
    int num_discovered_objectives; // Number of discovered objectives saved
} bot_memory_file_hdr_t;


#include "bot_tactical_ai.h" // For ObjectiveType_e
#include "bot_objective_discovery.h" // For ActivationMethod_e

// New struct for saving discovered objectives:
typedef struct {
    Vector location;
    char entity_classname[64];
    char entity_targetname[64];
    int unique_id; // Original unique_id (waypoint index or dynamic ID)

    ObjectiveType_e learned_objective_type;
    float confidence_score;

    int positive_event_correlations;
    int negative_event_correlations;

    // New fields for Phase 2 Step 3
    int current_owner_team;
    ActivationMethod_e learned_activation_method;
} SavedDiscoveredObjective_t;


// Structure to hold persistent data for a single bot
typedef struct {
    // Persisted from bot_t
    char name[BOT_NAME_LEN + 1];
    char skin[BOT_SKIN_LEN + 1]; // If different from default for model
    int bot_skill;               // 0-4

    int chat_percent;
    int taunt_percent;
    int whine_percent;
    int logo_percent;

    int chat_tag_percent;
    int chat_drop_percent;
    int chat_swap_percent;
    int chat_lower_percent;

    int reaction_time; // milliseconds

    int top_color;    // -1 if not set
    int bottom_color; // -1 if not set
    char logo_name[16];

    // Waypoint related memory specific to a bot's preferences/learning
    // These might be indices into the main waypoints array or special values.
    // Ensure they are handled correctly during load if waypoints change.
    int weapon_points[6];
    int sentrygun_waypoint; // index or -1
    int dispenser_waypoint; // index or -1

    // Team and class preferences, if they are to be remembered
    // These are often set during bot creation arguments, but saving them
    // could represent a learned preference if bots could change them.
    int bot_team;   // Team preference
    int bot_class;  // Class preference for that team

    // Add a field to indicate if this slot was in use,
    // so we know whether the loaded data is meaningful.
    bool is_used_in_save;

} persistent_bot_data_t;

#ifndef BOT_MEMORY_FUNCS_H // Guard against multiple inclusions if moved later
#define BOT_MEMORY_FUNCS_H
void SaveBotMemory(const char *filename);
void LoadBotMemory(const char *filename);
#endif

#endif // BOT_MEMORY_H
