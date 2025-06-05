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
// extern void ALERT(ALERT_TYPE atype, const char *szFmt, ...);


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
