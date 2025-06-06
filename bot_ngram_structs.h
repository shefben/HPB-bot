#ifndef BOT_NGRAM_STRUCTS_H
#define BOT_NGRAM_STRUCTS_H

#include <string>
#include <map>
#include <vector> // Included as it's contextually relevant for N-gram processing

#define NGRAM_KEY_DELIMITER "||"    // Delimiter for joining words in a prefix key

// Stores possible next words and their frequencies for a given prefix.
typedef std::map<std::string, int> NgramContinuationsMap_t; // Key: next_word, Value: frequency_count

// The main N-gram model structure.
typedef struct {
    int n_value; // The 'N' in N-gram (e.g., 2 for bigram, 3 for trigram)
    // Key: A string representing (N-1) words (the prefix/context), potentially joined by a delimiter.
    std::map<std::string, NgramContinuationsMap_t> model_data;
} NgramModel_t;

#endif // BOT_NGRAM_STRUCTS_H
