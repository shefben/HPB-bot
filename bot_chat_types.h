#ifndef BOT_CHAT_TYPES_H
#define BOT_CHAT_TYPES_H

#include <string>
#include <vector> // Included for completeness, though AdvancedChatMessage_t uses std::string directly

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
    // TFC/CTF-like objective events:
    CHAT_EVENT_ENEMY_TOOK_OUR_FLAG,
    CHAT_EVENT_WE_TOOK_ENEMY_FLAG,
    CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG, // Our flag dropped by enemy
    CHAT_EVENT_WE_DROPPED_ENEMY_FLAG,
    CHAT_EVENT_OUR_FLAG_RETURNED,
    CHAT_EVENT_ENEMY_FLAG_RETURNED,
    CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG, // Scored with the flag

    // Control Point-like objective events:
    CHAT_EVENT_WE_CAPTURING_POINT,      // Friendly team is capturing a point
    CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT, // Enemy is capturing a point owned by bot's team
    CHAT_EVENT_WE_SECURED_POINT,        // Friendly team successfully captured/secured a point
    CHAT_EVENT_ENEMY_SECURED_OUR_POINT, // Enemy successfully captured/secured a point owned by bot's team
    CHAT_EVENT_POINT_UNDER_ATTACK,      // A friendly point is being contested/attacked

    // Counter-Strike-like objective events:
    CHAT_EVENT_BOMB_PLANTED_AS_T,       // Bot is Terrorist, bomb has been planted (by anyone)
    CHAT_EVENT_BOMB_PLANTED_AS_CT,      // Bot is Counter-Terrorist, bomb has been planted
    CHAT_EVENT_BOMB_DEFUSED_AS_CT,      // Bot is CT, bomb successfully defused by CTs
    CHAT_EVENT_BOMB_DEFUSED_AS_T,       // Bot is T, bomb was defused by CTs (bad for T)
    CHAT_EVENT_HOSTAGES_RESCUED_AS_CT,
    CHAT_EVENT_HOSTAGES_RESCUED_AS_T,   // Bad for T if CTs rescue hostages
    CHAT_EVENT_BUY_TIME_START,          // Start of buy time in CS

    // Round/Game Lifecycle Events
    CHAT_EVENT_ROUND_START,
    CHAT_EVENT_ROUND_WIN_TEAM,          // Bot's team won the round
    CHAT_EVENT_ROUND_LOSE_TEAM,         // Bot's team lost the round
    CHAT_EVENT_MATCH_WIN_TEAM,          // Bot's team won the match/map
    CHAT_EVENT_MATCH_LOSE_TEAM,         // Bot's team lost the match/map

    // Bot Specific Requests / Information Sharing
    CHAT_EVENT_REQUEST_MEDIC_TFC,       // For TFC medics specifically
    CHAT_EVENT_REQUEST_AMMO,
    CHAT_EVENT_REQUEST_BACKUP,
    CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED,
    CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED,
    CHAT_EVENT_INFO_INCOMING_GRENADE,

    // Responses to Player Chat (more advanced, for future)
    // CHAT_EVENT_PLAYER_TAUNT_RESPONSE,
    // CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE,
    // CHAT_EVENT_AGREE_WITH_TEAMMATE_ORDER,
    // CHAT_EVENT_DISAGREE_WITH_TEAMMATE_ORDER,

    NUM_CHAT_EVENT_TYPES                // Must be last, for array sizing and iteration
} ChatEventType_e;

// Structure for a single chat message template.
typedef struct {
    std::string text;                   // The chat message template, possibly with placeholders like {victim_name}
    bool can_modify_runtime;            // Placeholder for future: if bot can learn to alter this base string
    // Potential future additions:
    // int min_skill_to_use;
    // float personality_trait_match_score;
} AdvancedChatMessage_t;

#endif // BOT_CHAT_TYPES_H
