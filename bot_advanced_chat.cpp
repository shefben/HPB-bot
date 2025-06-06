#include "bot_categorized_chat.h" // Includes bot_chat_types.h
#include "bot.h"                // For bot_t, gpGlobals, UTIL_HostSay, STRING, RANDOM_*, UTIL_BuildFileName, ALERT, at_console
#include <stdio.h>              // For fopen, fclose, fgets (though std::ifstream is used for loading)
#include <string.h>             // For strlen, strncpy, stricmp (or strcasecmp for POSIX), strstr
#include <ctype.h>              // For tolower, isspace (not used in provided snippet, but good for general text processing)
#include <fstream>              // For std::ifstream
#include <sstream>              // For std::ostringstream in JoinWords, std::istringstream for parsing
#include <algorithm>            // For std::remove if used

// Global map holding all categorized chat lines
std::map<ChatEventType_e, std::vector<AdvancedChatMessage_t>> g_categorized_chat_lines;

// Chat cooldown constants (using _ADV to distinguish if old constants exist)
const float MIN_CHAT_INTERVAL_ADV = 5.0f;
const float MAX_CHAT_INTERVAL_ADV = 15.0f;
const int MAX_CHAT_OUTPUT_LENGTH_ADV = 120;

// Helper to trim leading/trailing whitespace (if not already globally available)
static std::string trim_string_adv(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, (end - start + 1));
}

// Helper map for string to ChatEventType_e conversion
static std::map<std::string, ChatEventType_e> InitChatEventMap() {
    std::map<std::string, ChatEventType_e> m;
    m["CHAT_EVENT_GENERIC_IDLE"] = CHAT_EVENT_GENERIC_IDLE;
    m["CHAT_EVENT_WELCOME_MESSAGE"] = CHAT_EVENT_WELCOME_MESSAGE;
    m["CHAT_EVENT_GOT_KILL"] = CHAT_EVENT_GOT_KILL;
    m["CHAT_EVENT_GOT_KILL_HEADSHOT"] = CHAT_EVENT_GOT_KILL_HEADSHOT;
    m["CHAT_EVENT_GOT_KILL_REVENGE"] = CHAT_EVENT_GOT_KILL_REVENGE;
    m["CHAT_EVENT_GOT_MULTI_KILL"] = CHAT_EVENT_GOT_MULTI_KILL;
    m["CHAT_EVENT_WAS_KILLED_BY_ENEMY"] = CHAT_EVENT_WAS_KILLED_BY_ENEMY;
    m["CHAT_EVENT_WAS_KILLED_BY_TEAMMATE"] = CHAT_EVENT_WAS_KILLED_BY_TEAMMATE;
    m["CHAT_EVENT_LOW_HEALTH"] = CHAT_EVENT_LOW_HEALTH;
    m["CHAT_EVENT_RELOADING"] = CHAT_EVENT_RELOADING;
    m["CHAT_EVENT_ENEMY_TOOK_OUR_FLAG"] = CHAT_EVENT_ENEMY_TOOK_OUR_FLAG;
    m["CHAT_EVENT_WE_TOOK_ENEMY_FLAG"] = CHAT_EVENT_WE_TOOK_ENEMY_FLAG;
    m["CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG"] = CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG;
    m["CHAT_EVENT_WE_DROPPED_ENEMY_FLAG"] = CHAT_EVENT_WE_DROPPED_ENEMY_FLAG;
    m["CHAT_EVENT_OUR_FLAG_RETURNED"] = CHAT_EVENT_OUR_FLAG_RETURNED;
    m["CHAT_EVENT_ENEMY_FLAG_RETURNED"] = CHAT_EVENT_ENEMY_FLAG_RETURNED;
    m["CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG"] = CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG;
    m["CHAT_EVENT_WE_CAPTURING_POINT"] = CHAT_EVENT_WE_CAPTURING_POINT;
    m["CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT"] = CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT;
    m["CHAT_EVENT_WE_SECURED_POINT"] = CHAT_EVENT_WE_SECURED_POINT;
    m["CHAT_EVENT_ENEMY_SECURED_OUR_POINT"] = CHAT_EVENT_ENEMY_SECURED_OUR_POINT;
    m["CHAT_EVENT_POINT_UNDER_ATTACK"] = CHAT_EVENT_POINT_UNDER_ATTACK;
    m["CHAT_EVENT_BOMB_PLANTED_AS_T"] = CHAT_EVENT_BOMB_PLANTED_AS_T;
    m["CHAT_EVENT_BOMB_PLANTED_AS_CT"] = CHAT_EVENT_BOMB_PLANTED_AS_CT;
    m["CHAT_EVENT_BOMB_DEFUSED_AS_CT"] = CHAT_EVENT_BOMB_DEFUSED_AS_CT;
    m["CHAT_EVENT_BOMB_DEFUSED_AS_T"] = CHAT_EVENT_BOMB_DEFUSED_AS_T;
    m["CHAT_EVENT_HOSTAGES_RESCUED_AS_CT"] = CHAT_EVENT_HOSTAGES_RESCUED_AS_CT;
    m["CHAT_EVENT_HOSTAGES_RESCUED_AS_T"] = CHAT_EVENT_HOSTAGES_RESCUED_AS_T;
    m["CHAT_EVENT_BUY_TIME_START"] = CHAT_EVENT_BUY_TIME_START;
    m["CHAT_EVENT_ROUND_START"] = CHAT_EVENT_ROUND_START;
    m["CHAT_EVENT_ROUND_WIN_TEAM"] = CHAT_EVENT_ROUND_WIN_TEAM;
    m["CHAT_EVENT_ROUND_LOSE_TEAM"] = CHAT_EVENT_ROUND_LOSE_TEAM;
    m["CHAT_EVENT_MATCH_WIN_TEAM"] = CHAT_EVENT_MATCH_WIN_TEAM;
    m["CHAT_EVENT_MATCH_LOSE_TEAM"] = CHAT_EVENT_MATCH_LOSE_TEAM;
    m["CHAT_EVENT_REQUEST_MEDIC_TFC"] = CHAT_EVENT_REQUEST_MEDIC_TFC;
    m["CHAT_EVENT_REQUEST_AMMO"] = CHAT_EVENT_REQUEST_AMMO;
    m["CHAT_EVENT_REQUEST_BACKUP"] = CHAT_EVENT_REQUEST_BACKUP;
    m["CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED"] = CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED;
    m["CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED"] = CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED;
    m["CHAT_EVENT_INFO_INCOMING_GRENADE"] = CHAT_EVENT_INFO_INCOMING_GRENADE;
    m["CHAT_EVENT_PLAYER_TAUNT_RESPONSE"] = CHAT_EVENT_PLAYER_TAUNT_RESPONSE;
    m["CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE"] = CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE;
    m["CHAT_EVENT_AGREE_WITH_TEAMMATE"] = CHAT_EVENT_AGREE_WITH_TEAMMATE;
    m["CHAT_EVENT_DISAGREE_WITH_TEAMMATE"] = CHAT_EVENT_DISAGREE_WITH_TEAMMATE;
    // Developer must ensure all ChatEventType_e string names are added here
    return m;
}
static const std::map<std::string, ChatEventType_e> chat_event_string_map = InitChatEventMap();

void AdvancedChat_LoadChatFile(const char* filename) {
    char full_path[256];
    // Assuming filename is base like "HPB_bot_adv_chat.txt"
    // UTIL_BuildFileName prepends the mod directory.
    UTIL_BuildFileName(full_path, (char*)filename, NULL);

    std::ifstream file(full_path);
    if (!file.is_open()) {
        SERVER_PRINT("AdvChat: Could not open chat file: %s\n", full_path);
        // Attempt fallback to valve/filename
        char valve_path[256];
        // Construct path like "valve/HPB_bot_adv_chat.txt"
        sprintf(valve_path, "valve/%s", filename);
        // UTIL_BuildFileName might not be appropriate here if it always prepends current mod dir.
        // Direct fopen might be better for specific valve path.
        // For now, let's assume this forms a path like "mod_dir/valve/HPB_bot_adv_chat.txt" which is likely wrong.
        // Correct fallback should be just "valve/HPB_bot_adv_chat.txt" relative to base game dir.
        // This part of fallback needs to be OS-agnostic and aware of game's file structure.
        // Sticking to simpler UTIL_BuildFileName for the primary path for now.
        // Fallback logic as in original prompt:
        char temp_fn[256];
        strcpy(temp_fn, "valve/");
        strcat(temp_fn, filename);
        // We should check if UTIL_BuildFileName handles this correctly or if direct path is needed.
        // For now, we will assume direct path for fallback.
        FILE* fallback_fp = fopen(temp_fn, "r"); // Use direct path for valve fallback
        if (!fallback_fp) {
           SERVER_PRINT("AdvChat: Fallback - Could not open chat file: %s\n", temp_fn);
           return;
        }
        SERVER_PRINT("AdvChat: Using fallback chat file: %s\n", temp_fn);
        fclose(fallback_fp); // Close if only checking existence, then reopen with ifstream
        file.open(temp_fn); // Re-open with ifstream
        if (!file.is_open()) { // Should not happen if fopen succeeded, but good check
            SERVER_PRINT("AdvChat: Fallback ifstream failed for: %s\n", temp_fn);
            return;
        }
    }

    g_categorized_chat_lines.clear();
    std::string line;
    ChatEventType_e current_category = NUM_CHAT_EVENT_TYPES;
    int line_number = 0;
    int total_messages_loaded = 0;

    while (std::getline(file, line)) {
        line_number++;
        std::string trimmed_line = trim_string_adv(line);

        if (trimmed_line.empty() || trimmed_line.rfind("//", 0) == 0 || trimmed_line.rfind("#", 0) == 0) {
            continue;
        }

        if (trimmed_line.front() == '[' && trimmed_line.back() == ']') {
            std::string category_str = trimmed_line.substr(1, trimmed_line.length() - 2);
            auto map_iter = chat_event_string_map.find(category_str);
            if (map_iter != chat_event_string_map.end()) {
                current_category = map_iter->second;
            } else {
                current_category = NUM_CHAT_EVENT_TYPES;
                SERVER_PRINT("AdvChat: Unknown chat category: %s at line %d\n", category_str.c_str(), line_number);
            }
        } else if (current_category != NUM_CHAT_EVENT_TYPES) {
            AdvancedChatMessage_t msg;
            const char* text_start = trimmed_line.c_str();
            if (trimmed_line[0] == '!') {
                msg.can_modify_runtime = false;
                text_start++;
            } else {
                msg.can_modify_runtime = true;
            }
            msg.text = std::string(text_start);

            if (!msg.text.empty()) {
                g_categorized_chat_lines[current_category].push_back(msg);
                total_messages_loaded++;
            }
        }
    }
    file.close();
    SERVER_PRINT("AdvChat: Loaded %d categorized chat messages from %zu categories from %s.\n",
          total_messages_loaded, g_categorized_chat_lines.size(), filename);
}

std::string ReplacePlaceholders(std::string text, bot_t* pBot, edict_t* pSubject1, edict_t* pSubject2, const char* custom_str) {
    size_t pos;

    if (pBot && pBot->name[0]) {
        std::string self_name_placeholder = "{self_name}";
        while ((pos = text.find(self_name_placeholder)) != std::string::npos) {
            text.replace(pos, self_name_placeholder.length(), pBot->name);
        }
    }

    if (pSubject1 && !FNullEnt(pSubject1) && STRING(pSubject1->v.netname) && STRING(pSubject1->v.netname)[0]) {
        const char* subject1_name = STRING(pSubject1->v.netname);
        std::string victim_placeholder = "{victim_name}";
        std::string killer_placeholder = "{killer_name}";
        std::string enemy_placeholder = "{enemy_name}";
        std::string teammate_placeholder = "{teammate_name}"; // Basic, assumes pSubject1 is teammate if this placeholder used
        while ((pos = text.find(victim_placeholder)) != std::string::npos) text.replace(pos, victim_placeholder.length(), subject1_name);
        while ((pos = text.find(killer_placeholder)) != std::string::npos) text.replace(pos, killer_placeholder.length(), subject1_name);
        while ((pos = text.find(enemy_placeholder)) != std::string::npos) text.replace(pos, enemy_placeholder.length(), subject1_name);
        while ((pos = text.find(teammate_placeholder)) != std::string::npos) text.replace(pos, teammate_placeholder.length(), subject1_name);
    }

    if (custom_str) {
        std::string objective_placeholder = "{objective_name}";
        std::string weapon_placeholder_custom = "{weapon_name}";
        while ((pos = text.find(objective_placeholder)) != std::string::npos) text.replace(pos, objective_placeholder.length(), custom_str);
        if (!(pSubject2 && !FNullEnt(pSubject2) && STRING(pSubject2->v.classname) && STRING(pSubject2->v.classname)[0])) {
             if (strstr(custom_str, "weapon_") || strstr(custom_str, "ammo_")) { // Check if custom_str itself is a weapon name
                while ((pos = text.find(weapon_placeholder_custom)) != std::string::npos) text.replace(pos, weapon_placeholder_custom.length(), custom_str);
             }
        }
    }

    if (pSubject2 && !FNullEnt(pSubject2) && STRING(pSubject2->v.classname) && STRING(pSubject2->v.classname)[0]) {
         std::string weapon_placeholder_entity = "{weapon_name}";
         while ((pos = text.find(weapon_placeholder_entity)) != std::string::npos) text.replace(pos, weapon_placeholder_entity.length(), STRING(pSubject2->v.classname));
    }

    // Cleanup any unfilled placeholders like {victim_name} to avoid literal output
    std::string placeholders_to_clean[] = {"{self_name}", "{victim_name}", "{killer_name}", "{enemy_name}", "{teammate_name}", "{objective_name}", "{weapon_name}"};
    for(const auto& placeholder : placeholders_to_clean) {
        while((pos = text.find(placeholder)) != std::string::npos) {
            text.replace(pos, placeholder.length(), ""); // Replace with empty string
        }
    }
    return text;
}

void AdvancedChat_HandleGameEvent(bot_t* pBot, ChatEventType_e event_type,
                                  edict_t* pSubjectEntity1, edict_t* pSubjectEntity2,
                                  const char* custom_data_str) {
    if (!pBot || !pBot->is_used || !pBot->pEdict || !gpGlobals || FNullEnt(pBot->pEdict)) return;

    if (RANDOM_LONG(1, 100) > pBot->chat_percent) {
        return;
    }

    if (gpGlobals->time < pBot->f_bot_chat_time) {
        return;
    }

    auto category_iter = g_categorized_chat_lines.find(event_type);
    if (category_iter == g_categorized_chat_lines.end() || category_iter->second.empty()) {
        return;
    }

    const std::vector<AdvancedChatMessage_t>& messages = category_iter->second;
    const AdvancedChatMessage_t& chosen_template = messages[RANDOM_LONG(0, messages.size() - 1)];

    std::string processed_text = ReplacePlaceholders(chosen_template.text, pBot, pSubjectEntity1, pSubjectEntity2, custom_data_str);

    // Final trim and length check
    processed_text = trim_string_adv(processed_text);
    if (!processed_text.empty() && processed_text.length() < MAX_CHAT_OUTPUT_LENGTH_ADV) {
        int chat_mode = 0; // 0 for global, 1 for team

        // Determine chat mode based on event type (add more as needed)
        switch(event_type) {
            case CHAT_EVENT_ENEMY_TOOK_OUR_FLAG:
            case CHAT_EVENT_WE_TOOK_ENEMY_FLAG:
            case CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG:
            case CHAT_EVENT_WE_DROPPED_ENEMY_FLAG:
            case CHAT_EVENT_OUR_FLAG_RETURNED:
            case CHAT_EVENT_ENEMY_FLAG_RETURNED:
            case CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG:
            case CHAT_EVENT_WE_CAPTURING_POINT:
            case CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT:
            case CHAT_EVENT_WE_SECURED_POINT:
            case CHAT_EVENT_ENEMY_SECURED_OUR_POINT:
            case CHAT_EVENT_POINT_UNDER_ATTACK:
            case CHAT_EVENT_REQUEST_MEDIC_TFC:
            case CHAT_EVENT_REQUEST_AMMO:
            case CHAT_EVENT_REQUEST_BACKUP:
            case CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED:
            case CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED:
            case CHAT_EVENT_INFO_INCOMING_GRENADE:
            case CHAT_EVENT_AGREE_WITH_TEAMMATE:
            case CHAT_EVENT_DISAGREE_WITH_TEAMMATE:
            // Round/match events that are usually team-relevant if teamplay is on
            case CHAT_EVENT_ROUND_START: // Could be global or team
            case CHAT_EVENT_ROUND_WIN_TEAM:
            case CHAT_EVENT_ROUND_LOSE_TEAM:
                chat_mode = 1; // Team chat
                break;
            default:
                chat_mode = 0; // Global chat
                break;
        }

        UTIL_HostSay(pBot->pEdict, chat_mode, (char*)processed_text.c_str());

        pBot->f_bot_chat_time = gpGlobals->time + RANDOM_FLOAT(MIN_CHAT_INTERVAL_ADV, MAX_CHAT_INTERVAL_ADV);
    }
}
