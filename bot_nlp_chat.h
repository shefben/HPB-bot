#ifndef BOT_NLP_CHAT_H
#define BOT_NLP_CHAT_H

#include <string>
#include <vector>
#include <map>

// Function to load and preprocess chat sentences from a file
void NLP_LoadCorpusFromFile(const char* base_filename, const char* mod_name_for_path_unused, std::vector<std::string>& out_sentences);

// --- N-gram Model Structures ---
// Context for N-grams (N-1 words)
typedef std::vector<std::string> NlpNgramContext;

// Stores counts of words following a given context
typedef std::map<std::string, int> NextWordCounts;

// The N-gram model: Maps a context (N-1 words) to possible next words and their counts
typedef std::map<NlpNgramContext, NextWordCounts> NgramModel_t;

// Prototype for the training function
void NLP_TrainModel(const std::vector<std::string>& sentences, int N, NgramModel_t& out_model);

// Prototype for message generation (will be implemented in next step)
std::string NLP_GenerateChatMessage(const NgramModel_t& model, int N, int max_words, const std::vector<std::string>& seed_context = {});

// Expose global model and N-value for access from other files
extern NgramModel_t g_chat_ngram_model;
extern int g_ngram_model_N_value;


// --- Categorized Event-Driven Chat System ---

typedef enum {
    CHAT_EVENT_GENERIC_IDLE = 0,
    CHAT_EVENT_WELCOME_MESSAGE,

    // Combat Events
    CHAT_EVENT_GOT_KILL,
    CHAT_EVENT_GOT_KILL_HEADSHOT,
    CHAT_EVENT_GOT_KILL_REVENGE,
    CHAT_EVENT_GOT_MULTI_KILL,
    CHAT_EVENT_WAS_KILLED_BY_ENEMY,
    CHAT_EVENT_WAS_KILLED_BY_TEAMMATE,
    CHAT_EVENT_LOW_HEALTH,
    CHAT_EVENT_RELOADING,

    // Objective/Game State Events
    CHAT_EVENT_ENEMY_TOOK_OUR_FLAG,
    CHAT_EVENT_WE_TOOK_ENEMY_FLAG,
    CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG,
    CHAT_EVENT_WE_DROPPED_ENEMY_FLAG,
    CHAT_EVENT_OUR_FLAG_RETURNED,
    CHAT_EVENT_ENEMY_FLAG_RETURNED,
    CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG,
    CHAT_EVENT_WE_CAPTURING_POINT,
    CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT,
    CHAT_EVENT_WE_SECURED_POINT,
    CHAT_EVENT_ENEMY_SECURED_OUR_POINT,
    CHAT_EVENT_POINT_UNDER_ATTACK,
    CHAT_EVENT_BOMB_PLANTED_AS_T,
    CHAT_EVENT_BOMB_PLANTED_AS_CT,
    CHAT_EVENT_BOMB_DEFUSED_AS_CT,
    CHAT_EVENT_BOMB_DEFUSED_AS_T,
    CHAT_EVENT_HOSTAGES_RESCUED_AS_CT,
    CHAT_EVENT_HOSTAGES_RESCUED_AS_T,
    CHAT_EVENT_BUY_TIME_START,

    // Round/Game Lifecycle
    CHAT_EVENT_ROUND_START,
    CHAT_EVENT_ROUND_WIN_TEAM,
    CHAT_EVENT_ROUND_LOSE_TEAM,
    CHAT_EVENT_MATCH_WIN_TEAM,
    CHAT_EVENT_MATCH_LOSE_TEAM,

    // Bot Specific Requests / Info
    CHAT_EVENT_REQUEST_MEDIC_TFC,
    CHAT_EVENT_REQUEST_AMMO,
    CHAT_EVENT_REQUEST_BACKUP,
    CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED,
    CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED,
    CHAT_EVENT_INFO_INCOMING_GRENADE,

    // Taunts/Responses to player chat
    CHAT_EVENT_PLAYER_TAUNT_RESPONSE,
    CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE,
    CHAT_EVENT_AGREE_WITH_TEAMMATE,
    CHAT_EVENT_DISAGREE_WITH_TEAMMATE,

    NUM_CHAT_EVENT_TYPES
} ChatEventType_e;

typedef struct {
    std::string text;
    bool can_modify_runtime;
} AdvancedChatMessage_t;

// Declaration for the global map
extern std::map<ChatEventType_e, std::vector<AdvancedChatMessage_t>> g_categorized_chat_lines;

// Function prototype for loading categorized chat file
void AdvancedChat_LoadChatFile(const char* base_filename);


#endif // BOT_NLP_CHAT_H
