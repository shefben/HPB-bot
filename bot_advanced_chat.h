#ifndef BOT_ADVANCED_CHAT_H
#define BOT_ADVANCED_CHAT_H

#include <vector>
#include <string>
#include <map>
#include "extdll.h" // For edict_t

// Forward declare bot_t to avoid circular dependencies if this is included by bot.h
struct bot_t;

// Defines the various game events or contexts that can trigger a bot chat response.
typedef enum {
    CHAT_EVENT_GENERIC_IDLE = 0,    // For random chatter when idle
    CHAT_EVENT_WELCOME_MESSAGE,     // For initial server join message (if used by bot)

    // Combat Related Events
    CHAT_EVENT_GOT_KILL,            // Bot killed an enemy
    CHAT_EVENT_GOT_KILL_HEADSHOT,   // Bot killed an enemy with a headshot
    CHAT_EVENT_GOT_KILL_REVENGE,    // Bot killed an enemy who previously killed them
    CHAT_EVENT_GOT_MULTI_KILL,      // Bot got a double, triple kill, etc.
    CHAT_EVENT_WAS_KILLED_BY_ENEMY, // Bot was killed by an enemy
    CHAT_EVENT_WAS_KILLED_BY_TEAMMATE,// Bot was teamkilled
    CHAT_EVENT_LOW_HEALTH,          // Bot's health is low
    CHAT_EVENT_RELOADING,           // Bot is reloading

    // Objective/Game State Events (Examples - to be expanded based on mod needs)
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

    // Round/Game Lifecycle Events
    CHAT_EVENT_ROUND_START,
    CHAT_EVENT_ROUND_WIN_TEAM,
    CHAT_EVENT_ROUND_LOSE_TEAM,
    CHAT_EVENT_MATCH_WIN_TEAM,
    CHAT_EVENT_MATCH_LOSE_TEAM,

    // Bot Specific Requests / Information Sharing
    CHAT_EVENT_REQUEST_MEDIC_TFC,
    CHAT_EVENT_REQUEST_AMMO,
    CHAT_EVENT_REQUEST_BACKUP,
    CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED,
    CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED,
    CHAT_EVENT_INFO_INCOMING_GRENADE,

    // Taunts/Responses to player chat (more advanced, for future)
    // CHAT_EVENT_PLAYER_TAUNT_RESPONSE,
    // CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE,
    // CHAT_EVENT_AGREE_WITH_TEAMMATE_ORDER,
    // CHAT_EVENT_DISAGREE_WITH_TEAMMATE_ORDER,

    NUM_CHAT_EVENT_TYPES
} ChatEventType_e;

// Structure for a single chat message template.
typedef struct {
    std::string text;
    bool can_modify_runtime;
} AdvancedChatMessage_t;

// Global map holding all categorized chat lines, loaded from a file.
extern std::map<ChatEventType_e, std::vector<AdvancedChatMessage_t>> g_categorized_chat_lines;

// Loads categorized chat lines from the specified file into g_categorized_chat_lines.
void AdvancedChat_LoadChatFile(const char* filename);

// Handles a game event and makes the bot say an appropriate categorized chat message.
void AdvancedChat_HandleGameEvent(bot_t* pBot, ChatEventType_e event_type,
                                  edict_t* pSubjectEntity1 = NULL,
                                  edict_t* pSubjectEntity2 = NULL,
                                  const char* custom_data_str = NULL);


#ifndef BOT_NGRAM_TYPEDEFS_H
#define BOT_NGRAM_TYPEDEFS_H

// Stores possible next words and their frequencies for a given prefix.
typedef std::map<std::string, int> NgramContinuationsMap_t;

// The main N-gram model structure.
typedef struct {
    int n_value;
    std::map<std::string, NgramContinuationsMap_t> model_data;
} NgramModel_t;

// Constants for N-gram processing
const std::string NGRAM_KEY_DELIMITER = " ";
const std::string NGRAM_START_TOKEN = "<SOS>";
const std::string NGRAM_END_TOKEN = "<EOS>";
