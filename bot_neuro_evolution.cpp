#include "bot_neuro_evolution.h"
#include <cmath>      // For expf (sigmoid) or tanhf
#include <cstdlib>    // For rand, RAND_MAX
#include <vector>     // For std::vector (already in .h but good practice)
#include <numeric>    // For std::inner_product (optional, can do manual loops)
#include <algorithm>  // For std::max_element (optional), std::fill
#include <limits>     // For std::numeric_limits

// If using ALERT for debugging, include relevant headers like meta_api.h or bot.h
// Example:
// #include "extdll.h"
// #include "util.h" // For ALERT
// For gpGlobals, IsAlive, ENTINDEX, UTIL_GetBotPointer
#include "extdll.h"
#include "bot.h" // For bot_t, IsAlive, UTIL_GetBotPointer, etc.
#include "bot_objective_discovery.h" // For GameEvent_t
#include "bot_tactical_ai.h"       // For GlobalTacticalState_t, GetGlobalTacticalState()

// Additional includes for EA
#include <algorithm> // For std::sort, std::min, std::max, std::swap (used by EA funcs)
#include <vector>    // For std::vector (already used, but good to list for EA section)
#include <limits>    // For std::numeric_limits (already used, but good to list for EA section)
// rand() and RAND_MAX are from cstdlib, which is implicitly included by other headers like cmath or algorithm often.

extern globalvars_t  *gpGlobals; // From engine

// Extern declarations for CVars defined in dll.cpp
extern cvar_t bot_ne_mutation_rate;
extern cvar_t bot_ne_mutation_strength;
extern cvar_t bot_ne_tournament_size;
extern cvar_t bot_ne_num_elites;
// bot_ne_generation_period and bot_ne_min_population are used in dll.cpp's StartFrame directly.


// Fitness Component Weights
const float FIT_OBJ_CAPTURE_WEIGHT = 100.0f;
const float FIT_SCORE_CONTRIB_WEIGHT = 10.0f;
const float FIT_KILL_WEIGHT = 10.0f;
const float FIT_DEATH_PENALTY_WEIGHT = 5.0f;
const float FIT_SURVIVAL_TIME_WEIGHT = 0.1f;
const float FIT_DAMAGE_DEALT_WEIGHT = 0.01f;


// --- Activation Functions (Static) ---
static float nn_sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

// static float nn_tanh(float x) {
//     return tanhf(x);
// }

// static float nn_relu(float x) {
//     return x > 0.0f ? x : 0.0f;
// }


// --- Neural Network Operations ---

void NN_Initialize(TacticalNeuralNetwork_t* nn, int num_inputs, int num_hidden, int num_outputs,
                   bool initialize_with_random_weights, const float* initial_weights_data) {
    if (!nn) return;

    nn->num_input_neurons = num_inputs;
    nn->num_hidden_neurons = num_hidden;
    nn->num_output_neurons = num_outputs;

    // Resize vectors
    nn->weights_input_hidden.resize((size_t)num_inputs * num_hidden);
    nn->bias_hidden.resize(num_hidden);
    nn->weights_hidden_output.resize((size_t)num_hidden * num_outputs);
    nn->bias_output.resize(num_outputs);
    nn->hidden_layer_output_activations.resize(num_hidden);

    if (initialize_with_random_weights) {
        // Initialize with small random values (e.g., between -0.5 and 0.5 or -1 and 1)
        // Ensure srand() has been called once globally (e.g., in GameDLLInit)
        float range = 1.0f;
        float offset = range / 2.0f;

        for (size_t i = 0; i < nn->weights_input_hidden.size(); ++i) {
            nn->weights_input_hidden[i] = ((float)rand() / (float)RAND_MAX) * range - offset;
        }
        for (size_t i = 0; i < nn->bias_hidden.size(); ++i) {
            nn->bias_hidden[i] = ((float)rand() / (float)RAND_MAX) * range - offset;
        }
        for (size_t i = 0; i < nn->weights_hidden_output.size(); ++i) {
            nn->weights_hidden_output[i] = ((float)rand() / (float)RAND_MAX) * range - offset;
        }
        for (size_t i = 0; i < nn->bias_output.size(); ++i) {
            nn->bias_output[i] = ((float)rand() / (float)RAND_MAX) * range - offset;
        }
    } else if (initial_weights_data) {
        const float* p = initial_weights_data;
        size_t idx = 0;
        for (idx = 0; idx < nn->weights_input_hidden.size(); ++idx) nn->weights_input_hidden[idx] = *p++;
        for (idx = 0; idx < nn->bias_hidden.size(); ++idx) nn->bias_hidden[idx] = *p++;
        for (idx = 0; idx < nn->weights_hidden_output.size(); ++idx) nn->weights_hidden_output[idx] = *p++;
        for (idx = 0; idx < nn->bias_output.size(); ++idx) nn->bias_output[idx] = *p++;
    } else {
        // Zero-initialize if no random and no initial data
        std::fill(nn->weights_input_hidden.begin(), nn->weights_input_hidden.end(), 0.0f);
        std::fill(nn->bias_hidden.begin(), nn->bias_hidden.end(), 0.0f);
        std::fill(nn->weights_hidden_output.begin(), nn->weights_hidden_output.end(), 0.0f);
        std::fill(nn->bias_output.begin(), nn->bias_output.end(), 0.0f);
    }
}

void NN_FeedForward(TacticalNeuralNetwork_t* nn, const float* inputs, float* raw_outputs_buffer) {
    if (!nn || !inputs || !raw_outputs_buffer) return;
    if (nn->num_input_neurons <= 0 || nn->num_hidden_neurons <= 0 || nn->num_output_neurons <= 0) return;


    // Input to Hidden Layer
    for (int j = 0; j < nn->num_hidden_neurons; ++j) {
        float hidden_sum = 0.0f;
        for (int i = 0; i < nn->num_input_neurons; ++i) {
            // Weight index: input_idx * num_hidden_neurons + hidden_idx (Column-major if inputs are columns)
            // Or: hidden_idx * num_input_neurons + input_idx (Row-major if inputs are rows for weights matrix)
            // Assuming weights_input_hidden is flattened row-major: W[input_idx][hidden_idx] -> flat[input_idx * num_hidden + hidden_idx]
            hidden_sum += inputs[i] * nn->weights_input_hidden[i * nn->num_hidden_neurons + j];
        }
        hidden_sum += nn->bias_hidden[j];
        nn->hidden_layer_output_activations[j] = nn_sigmoid(hidden_sum);
    }

    // Hidden to Output Layer
    for (int k = 0; k < nn->num_output_neurons; ++k) {
        float output_sum = 0.0f;
        for (int j = 0; j < nn->num_hidden_neurons; ++j) {
            // Weight index: hidden_idx * num_output_neurons + output_idx
            output_sum += nn->hidden_layer_output_activations[j] * nn->weights_hidden_output[j * nn->num_output_neurons + k];
        }
        output_sum += nn->bias_output[k];
        raw_outputs_buffer[k] = output_sum; // Store raw sum (logit), no activation on output layer for Q-learning/policy gradients usually
                                          // Or apply sigmoid/softmax if output represents probabilities for classification
    }
}

TacticalDirective NN_GetBestDirective(const float* nn_raw_outputs, int num_outputs) {
    if (!nn_raw_outputs || num_outputs <= 0) {
        return TACTICS_SEARCH_FOR_OBJECTIVES; // Default or error directive
    }

    // This check is important. If it fails, it indicates a mismatch between the NN output size
    // and the number of defined tactical directives.
    // if (num_outputs != NUM_TACTICAL_DIRECTIVES) {
    //    ALERT(at_console, "NN_GetBestDirective: num_outputs (%d) != NUM_TACTICAL_DIRECTIVES (%d)\n", num_outputs, NUM_TACTICAL_DIRECTIVES);
    // }

    int best_index = 0;
    float max_activation = -std::numeric_limits<float>::infinity();

    for (int i = 0; i < num_outputs; ++i) {
        if (nn_raw_outputs[i] > max_activation) {
            max_activation = nn_raw_outputs[i];
            best_index = i;
        }
    }

    if (best_index >= 0 && best_index < NUM_TACTICAL_DIRECTIVES) {
         return (TacticalDirective)best_index;
    }
    // Fallback if best_index is somehow out of enum range, though loop logic should prevent this if num_outputs is correct.
    return TACTICS_SEARCH_FOR_OBJECTIVES;
}


const char* TacticalDirectiveToString(TacticalDirective directive) {
    switch (directive) {
        case TACTICS_ATTACK_OBJECTIVE_1: return "ATTACK_OBJ_1";
        case TACTICS_DEFEND_OBJECTIVE_1: return "DEFEND_OBJ_1";
        case TACTICS_ATTACK_OBJECTIVE_2: return "ATTACK_OBJ_2";
        case TACTICS_DEFEND_OBJECTIVE_2: return "DEFEND_OBJ_2";
        case TACTICS_ATTACK_OBJECTIVE_3: return "ATTACK_OBJ_3";
        case TACTICS_DEFEND_OBJECTIVE_3: return "DEFEND_OBJ_3";
        case TACTICS_GATHER_RESOURCES: return "GATHER_RESOURCES";
        case TACTICS_GROUP_UP_TEAM: return "GROUP_UP_TEAM";
        case TACTICS_HOLD_DEFENSIVE_AREA: return "HOLD_DEFENSIVE_AREA";
        case TACTICS_SEARCH_FOR_OBJECTIVES: return "SEARCH_FOR_OBJECTIVES";
        case TACTICS_FALLBACK_REGROUP: return "FALLBACK_REGROUP";
        case TACTICS_PURSUE_WEAK_ENEMIES: return "PURSUE_WEAK_ENEMIES";
        case NUM_TACTICAL_DIRECTIVES: return "NUM_TACTICAL_DIRECTIVES_INVALID"; // Should not happen
        default: return "UNKNOWN_DIRECTIVE";
    }
}

// --- Evolutionary Algorithm (EA) Operations ---

// Helper to get a random integer within a range (inclusive)
static int GetRandomInt(int min_val, int max_val) {
    if (min_val > max_val) std::swap(min_val, max_val);
    if (min_val == max_val) return min_val;
    return min_val + (rand() % (max_val - min_val + 1));
}

void NE_Selection_Tournament(const std::vector<bot_t*>& current_population_bots,
                             int tournament_k_size,
                             std::vector<const TacticalNeuralNetwork_t*>& selected_parents_nns) {
    if (current_population_bots.empty() || tournament_k_size <= 0) return;

    selected_parents_nns.clear();
    // We need to select enough parents to create a new generation of the same size.
    // Typically, crossover produces 2 offspring from 2 parents.
    // So, we usually need current_population_bots.size() parents if pairing them up.
    int num_selections_needed = current_population_bots.size();

    for (int i = 0; i < num_selections_needed; ++i) {
        bot_t* tournament_winner = NULL;
        float best_fitness_in_tournament = -std::numeric_limits<float>::infinity();

        if (current_population_bots.empty()) continue;

        for (int j = 0; j < tournament_k_size; ++j) {
            int random_idx = GetRandomInt(0, current_population_bots.size() - 1);
            bot_t* competitor = current_population_bots[random_idx];

            // Ensure competitor is valid and has an initialized NN before accessing fitness/NN
            if (competitor && competitor->is_used && competitor->nn_initialized) {
                if (competitor->fitness_score > best_fitness_in_tournament) {
                    best_fitness_in_tournament = competitor->fitness_score;
                    tournament_winner = competitor;
                }
            }
        }

        if (tournament_winner) {
            selected_parents_nns.push_back(&tournament_winner->tactical_nn);
        } else {
            // Fallback: if tournament somehow failed (e.g., all selected competitors were invalid)
            // try to pick any valid bot from the population.
            bool parent_found = false;
            for (bot_t* potential_fallback_bot : current_population_bots) {
                if (potential_fallback_bot && potential_fallback_bot->is_used && potential_fallback_bot->nn_initialized) {
                    selected_parents_nns.push_back(&potential_fallback_bot->tactical_nn);
                    parent_found = true;
                    break;
                }
            }
            // If still no parent found (e.g. population is all invalid bots), the vector will be smaller than desired.
            // The calling function (NE_PerformEvolutionaryCycle) will need to handle this.
        }
    }
}

void NN_FlattenWeights(const TacticalNeuralNetwork_t* nn, float* flat_array) {
    if (!nn || !flat_array) return;

    float* p = flat_array;
    size_t offset = 0;

    memcpy(p + offset, nn->weights_input_hidden.data(), nn->weights_input_hidden.size() * sizeof(float));
    offset += nn->weights_input_hidden.size();

    memcpy(p + offset, nn->bias_hidden.data(), nn->bias_hidden.size() * sizeof(float));
    offset += nn->bias_hidden.size();

    memcpy(p + offset, nn->weights_hidden_output.data(), nn->weights_hidden_output.size() * sizeof(float));
    offset += nn->weights_hidden_output.size();

    memcpy(p + offset, nn->bias_output.data(), nn->bias_output.size() * sizeof(float));
}


void NE_Crossover_SinglePoint(const TacticalNeuralNetwork_t* nn_parent1,
                              const TacticalNeuralNetwork_t* nn_parent2,
                              TacticalNeuralNetwork_t* nn_offspring1,
                              TacticalNeuralNetwork_t* nn_offspring2) {

    if (!nn_parent1 || !nn_parent2 || !nn_offspring1 || !nn_offspring2) return;

    // Offspring NNs should be initialized for structure (dimensions) before this call.
    // This function will overwrite their weights.
    // MAX_NN_WEIGHT_SIZE is used as the definitive size for the flat weight arrays.

    std::vector<float> parent1_flat_weights(MAX_NN_WEIGHT_SIZE);
    std::vector<float> parent2_flat_weights(MAX_NN_WEIGHT_SIZE);
    NN_FlattenWeights(nn_parent1, parent1_flat_weights.data());
    NN_FlattenWeights(nn_parent2, parent2_flat_weights.data());

    std::vector<float> offspring1_flat_weights(MAX_NN_WEIGHT_SIZE);
    std::vector<float> offspring2_flat_weights(MAX_NN_WEIGHT_SIZE);

    if (MAX_NN_WEIGHT_SIZE <= 1) {
        offspring1_flat_weights = parent1_flat_weights; // Copy all
        offspring2_flat_weights = parent2_flat_weights; // Copy all
    } else {
        int crossover_point = GetRandomInt(1, MAX_NN_WEIGHT_SIZE - 1); // Crossover point between 1 and size-1
        for (int i = 0; i < MAX_NN_WEIGHT_SIZE; ++i) {
            if (i < crossover_point) {
                offspring1_flat_weights[i] = parent1_flat_weights[i];
                offspring2_flat_weights[i] = parent2_flat_weights[i];
            } else {
                offspring1_flat_weights[i] = parent2_flat_weights[i];
                offspring2_flat_weights[i] = parent1_flat_weights[i];
            }
        }
    }

    // Load these new flat weights back into the offspring NN structures.
    // Offspring NNs must be pre-initialized with correct dimensions (same as parents).
    NN_Initialize(nn_offspring1, nn_parent1->num_input_neurons, nn_parent1->num_hidden_neurons, nn_parent1->num_output_neurons,
                  false, offspring1_flat_weights.data());
    NN_Initialize(nn_offspring2, nn_parent2->num_input_neurons, nn_parent2->num_hidden_neurons, nn_parent2->num_output_neurons,
                  false, offspring2_flat_weights.data());
}

static float GetRandomMutationValue(float strength) {
    // Generates a random float between -strength and +strength
    return (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * strength;
}

void NE_Mutation(TacticalNeuralNetwork_t* nn, float mutation_rate, float mutation_strength) {
    if (!nn) return;

    // Helper lambda to mutate a vector of floats
    auto mutate_vector = [&](std::vector<float>& vec) {
        for (size_t i = 0; i < vec.size(); ++i) {
            if (((float)rand() / (float)RAND_MAX) < mutation_rate) {
                vec[i] += GetRandomMutationValue(mutation_strength);
                // Optional: Clamp weights if they tend to grow too large/small
                // if (vec[i] > 5.0f) vec[i] = 5.0f;
                // else if (vec[i] < -5.0f) vec[i] = -5.0f;
            }
        }
    };

    mutate_vector(nn->weights_input_hidden);
    mutate_vector(nn->bias_hidden);
    mutate_vector(nn->weights_hidden_output);
    mutate_vector(nn->bias_output);
}

// Placeholder for NE_ResetBotFitnessStats and NE_UpdateFitnessStatsOnEvent, NE_CalculateFinalFitness
// These were implemented in a previous subtask. Assuming they are present.

// Comparison function for sorting bots by fitness (used by NE_PerformEvolutionaryCycle)
static bool CompareBotFitness(const bot_t* a, const bot_t* b) {
    if (!a || !b) return false;
    return a->fitness_score > b->fitness_score; // Sort descending (higher fitness is better)
}

void NE_ResetBotFitnessStats(bot_t* pBot) {
    if (!pBot) return;
    pBot->fitness_score = 0.0f;
    pBot->current_eval_score_contribution = 0;
    pBot->current_eval_objectives_captured_or_defended = 0;
    pBot->current_eval_kills = 0;
    pBot->current_eval_deaths = 0;
    pBot->current_eval_damage_dealt = 0.0f;
    pBot->current_eval_survival_start_time = gpGlobals->time;
}

void NE_UpdateFitnessStatsOnEvent(bot_t* pBot, GameEventType_e event_type, const GameEvent_t* event_data) {
    if (!pBot || !pBot->is_used || !pBot->nn_initialized) return; // Only track for active, NN-controlled bots

    switch (event_type) {
        case EVENT_GAME_SCORE_CHANGED:
            if (event_data && event_data->team_index == pBot->pEdict->v.team) {
                // Assuming event_data->value1 is the score change amount
                pBot->current_eval_score_contribution += event_data->value1;
            }
            break;
        case EVENT_OBJECTIVE_CAPTURED: // Assuming this means fully captured by bot's team
            if (event_data && event_data->team_index == pBot->pEdict->v.team) {
                 // Check if this bot was significantly involved, e.g., near the objective
                 // For now, any team capture during its lifetime contributes.
                pBot->current_eval_objectives_captured_or_defended++;
            }
            break;
        case EVENT_OBJECTIVE_DEFENDED: // Placeholder, needs specific game logic to trigger
             if (event_data && event_data->team_index == pBot->pEdict->v.team) {
                pBot->current_eval_objectives_captured_or_defended++;
            }
            break;
        case EVENT_PLAYER_KILLED_PLAYER: // Bot killed another player
            if (event_data && event_data->player_edict_index == ENTINDEX(pBot->pEdict)) {
                pBot->current_eval_kills++;
            }
            break;
        case EVENT_PLAYER_DIED: // Bot died
            if (event_data && event_data->player_edict_index == ENTINDEX(pBot->pEdict)) {
                pBot->current_eval_deaths++;
            }
            break;
        case EVENT_PLAYER_DEALT_DAMAGE: // Bot dealt damage
             if (event_data && event_data->player_edict_index == ENTINDEX(pBot->pEdict)) {
                pBot->current_eval_damage_dealt += event_data->value1; // Assuming value1 is damage amount
            }
            break;
        // Other events can be added here if they contribute to fitness
        default:
            break;
    }
}

// Updated signature to include current_tactical_state, though it's not used in the current simple fitness calculation.
float NE_CalculateFinalFitness(bot_t* pBot, const GlobalTacticalState_t* current_tactical_state) {
    // current_tactical_state is available if more complex fitness metrics are needed later.
    // For now, the fitness calculation relies only on bot's own accumulated stats.
    if (!pBot) return 0.0f;

    float fitness = 0.0f;
    fitness += pBot->current_eval_score_contribution * FIT_SCORE_CONTRIB_WEIGHT;
    fitness += pBot->current_eval_objectives_captured_or_defended * FIT_OBJ_CAPTURE_WEIGHT;
    fitness += pBot->current_eval_kills * FIT_KILL_WEIGHT;
    fitness -= pBot->current_eval_deaths * FIT_DEATH_PENALTY_WEIGHT; // Penalty for deaths
    fitness += pBot->current_eval_damage_dealt * FIT_DAMAGE_DEALT_WEIGHT;

    float survival_time = gpGlobals->time - pBot->current_eval_survival_start_time;
    fitness += survival_time * FIT_SURVIVAL_TIME_WEIGHT;

    // Normalize? Clamp? For now, raw score.
    // Could add bonuses for specific directives if that's desired for shaping behavior,
    // but generally fitness should reflect outcomes.

    pBot->fitness_score = fitness; // Store it back to the bot
    return fitness;
}

void NE_PerformEvolutionaryCycle(std::vector<bot_t*>& current_population_bots,
                                   const GlobalTacticalState_t* current_tactical_state) {
    if (current_population_bots.size() < NE_MIN_POPULATION_FOR_EVOLUTION) {
        // ALERT(at_console, "NE: Population too small (%zu) for evolution, skipping cycle.\n", current_population_bots.size());
        // Still reset stats for the next attempt, as evaluation period ends.
        for (bot_t* pBot : current_population_bots) {
            if (pBot && pBot->is_used && pBot->nn_initialized) {
                NE_ResetBotFitnessStats(pBot);
            }
        }
        return;
    }

    // 1. Calculate final fitness for all bots in the current population
    for (bot_t* pBot : current_population_bots) {
        if (pBot && pBot->is_used && pBot->nn_initialized) {
            // Pass current_tactical_state, even if NE_CalculateFinalFitness doesn't use it yet.
            NE_CalculateFinalFitness(pBot, current_tactical_state);
        }
    }

    // 2. Sort population by fitness (descending)
    std::sort(current_population_bots.begin(), current_population_bots.end(), CompareBotFitness);

    // if (!current_population_bots.empty() && current_population_bots[0]) {
    //    ALERT(at_console, "NE: Top bot fitness for this generation: %.2f (Bot: %s)\n",
    //          current_population_bots[0]->fitness_score, current_population_bots[0]->name);
    // }

    // Read NE parameters from CVars
    float cvar_mutation_rate = bot_ne_mutation_rate.value;
    float cvar_mutation_strength = bot_ne_mutation_strength.value;
    int cvar_num_elites = (int)bot_ne_num_elites.value;
    int cvar_tournament_size = (int)bot_ne_tournament_size.value;

    // Ensure values are somewhat sane in case CVars are set badly
    if (cvar_num_elites < 0) cvar_num_elites = 0;
    if (cvar_tournament_size <= 0) cvar_tournament_size = 1;


    std::vector<TacticalNeuralNetwork_t> next_generation_nns;
    next_generation_nns.reserve(current_population_bots.size());

    // 3. Elitism: Copy NNs of the best individuals using CVar value
    int elites_copied = 0;
    for (int i = 0; i < cvar_num_elites && i < current_population_bots.size(); ++i) {
        if (current_population_bots[i] && current_population_bots[i]->is_used && current_population_bots[i]->nn_initialized) {
            next_generation_nns.push_back(current_population_bots[i]->tactical_nn);
            elites_copied++;
        }
    }
    // if (elites_copied > 0) {
    //    ALERT(at_console, "NE: Copied %d elites to next generation using cvar_num_elites %d.\n", elites_copied, cvar_num_elites);
    // }


    // 4. Generate Offspring for the rest of the population
    std::vector<const TacticalNeuralNetwork_t*> selected_parents_nns;
    // Use CVar value for tournament size
    NE_Selection_Tournament(current_population_bots, cvar_tournament_size, selected_parents_nns);

    // Ensure selected_parents_nns is not empty to prevent crashes if selection fails.
    if (selected_parents_nns.empty() && next_generation_nns.size() < current_population_bots.size()) {
        // ALERT(at_console, "NE: Warning - Parent selection returned empty list. Cannot generate offspring.\n");
        // Fill remaining spots with copies of elites if possible, or random new NNs if no elites.
        // This is a fallback. Ideally, selection always provides parents if population is valid.
        while (next_generation_nns.size() < current_population_bots.size()) {
            if (elites_copied > 0) {
                next_generation_nns.push_back(next_generation_nns[rand() % elites_copied]); // Copy a random elite
            } else if (!current_population_bots.empty() && current_population_bots[0]->nn_initialized) {
                // Fallback to just copying the first bot's NN if no elites and selection failed badly
                TacticalNeuralNetwork_t new_random_nn;
                NN_Initialize(&new_random_nn, current_population_bots[0]->tactical_nn.num_input_neurons,
                              current_population_bots[0]->tactical_nn.num_hidden_neurons,
                              current_population_bots[0]->tactical_nn.num_output_neurons, true, NULL);
                next_generation_nns.push_back(new_random_nn);
            } else {
                break; // Cannot proceed
            }
        }
    }

    int current_parent_pool_idx = 0;
    while (next_generation_nns.size() < current_population_bots.size() && !selected_parents_nns.empty()) {
        const TacticalNeuralNetwork_t* parent1 = selected_parents_nns[current_parent_pool_idx % selected_parents_nns.size()];
        current_parent_pool_idx++;
        const TacticalNeuralNetwork_t* parent2 = selected_parents_nns[current_parent_pool_idx % selected_parents_nns.size()];
        current_parent_pool_idx++; // Increment again for the next pair, or rely on modulo for wrap-around

        TacticalNeuralNetwork_t offspring1_nn, offspring2_nn;

        // Initialize offspring structures with correct dimensions (must match parents)
        NN_Initialize(&offspring1_nn, parent1->num_input_neurons, parent1->num_hidden_neurons, parent1->num_output_neurons, false, NULL);
        NN_Initialize(&offspring2_nn, parent2->num_input_neurons, parent2->num_hidden_neurons, parent2->num_output_neurons, false, NULL);

        NE_Crossover_SinglePoint(parent1, parent2, &offspring1_nn, &offspring2_nn);

        // Use CVar values for mutation rate and strength
        NE_Mutation(&offspring1_nn, cvar_mutation_rate, cvar_mutation_strength);
        NE_Mutation(&offspring2_nn, cvar_mutation_rate, cvar_mutation_strength);

        if (next_generation_nns.size() < current_population_bots.size()) {
            next_generation_nns.push_back(offspring1_nn);
        }
        if (next_generation_nns.size() < current_population_bots.size()) {
            next_generation_nns.push_back(offspring2_nn);
        }
    }

    // Ensure next_generation_nns is not larger than current_population_bots
    if (next_generation_nns.size() > current_population_bots.size()) {
        next_generation_nns.resize(current_population_bots.size());
    }
    // And if it's smaller (due to selection failure), fill with copies of elites or random
     while (next_generation_nns.size() < current_population_bots.size()) {
        if (elites_copied > 0) {
            next_generation_nns.push_back(next_generation_nns[rand() % elites_copied]); // Copy a random elite
        } else if (!current_population_bots.empty() && current_population_bots[0]->nn_initialized){
             TacticalNeuralNetwork_t new_random_nn;
             NN_Initialize(&new_random_nn, current_population_bots[0]->tactical_nn.num_input_neurons,
                           current_population_bots[0]->tactical_nn.num_hidden_neurons,
                           current_population_bots[0]->tactical_nn.num_output_neurons, true, NULL);
             next_generation_nns.push_back(new_random_nn);
        } else {
            // Cannot fill, this implies no valid NNs in population to begin with.
            // ALERT(at_console, "NE: Critical error - cannot fill next generation due to lack of source NNs.\n");
            break;
        }
    }


    // 5. Update Bot Population with New NNs
    // The current_population_bots vector was sorted by fitness.
    // We assign the new NNs from next_generation_nns back to these bot_t objects.
    // The elites are at the start of next_generation_nns.
    // The offspring fill the rest.
    // We assign them back to the bots in the order they appear in current_population_bots (which is sorted by fitness).
    // So, the fittest bots from previous gen get the elite NNs, the next fittest get good offspring etc. This is a common replacement strategy.
    for (size_t i = 0; i < current_population_bots.size() && i < next_generation_nns.size(); ++i) {
        bot_t* pBot_to_update = current_population_bots[i];
        if (pBot_to_update && pBot_to_update->is_used && pBot_to_update->nn_initialized) {
            pBot_to_update->tactical_nn = next_generation_nns[i];
        }
    }

    // 6. Reset Fitness Stats for the new generation
    for (bot_t* pBot : current_population_bots) {
        if (pBot && pBot->is_used && pBot->nn_initialized) {
            NE_ResetBotFitnessStats(pBot);
        }
    }
    // ALERT(at_console, "NE: Evolutionary cycle complete. New generation created with %zu NNs.\n", next_generation_nns.size());
}
