#ifndef BOT_NGRAM_FUNCTIONS_H
#define BOT_NGRAM_FUNCTIONS_H

#include "bot_ngram_structs.h" // For NgramModel_t

// Global instance of the N-gram model, defined in bot_advanced_chat.cpp
extern NgramModel_t g_chat_ngram_model;

// Loads pre-trained N-gram model data from a file.
bool AdvancedChat_LoadNgramData(const char* filename);

// Tokenizes a string into words based on delimiters.
// Default delimiters include space, tab, newline, and common punctuation.
std::vector<std::string> AdvancedChat_TokenizeString(const std::string& str, const std::string& delimiters = " \t\n.,!?;:");

// Joins a vector of words into a single string with a specified separator.
// Defaults to a single space as separator.
std::string AdvancedChat_JoinWords(const std::vector<std::string>& words, const std::string& separator = " ");

// Generates a sentence using the N-gram model, starting with an optional seed phrase.
std::string AdvancedChat_GenerateNgramSentence(const NgramModel_t* model, const std::string& seed_phrase, int max_length);

#endif // BOT_NGRAM_FUNCTIONS_H
