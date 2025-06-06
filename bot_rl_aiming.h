#ifndef BOT_RL_AIMING_H
#define BOT_RL_AIMING_H

#include "extdll.h" // For Vector (if not already transitively included by other bot headers)
#include <vector>   // For std::vector

// --- State Representation for Aiming RL ---
typedef struct {
    // All values should be normalized, typically to [-1, 1] or [0, 1]
    float delta_yaw_to_target;          // Normalized: (current_yaw - target_yaw) / 180.0f
    float delta_pitch_to_target;        // Normalized: (current_pitch - target_pitch) / 90.0f (approx)
    float target_distance_normalized;   // Normalized: distance / typical_max_engagement_range (e.g., 2048.0f)

    // Target's velocity projected onto bot's view plane (normalized)
    // These are harder to get; might require tracking target over a few frames.
    // For initial version, can be placeholder (0.0f) or based on simple difference.
    float target_apparent_velocity_x_normalized; // e.g., change_in_screen_x / (max_expected_screen_vel * dt)
    float target_apparent_velocity_y_normalized; // e.g., change_in_screen_y / (max_expected_screen_vel * dt)

    float bot_is_moving_factor;         // 0 if still, 1 if moving fast (normalized bot speed)
    float target_is_moving_factor;      // 0 if target still, 1 if target moving fast (normalized target speed)

    float current_weapon_spread_factor; // Normalized: current_spread / max_possible_spread_for_weapon
    float current_weapon_next_fire_time_ratio; // Normalized: time_until_can_fire_again / typical_fire_rate_interval

    float line_of_sight_clear_to_target_center; // 1.0 if clear, 0.0 if blocked (to center mass)
    float line_of_sight_clear_to_target_head;   // 1.0 if clear, 0.0 if blocked (to head)

    // Add more features as needed, e.g., bot's own angular velocity, time since last shot
    // Ensure this list matches RL_AIMING_STATE_SIZE

} RL_AimingState_t;

// --- Action Space for Aiming RL ---
typedef enum {
    // Yaw Adjustments (relative to current view)
    AIM_RL_YAW_NONE = 0,
    AIM_RL_YAW_LEFT_SMALL,
    AIM_RL_YAW_LEFT_MEDIUM,
    AIM_RL_YAW_LEFT_LARGE,
    AIM_RL_YAW_RIGHT_SMALL,
    AIM_RL_YAW_RIGHT_MEDIUM,
    AIM_RL_YAW_RIGHT_LARGE,

    // Pitch Adjustments (relative to current view)
    AIM_RL_PITCH_NONE, // Separate from YAW_NONE if combined actions are not used
    AIM_RL_PITCH_UP_SMALL,
    AIM_RL_PITCH_UP_MEDIUM,
    AIM_RL_PITCH_UP_LARGE,
    AIM_RL_PITCH_DOWN_SMALL,
    AIM_RL_PITCH_DOWN_MEDIUM,
    AIM_RL_PITCH_DOWN_LARGE,

    // Firing Action
    AIM_RL_FIRE_PRIMARY, // Attempt to fire primary attack

    AIM_RL_HOLD_STEADY,  // Deliberately do nothing (no aim change, no fire)
                        // This could be same as YAW_NONE + PITCH_NONE + NO_FIRE
                        // For a discrete action space, it's often better to have fewer, more distinct actions.
                        // Consider if YAW_NONE & PITCH_NONE implicitly mean hold steady if not firing.

    // Alternative: Combined actions like AIM_YAW_LEFT_SMALL_AND_FIRE. This explodes action space.
    // Simpler: NN outputs aim adjustment, a separate mechanism/heuristic decides *when* to fire.
    // Or, NN has a dedicated "fire" output neuron, and aim adjustment neurons.
    // For now, keeping FIRE separate.

    NUM_AIMING_RL_ACTIONS // Automatically gives the count
} RL_AimingAction_e;


// --- Neural Network Structure for Aiming RL ---
// This is generic; can be used for Policy Network or Q-Value Network
typedef struct {
    int num_input_neurons;
    int num_hidden_neurons;
    int num_output_neurons; // For policy: NUM_AIMING_RL_ACTIONS. For Q-value: NUM_AIMING_RL_ACTIONS.

    std::vector<float> weights_input_hidden;
    std::vector<float> bias_hidden;
    std::vector<float> weights_hidden_output;
    std::vector<float> bias_output;
    std::vector<float> hidden_layer_output_activations;
} RL_Aiming_NN_t;


// --- Constants for NN Dimensions & RL Parameters ---
#ifndef BOT_RL_AIMING_CONSTANTS_H
#define BOT_RL_AIMING_CONSTANTS_H

// Update this count based on the final number of fields in RL_AimingState_t
const int RL_AIMING_STATE_SIZE = 11; // Based on current RL_AimingState_t example
const int RL_AIMING_HIDDEN_LAYER_SIZE = 32; // Example, can be tuned
const int RL_AIMING_OUTPUT_SIZE = NUM_AIMING_RL_ACTIONS; // Derived from enum

const int MAX_RL_AIMING_NN_WEIGHT_SIZE =
                           (RL_AIMING_STATE_SIZE * RL_AIMING_HIDDEN_LAYER_SIZE) +  // Input-Hidden Weights
                           RL_AIMING_HIDDEN_LAYER_SIZE +                           // Hidden Biases
                           (RL_AIMING_HIDDEN_LAYER_SIZE * RL_AIMING_OUTPUT_SIZE) + // Hidden-Output Weights
                           RL_AIMING_OUTPUT_SIZE;                                  // Output Biases

// Example action magnitudes (degrees for angles) - these will be used by the action execution logic
const float AIM_RL_ADJUST_YAW_SMALL_DEG = 0.5f;
const float AIM_RL_ADJUST_YAW_MEDIUM_DEG = 2.0f;
const float AIM_RL_ADJUST_YAW_LARGE_DEG = 5.0f;
const float AIM_RL_ADJUST_PITCH_SMALL_DEG = 0.3f;
const float AIM_RL_ADJUST_PITCH_MEDIUM_DEG = 1.5f;
const float AIM_RL_ADJUST_PITCH_LARGE_DEG = 3.0f;

const float AIM_RL_MAX_GRADIENT_UPDATE_PER_WEIGHT = 0.1f; // Max absolute change for a single weight per update step

#endif // BOT_RL_AIMING_CONSTANTS_H

// --- Experience Struct for REINFORCE ---
typedef struct {
    float state_features[RL_AIMING_STATE_SIZE]; // Store the actual features
    RL_AimingAction_e action_taken;
    float reward_received;
    float log_prob_action; // Log probability of the action taken
} RL_Aiming_Experience_t;

// Forward declare bot_t for function prototypes
struct bot_t;

// --- Function Prototypes for bot_rl_aiming.cpp ---
void RL_NN_Initialize_Aiming(RL_Aiming_NN_t* nn, int num_inputs, int num_hidden, int num_outputs, bool random_init, const float* weights_data);
void RL_NN_FlattenWeights_Aiming(const RL_Aiming_NN_t* nn, float* flat_array);
void RL_NN_FeedForward_Aiming(RL_Aiming_NN_t* nn, const float* inputs, std::vector<float>& action_probabilities_out);

RL_AimingAction_e RL_ChooseAction_Policy(RL_Aiming_NN_t* nn, const float* state_features_array,
                                         float exploration_epsilon, float* out_log_prob_action_taken);
void RL_StoreExperience_Policy(bot_t* pBot, const float* state_features,
                               RL_AimingAction_e action, float reward, float log_prob_action);
void RL_UpdatePolicyNetwork_REINFORCE(bot_t* pBot, float learning_rate, float discount_factor);

// Helper function prototypes for RL Aiming logic
void PrepareRLAimingState(bot_t *pBot, edict_t *pEnemy, float* state_features_array);
void ExecuteRLAimingAction(bot_t *pBot, RL_AimingAction_e action);
float CalculateRLAimingReward(
    bot_t *pBot, edict_t *pEnemy,
    RL_AimingAction_e action_taken_at_s_t,      // a_t
    const float* state_features_s_t_plus_1,    // s_t+1 (current state after action)
    const float* state_features_s_t,           // s_t (state before action)
    bool shot_fired_by_rl_at_s_t,              // Was IN_ATTACK set by RL for a_t?
    bool* out_hit_target_at_s_t                 // Did that shot hit?
);

const char* RL_AimingActionToString(RL_AimingAction_e action);

#endif // BOT_RL_AIMING_H
