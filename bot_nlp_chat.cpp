#include "bot_nlp_chat.h"
#include "bot.h" // For UTIL_BuildFileName, ALERT, at_console, gpGlobals
#include <stdio.h>
#include <string.h> // For strlen, strncpy, stricmp (or strcasecmp for POSIX)
#include <ctype.h>  // For tolower, isspace
#include <string>   // Ensure std::string is fully available
#include <vector>   // Ensure std::vector is fully available
#include <map>      // Ensure std::map is fully available

// --- Categorized Chat System ---
std::map<ChatEventType_e, std::vector<AdvancedChatMessage_t>> g_categorized_chat_lines;

// Helper to trim leading/trailing whitespace
static std::string trim_string(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) return ""; // Empty or all whitespace
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, (end - start + 1));
}

// Helper to convert category string to ChatEventType_e enum
ChatEventType_e StringToChatEventType(const std::string& category_string) {
    if (category_string == "CHAT_EVENT_GENERIC_IDLE") return CHAT_EVENT_GENERIC_IDLE;
    if (category_string == "CHAT_EVENT_WELCOME_MESSAGE") return CHAT_EVENT_WELCOME_MESSAGE;
    if (category_string == "CHAT_EVENT_GOT_KILL") return CHAT_EVENT_GOT_KILL;
    if (category_string == "CHAT_EVENT_GOT_KILL_HEADSHOT") return CHAT_EVENT_GOT_KILL_HEADSHOT;
    if (category_string == "CHAT_EVENT_GOT_KILL_REVENGE") return CHAT_EVENT_GOT_KILL_REVENGE;
    if (category_string == "CHAT_EVENT_GOT_MULTI_KILL") return CHAT_EVENT_GOT_MULTI_KILL;
    if (category_string == "CHAT_EVENT_WAS_KILLED_BY_ENEMY") return CHAT_EVENT_WAS_KILLED_BY_ENEMY;
    if (category_string == "CHAT_EVENT_WAS_KILLED_BY_TEAMMATE") return CHAT_EVENT_WAS_KILLED_BY_TEAMMATE;
    if (category_string == "CHAT_EVENT_LOW_HEALTH") return CHAT_EVENT_LOW_HEALTH;
    if (category_string == "CHAT_EVENT_RELOADING") return CHAT_EVENT_RELOADING;
    if (category_string == "CHAT_EVENT_ENEMY_TOOK_OUR_FLAG") return CHAT_EVENT_ENEMY_TOOK_OUR_FLAG;
    if (category_string == "CHAT_EVENT_WE_TOOK_ENEMY_FLAG") return CHAT_EVENT_WE_TOOK_ENEMY_FLAG;
    if (category_string == "CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG") return CHAT_EVENT_ENEMY_DROPPED_OUR_FLAG;
    if (category_string == "CHAT_EVENT_WE_DROPPED_ENEMY_FLAG") return CHAT_EVENT_WE_DROPPED_ENEMY_FLAG;
    if (category_string == "CHAT_EVENT_OUR_FLAG_RETURNED") return CHAT_EVENT_OUR_FLAG_RETURNED;
    if (category_string == "CHAT_EVENT_ENEMY_FLAG_RETURNED") return CHAT_EVENT_ENEMY_FLAG_RETURNED;
    if (category_string == "CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG") return CHAT_EVENT_WE_CAPTURED_ENEMY_FLAG;
    if (category_string == "CHAT_EVENT_WE_CAPTURING_POINT") return CHAT_EVENT_WE_CAPTURING_POINT;
    if (category_string == "CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT") return CHAT_EVENT_ENEMY_CAPTURING_OUR_POINT;
    if (category_string == "CHAT_EVENT_WE_SECURED_POINT") return CHAT_EVENT_WE_SECURED_POINT;
    if (category_string == "CHAT_EVENT_ENEMY_SECURED_OUR_POINT") return CHAT_EVENT_ENEMY_SECURED_OUR_POINT;
    if (category_string == "CHAT_EVENT_POINT_UNDER_ATTACK") return CHAT_EVENT_POINT_UNDER_ATTACK;
    if (category_string == "CHAT_EVENT_BOMB_PLANTED_AS_T") return CHAT_EVENT_BOMB_PLANTED_AS_T;
    if (category_string == "CHAT_EVENT_BOMB_PLANTED_AS_CT") return CHAT_EVENT_BOMB_PLANTED_AS_CT;
    if (category_string == "CHAT_EVENT_BOMB_DEFUSED_AS_CT") return CHAT_EVENT_BOMB_DEFUSED_AS_CT;
    if (category_string == "CHAT_EVENT_BOMB_DEFUSED_AS_T") return CHAT_EVENT_BOMB_DEFUSED_AS_T;
    if (category_string == "CHAT_EVENT_HOSTAGES_RESCUED_AS_CT") return CHAT_EVENT_HOSTAGES_RESCUED_AS_CT;
    if (category_string == "CHAT_EVENT_HOSTAGES_RESCUED_AS_T") return CHAT_EVENT_HOSTAGES_RESCUED_AS_T;
    if (category_string == "CHAT_EVENT_BUY_TIME_START") return CHAT_EVENT_BUY_TIME_START;
    if (category_string == "CHAT_EVENT_ROUND_START") return CHAT_EVENT_ROUND_START;
    if (category_string == "CHAT_EVENT_ROUND_WIN_TEAM") return CHAT_EVENT_ROUND_WIN_TEAM;
    if (category_string == "CHAT_EVENT_ROUND_LOSE_TEAM") return CHAT_EVENT_ROUND_LOSE_TEAM;
    if (category_string == "CHAT_EVENT_MATCH_WIN_TEAM") return CHAT_EVENT_MATCH_WIN_TEAM;
    if (category_string == "CHAT_EVENT_MATCH_LOSE_TEAM") return CHAT_EVENT_MATCH_LOSE_TEAM;
    if (category_string == "CHAT_EVENT_REQUEST_MEDIC_TFC") return CHAT_EVENT_REQUEST_MEDIC_TFC;
    if (category_string == "CHAT_EVENT_REQUEST_AMMO") return CHAT_EVENT_REQUEST_AMMO;
    if (category_string == "CHAT_EVENT_REQUEST_BACKUP") return CHAT_EVENT_REQUEST_BACKUP;
    if (category_string == "CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED") return CHAT_EVENT_INFO_ENEMY_SNIPER_SPOTTED;
    if (category_string == "CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED") return CHAT_EVENT_INFO_ENEMY_SENTRY_SPOTTED;
    if (category_string == "CHAT_EVENT_INFO_INCOMING_GRENADE") return CHAT_EVENT_INFO_INCOMING_GRENADE;
    if (category_string == "CHAT_EVENT_PLAYER_TAUNT_RESPONSE") return CHAT_EVENT_PLAYER_TAUNT_RESPONSE;
    if (category_string == "CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE") return CHAT_EVENT_PLAYER_COMPLIMENT_RESPONSE;
    if (category_string == "CHAT_EVENT_AGREE_WITH_TEAMMATE") return CHAT_EVENT_AGREE_WITH_TEAMMATE;
    if (category_string == "CHAT_EVENT_DISAGREE_WITH_TEAMMATE") return CHAT_EVENT_DISAGREE_WITH_TEAMMATE;

    // ALERT(at_console, "NLP_Chat: Unknown category in chat file: %s\n", category_string.c_str());
    return NUM_CHAT_EVENT_TYPES;
}

void AdvancedChat_LoadChatFile(const char* base_filename) {
    g_categorized_chat_lines.clear();

    char full_filename[256];
    UTIL_BuildFileName(full_filename, (char*)base_filename, NULL);

    FILE* fp = fopen(full_filename, "r");
    if (!fp) {
        ALERT(at_console, "NLP_Chat: Advanced corpus file NOT FOUND: %s\n", full_filename);
        char valve_filename[256];
        strcpy(valve_filename, "valve/");
        strcat(valve_filename, base_filename);
        fp = fopen(valve_filename, "r");
        if (!fp) {
           ALERT(at_console, "NLP_Chat: Fallback advanced corpus file NOT FOUND: %s\n", valve_filename);
           return;
        } else {
           ALERT(at_console, "NLP_Chat: Using fallback advanced corpus file: %s\n", valve_filename);
        }
    }

    char line_buffer[1024];
    ChatEventType_e current_category = NUM_CHAT_EVENT_TYPES;
    int line_number = 0;
    int total_messages_loaded = 0;

    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        line_number++;
        std::string line_str(line_buffer);
        std::string trimmed_line = trim_string(line_str);

        if (trimmed_line.empty() || trimmed_line.rfind("//", 0) == 0 || trimmed_line.rfind("#", 0) == 0) {
            continue;
        }

        if (trimmed_line.front() == '[' && trimmed_line.back() == ']') {
            std::string category_name = trimmed_line.substr(1, trimmed_line.length() - 2);
            current_category = StringToChatEventType(category_name);
            if (current_category == NUM_CHAT_EVENT_TYPES) {
                ALERT(at_console, "NLP_Chat: Unknown category '%s' at line %d in %s\n", category_name.c_str(), line_number, base_filename);
            }
            continue;
        }

        if (current_category != NUM_CHAT_EVENT_TYPES) {
            AdvancedChatMessage_t msg;
            msg.can_modify_runtime = true;
            const char* text_start = trimmed_line.c_str();

            if (trimmed_line[0] == '!') {
                msg.can_modify_runtime = false;
                text_start = trimmed_line.c_str() + 1;
            }

            msg.text = std::string(text_start);

            if (!msg.text.empty()) {
                g_categorized_chat_lines[current_category].push_back(msg);
                total_messages_loaded++;
            }
        }
    }
    fclose(fp);
    ALERT(at_console, "NLP_Chat: Loaded %d categorized chat messages from %d categories from %s.\n",
          total_messages_loaded, (int)g_categorized_chat_lines.size(), base_filename);
}


// --- N-gram Model System (Placeholders/Originals) ---
NgramModel_t g_chat_ngram_model;
int g_ngram_model_N_value = 3; // Default N value

// Loads the general chat corpus for N-gram model training (and potentially other NLP tasks)
void NLP_LoadCorpusFromFile(const char* base_filename, const char* mod_name_for_path_unused, std::vector<std::string>& out_sentences) {
    out_sentences.clear();
    char full_filename[256];
    UTIL_BuildFileName(full_filename, (char*)base_filename, NULL); // Assumes HPB_bot_chat.txt is at mod_name/HPB_bot_chat.txt

    FILE* fp = fopen(full_filename, "r");
    if (!fp) {
        ALERT(at_console, "NLP_Corpus: File NOT FOUND: %s\n", full_filename);
        char valve_filename[256];
        strcpy(valve_filename, "valve/");
        strcat(valve_filename, base_filename);
        fp = fopen(valve_filename, "r");
        if (!fp) {
           ALERT(at_console, "NLP_Corpus: Fallback file NOT FOUND: %s\n", valve_filename);
           return;
        } else {
           ALERT(at_console, "NLP_Corpus: Using fallback file: %s\n", valve_filename);
        }
    }

    char line_buffer[512];
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        size_t len = strlen(line_buffer);
        while (len > 0 && (line_buffer[len-1] == '\n' || line_buffer[len-1] == '\r')) {
            line_buffer[--len] = '\0';
        }

        std::string trimmed_line_for_check = trim_string(std::string(line_buffer));
        if (trimmed_line_for_check.rfind("//", 0) == 0 ||
            trimmed_line_for_check.rfind("#", 0) == 0 ||
            (trimmed_line_for_check.front() == '[' && trimmed_line_for_check.back() == ']')) {
            continue;
        }

        char* actual_text_start = line_buffer;
        if (line_buffer[0] == '!') { // From original HPB_bot_chat format
            actual_text_start = line_buffer + 1;
        }

        std::string sentence(actual_text_start);
        sentence = trim_string(sentence);

        for (size_t i = 0; i < sentence.length(); ++i) {
            sentence[i] = tolower(sentence[i]);
        }

        if (!sentence.empty()) {
            out_sentences.push_back(sentence);
        }
    }
    fclose(fp);
    // ALERT(at_console, "NLP_Corpus: Loaded %zu sentences from %s for N-gram model.\n", out_sentences.size(), base_filename);
}


void NLP_TrainModel(const std::vector<std::string>& sentences, int N, NgramModel_t& out_model) {
    out_model.clear();
    if (N < 2) {
        ALERT(at_console, "NLP_TrainModel: N must be at least 2 for N-grams.\n");
        return;
    }

    for (const std::string& sentence : sentences) {
        std::vector<std::string> words;
        std::string current_word;
        for (char ch : sentence) {
            if (std::isspace(ch)) {
                if (!current_word.empty()) {
                    words.push_back(current_word);
                    current_word.clear();
                }
            } else {
                current_word += ch;
            }
        }
        if (!current_word.empty()) {
            words.push_back(current_word);
        }

        if (words.size() < (size_t)N) {
            continue;
        }

        for (size_t i = 0; i <= words.size() - N; ++i) {
            NlpNgramContext context;
            for (int j = 0; j < N - 1; ++j) {
                context.push_back(words[i + j]);
            }
            std::string next_word = words[i + N - 1];
            out_model[context][next_word]++;
        }
    }
    // ALERT(at_console, "NLP_TrainModel: N-gram model training complete. Contexts: %zu\n", out_model.size());
}

std::string NLP_GenerateChatMessage(const NgramModel_t& model, int N, int max_words, const std::vector<std::string>& seed_context_optional) {
    if (model.empty() || N < 2 || max_words <= 0) {
        return "No data for chat.";
    }

    std::vector<std::string> current_sentence;
    NlpNgramContext current_context;

    // Initialize context
    if (!seed_context_optional.empty() && seed_context_optional.size() >= (size_t)(N - 1)) {
        // Use last N-1 words from seed if possible
        for(size_t i = seed_context_optional.size() - (N - 1); i < seed_context_optional.size(); ++i) {
            current_context.push_back(seed_context_optional[i]);
            current_sentence.push_back(seed_context_optional[i]);
        }
    } else if (!seed_context_optional.empty()) { // Seed is smaller than N-1
        current_context = seed_context_optional;
        current_sentence = seed_context_optional;
    }

    // If context is still too short (or no seed), try to pick a random starting context from the model
    if (current_context.size() < (size_t)(N - 1)) {
        if (!model.empty()) {
            int random_start_idx = rand() % model.size();
            auto it = model.begin();
            std::advance(it, random_start_idx);
            current_context = it->first; // Use a random context from the model
            for(const auto& word : current_context) {
                 if (current_sentence.size() < (size_t)max_words) current_sentence.push_back(word);
            }
        } else {
             return "Not enough context to start chat.";
        }
    }


    for (int i = 0; i < max_words - (N-1) ; ++i) { // Generate up to max_words
        auto model_it = model.find(current_context);
        if (model_it == model.end() || model_it->second.empty()) {
            break; // No known words follow this context
        }

        const NextWordCounts& next_options = model_it->second;
        int total_counts = 0;
        for (const auto& pair : next_options) {
            total_counts += pair.second;
        }

        if (total_counts == 0) break;

        int random_val = rand() % total_counts;
        std::string chosen_word;
        int cumulative_count = 0;
        for (const auto& pair : next_options) {
            cumulative_count += pair.second;
            if (random_val < cumulative_count) {
                chosen_word = pair.first;
                break;
            }
        }

        if (chosen_word.empty()) break; // Should not happen if total_counts > 0

        current_sentence.push_back(chosen_word);
        if (current_sentence.size() >= (size_t)max_words) break;

        // Update context for next word
        if (N > 1) {
            current_context.erase(current_context.begin());
            current_context.push_back(chosen_word);
        } else { // Should not happen if N >= 2
            break;
        }
    }

    std::string result_sentence;
    for (size_t i = 0; i < current_sentence.size(); ++i) {
        result_sentence += current_sentence[i] + (i == current_sentence.size() - 1 ? "" : " ");
    }
    return result_sentence;
}
