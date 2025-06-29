#ifndef BOT_OBJECTIVE_DISCOVERY_H
#define BOT_OBJECTIVE_DISCOVERY_H

#include "extdll.h" // For Vector
#include <vector>   // For std::vector (if used for globals later, not strictly needed for struct defs)
#include <list>     // For std::list (if used for globals later)

// Forward declare ObjectiveType_e from bot_tactical_ai.h if not including the whole file
// or ensure bot_tactical_ai.h is included if its definitions are stable and non-circular.
// For simplicity here, let's assume ObjectiveType_e will be defined/accessible.
// If #include "bot_tactical_ai.h" was intended, ensure it's there.
// To be safe for this file's standalone definition, we can define a local enum
// or ensure bot_tactical_ai.h is minimal and included.
// For this task, assume "bot_tactical_ai.h" can be included or ObjectiveType_e is available.
#include "bot_tactical_ai.h" // For ObjectiveType_e

#ifndef BOT_OBJECTIVE_DISCOVERY_CONSTANTS_H
#define BOT_OBJECTIVE_DISCOVERY_CONSTANTS_H

#define MAX_OBJECTIVES_IN_DISCOVERY_LIST 256 // Max candidates from waypoints + dynamic
#define MAX_GAME_EVENTS_LOG_SIZE 500         // Max number of recent game events to keep

// Time window (in seconds) to look back for interaction events related to an outcome event
#define CORRELATION_WINDOW_SECONDS 15.0f
// Minimum number of total correlations before confidence score is considered somewhat stable
#define MIN_CORRELATIONS_FOR_STABLE_CONFIDENCE 5
#define MAX_RECENT_INTERACTORS 5 // For CandidateObjective_t recent interactors list size

#define LEARNING_RATE 0.05f
#define CONFIDENCE_DECAY_RATE 0.999f
#define CONFIDENCE_DECAY_THRESHOLD_SECONDS 120.0f
#define ROUND_WIN_CORRELATION_BONUS 5
#define MIN_CONFIDENCE_THRESHOLD 0.01f

#endif

// Enum for Activation Method Types
typedef enum {
    ACT_UNKNOWN = 0,
    ACT_TOUCH,          // Player must physically touch it
    ACT_USE,            // Player must press +use on it
    ACT_SHOOT_TARGET,   // Objective must be shot (e.g., a button activated by damage)
    ACT_TIMED_AREA_PRESENCE // Player must remain in an area for a duration
} ActivationMethod_e;

// Enum for Game Event Types
typedef enum {
    EVENT_TYPE_NONE = 0,
    EVENT_SCORE_CHANGED,          // Team score changed
    EVENT_ROUND_OUTCOME,          // Round ended (win/loss/draw for a team)
    EVENT_PLAYER_TOUCHED_ENTITY,  // Player touched a specific, potentially unknown, static entity
    EVENT_PLAYER_USED_ENTITY,     // Player likely pressed +use on an entity
    EVENT_PLAYER_ENTERED_AREA,    // Player entered a notable area (e.g., near a candidate objective)
    EVENT_PLAYER_DIED_NEAR_CANDIDATE, // Player died near a candidate objective
    EVENT_IMPORTANT_GAME_MESSAGE, // A text message that might be relevant (e.g., "Bomb Planted")
    EVENT_CANDIDATE_STATE_CHANGE,  // A candidate objective itself changed state (e.g. a door opened)
    EVENT_INTERNAL_OBJECTIVE_SHARE // An event logged by a bot when it confirms an objective
    // Add more event types as needed
} GameEventType_e;


typedef struct {
    Vector location;                    // World coordinates
    char entity_classname[64];        // Classname if it's an entity-based candidate
    char entity_targetname[64];       // Targetname if applicable
    int unique_id;                      // Waypoint index, or internal unique ID for non-waypoint based candidates

    ObjectiveType_e learned_objective_type; // e.g., OBJ_TYPE_NONE initially, later OBJ_TYPE_FLAG, etc. from bot_tactical_ai.h
    float confidence_score;             // 0.0 to 1.0, how sure the bot is that this is a real objective

    int last_interacting_team;          // Team ID that last interacted significantly
    float last_interaction_time;        // gpGlobals->time of last significant interaction

    int positive_event_correlations;    // Count of times interaction was followed by good game events for interacting team
    int negative_event_correlations;    // Count of times interaction was followed by bad game events for interacting team

    // New fields for Step 1 of Phase 2:
    int current_owner_team;                             // Team ID, -1 for neutral/unknown
    ActivationMethod_e learned_activation_method;     // How this objective is believed to be used
    std::vector<int> recent_interacting_player_edict_indices; // Edict indices of last N players
    std::vector<int> recent_interacting_teams;                // Teams of last N players/interactions
    float last_positive_correlation_update_time; // Time of last positive reinforcement for decay logic

    // New fields for richer semantic feature tracking
    bool gives_health_or_armor;
    bool triggers_another_entity_event;
    char primary_correlated_message_keyword[32];
    bool has_been_shared_as_confirmed; // To prevent spamming share events

} CandidateObjective_t;


typedef struct {
    GameEventType_e type;
    float timestamp;                    // gpGlobals->time when the event occurred or was logged

    int primarily_involved_team_id;     // e.g., team that scored, team that won round, team of player interacting
    int secondary_involved_team_id;   // e.g., other team in a score change, team of player who died

    int candidate_objective_id;         // unique_id of CandidateObjective_t if event is directly related to one, else -1
    int involved_player_user_id;        // UserID of player if relevant (e.g. from pEntity->v.user_id, though this field isn't standard in HL SDK edict_t, more of a concept)
                                        // More likely, store edict index if it's a player: int involved_player_edict_index;

    float event_value_float;            // e.g., score change amount, damage amount
    int event_value_int;                // e.g., win=1, loss=-1, draw=0 for ROUND_OUTCOME for primarily_involved_team_id
    char event_message_text[128];       // For EVENT_IMPORTANT_GAME_MESSAGE or other relevant text

    // New fields for advanced correlation
    bool is_direct_consequence_link;    // True if this event directly confirms an objective interaction's outcome
    int directly_linked_candidate_id; // unique_id of the CandidateObjective_t if this is a direct link

} GameEvent_t;

// Global data extern declarations
extern std::vector<CandidateObjective_t> g_candidate_objectives;
extern std::list<GameEvent_t> g_game_event_log;
extern int g_dynamic_candidate_id_counter;

// Function prototypes
void ObjectiveDiscovery_LevelInit();
void ObjectiveDiscovery_UpdatePeriodic();
void ObjectiveDiscovery_AnalyzeEvents();
// Updated AddGameEvent signature
void AddGameEvent(GameEventType_e type, float timestamp,
                  int team1, int team2, int associated_candidate_id,
                  int player_edict_idx, float val_float, int val_int, const char* message,
                  bool is_direct_link = false, int direct_link_obj_id = -1);
CandidateObjective_t* GetCandidateObjectiveById(int unique_id);
const char* ObjectiveTypeToString(ObjectiveType_e obj_type);
const char* GameEventTypeToString(GameEventType_e event_type);
void ObjectiveDiscovery_DrawDebugVisuals(edict_t* pViewPlayer);
const char* ActivationMethodToString(ActivationMethod_e act_meth);

// Forward declare bot_t for the new function prototype if not already available
struct bot_t;
void ObjectiveDiscovery_ProcessSharedObjectiveData(bot_t* pReceivingBot, int shared_candidate_id, ObjectiveType_e shared_type, float shared_confidence, const char* shared_keyword);


#endif // BOT_OBJECTIVE_DISCOVERY_H
