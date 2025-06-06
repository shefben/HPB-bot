#ifndef BOT_CATEGORIZED_CHAT_H
#define BOT_CATEGORIZED_CHAT_H

#include "bot_chat_types.h" // For ChatEventType_e, AdvancedChatMessage_t
#include <map>              // For std::map
#include <vector>           // For std::vector
#include <string>           // For std::string (though likely included by bot_chat_types.h)

// Forward declarations for types used in function prototypes
struct bot_t;         // Defined in bot.h
struct edict_s;       // Defined in extdll.h (usually typedef'd to edict_t)

// Ensure edict_t is defined. This handling can be platform/compiler specific.
// A common approach is that extdll.h (or a similar core SDK header) provides the typedef.
// If relying on that, ensure extdll.h is included where edict_t is needed.
// For this header, a forward declaration of the struct and a conditional typedef is a common pattern.
#ifndef _WIN32
typedef struct edict_s edict_t;
#else
#ifndef __EDICT_T__   // Check for a common guard SDKs might use for edict_t typedef
#define __EDICT_T__
typedef struct edict_s edict_t;
#endif
#endif


// Global map holding all categorized chat lines, loaded from a file.
// The actual definition will be in a .cpp file (e.g., bot_advanced_chat.cpp or bot_chat.cpp).
extern std::map<ChatEventType_e, std::vector<AdvancedChatMessage_t>> g_categorized_chat_lines;

/**
 * @brief Loads categorized chat lines from the specified file into g_categorized_chat_lines.
 * @param filename The path to the chat file.
 */
void AdvancedChat_LoadChatFile(const char* filename);

/**
 * @brief Handles a game event and makes the bot say an appropriate categorized chat message.
 * @param pBot Pointer to the bot_t structure for the speaking bot.
 * @param event_type The type of game event that occurred.
 * @param pSubjectEntity1 Primary entity related to the event (e.g., victim, killer, objective entity).
 * @param pSubjectEntity2 Secondary entity (e.g., weapon).
 * @param custom_data_str Custom string data (e.g., name of an objective not represented by an entity).
 */
void AdvancedChat_HandleGameEvent(bot_t* pBot, ChatEventType_e event_type,
                                  edict_t* pSubjectEntity1 = NULL,
                                  edict_t* pSubjectEntity2 = NULL,
                                  const char* custom_data_str = NULL);

#endif // BOT_CATEGORIZED_CHAT_H
