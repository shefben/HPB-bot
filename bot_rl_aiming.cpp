// bot_rl_aiming.cpp

#include "bot_rl_aiming.h"
#include "bot.h" // For bot_t (will need members: aiming_rl_nn, aiming_nn_initialized, current_aiming_episode_data, aiming_episode_step_count)
#include <cmath>      // For expf, logf
#include <vector>     // For std::vector
#include <numeric>    // For std::accumulate (optional)
#include <algorithm>  // For std::max_element, std::fill (optional)
#include <limits>     // For std::numeric_limits
#include <cstring>    // For memcpy

// --- Global RL Parameters (can be CVars later) ---
float AIM_RL_LEARNING_RATE = 0.001f;
float AIM_RL_DISCOUNT_FACTOR = 0.99f;
float AIM_RL_EXPLORATION_EPSILON = 0.1f; // For epsilon-greedy policy sampling
const int MAX_AIMING_EPISODE_LENGTH = 100; // Max steps in an episode before forcing update

// --- Activation Functions ---
static float rl_nn_sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

// Softmax for output layer to get probabilities
static void rl_nn_softmax(const std::vector<float>& raw_outputs, std::vector<float>& probabilities_out) {
    probabilities_out.resize(raw_outputs.size());
    if (raw_outputs.empty()) return;

    float max_val = raw_outputs[0];
    for (size_t i = 1; i < raw_outputs.size(); ++i) {
        if (raw_outputs[i] > max_val) max_val = raw_outputs[i];
    }

    float sum_exp = 0.0f;
    for (size_t i = 0; i < raw_outputs.size(); ++i) {
        probabilities_out[i] = expf(raw_outputs[i] - max_val); // Subtract max_val for numerical stability
        sum_exp += probabilities_out[i];
    }

    if (sum_exp > 0.0f && sum_exp != std::numeric_limits<float>::infinity()) { // Avoid division by zero or NaN
        for (size_t i = 0; i < raw_outputs.size(); ++i) {
            probabilities_out[i] /= sum_exp;
        }
    } else {
        // Fallback to uniform distribution if sum_exp is 0 or inf (should be rare)
        for (size_t i = 0; i < raw_outputs.size(); ++i) {
            probabilities_out[i] = 1.0f / raw_outputs.size();
        }
    }
}

// --- Neural Network Operations for Aiming ---

void RL_NN_Initialize_Aiming(RL_Aiming_NN_t* nn, int num_inputs, int num_hidden, int num_outputs,
                             bool random_init, const float* weights_data) {
    if (!nn) return;

    nn->num_input_neurons = num_inputs;
    nn->num_hidden_neurons = num_hidden;
    nn->num_output_neurons = num_outputs;

    nn->weights_input_hidden.resize((size_t)num_inputs * num_hidden);
    nn->bias_hidden.resize(num_hidden);
    nn->weights_hidden_output.resize((size_t)num_hidden * num_outputs);
    nn->bias_output.resize(num_outputs);
    nn->hidden_layer_output_activations.resize(num_hidden);

    if (random_init) {
        float range = 1.0f;
        float offset = range / 2.0f;
        auto init_vec = [&](std::vector<float>& vec) {
            for (size_t i = 0; i < vec.size(); ++i) {
                vec[i] = ((float)rand() / (float)RAND_MAX) * range - offset;
            }
        };
        init_vec(nn->weights_input_hidden);
        init_vec(nn->bias_hidden);
        init_vec(nn->weights_hidden_output);
        init_vec(nn->bias_output);
    } else if (weights_data) {
        const float* p = weights_data;
        size_t offset = 0;
        memcpy(nn->weights_input_hidden.data(), p + offset, nn->weights_input_hidden.size() * sizeof(float)); offset += nn->weights_input_hidden.size();
        memcpy(nn->bias_hidden.data(), p + offset, nn->bias_hidden.size() * sizeof(float)); offset += nn->bias_hidden.size();
        memcpy(nn->weights_hidden_output.data(), p + offset, nn->weights_hidden_output.size() * sizeof(float)); offset += nn->weights_hidden_output.size();
        memcpy(nn->bias_output.data(), p + offset, nn->bias_output.size() * sizeof(float));
    } else {
        std::fill(nn->weights_input_hidden.begin(), nn->weights_input_hidden.end(), 0.0f);
        std::fill(nn->bias_hidden.begin(), nn->bias_hidden.end(), 0.0f);
        std::fill(nn->weights_hidden_output.begin(), nn->weights_hidden_output.end(), 0.0f);
        std::fill(nn->bias_output.begin(), nn->bias_output.end(), 0.0f);
    }
}

void RL_NN_FlattenWeights_Aiming(const RL_Aiming_NN_t* nn, float* flat_array) {
    if (!nn || !flat_array) return;
    float* p = flat_array;
    size_t offset = 0;
    memcpy(p + offset, nn->weights_input_hidden.data(), nn->weights_input_hidden.size() * sizeof(float)); offset += nn->weights_input_hidden.size();
    memcpy(p + offset, nn->bias_hidden.data(), nn->bias_hidden.size() * sizeof(float)); offset += nn->bias_hidden.size();
    memcpy(p + offset, nn->weights_hidden_output.data(), nn->weights_hidden_output.size() * sizeof(float)); offset += nn->weights_hidden_output.size();
    memcpy(p + offset, nn->bias_output.data(), nn->bias_output.size() * sizeof(float));
}

void RL_NN_FeedForward_Aiming(RL_Aiming_NN_t* nn, const float* inputs, std::vector<float>& action_probabilities_out) {
    if (!nn || !inputs) return;
    if (nn->num_input_neurons <= 0 || nn->num_hidden_neurons <= 0 || nn->num_output_neurons <= 0) {
        action_probabilities_out.assign(nn->num_output_neurons, 1.0f / nn->num_output_neurons); // Uniform if NN not setup
        return;
    }

    // Input to Hidden Layer
    for (int j = 0; j < nn->num_hidden_neurons; ++j) {
        float hidden_sum = 0.0f;
        for (int i = 0; i < nn->num_input_neurons; ++i) {
            hidden_sum += inputs[i] * nn->weights_input_hidden[i * nn->num_hidden_neurons + j];
        }
        hidden_sum += nn->bias_hidden[j];
        nn->hidden_layer_output_activations[j] = rl_nn_sigmoid(hidden_sum);
    }

    // Hidden to Output Layer (raw activations/logits)
    std::vector<float> raw_outputs(nn->num_output_neurons);
    for (int k = 0; k < nn->num_output_neurons; ++k) {
        raw_outputs[k] = 0.0f;
        for (int j = 0; j < nn->num_hidden_neurons; ++j) {
            raw_outputs[k] += nn->hidden_layer_output_activations[j] * nn->weights_hidden_output[j * nn->num_output_neurons + k];
        }
        raw_outputs[k] += nn->bias_output[k];
    }

    rl_nn_softmax(raw_outputs, action_probabilities_out);
}

// --- REINFORCE Algorithm Components ---

RL_AimingAction_e RL_ChooseAction_Policy(RL_Aiming_NN_t* nn, const float* state_features_array,
                                         float exploration_epsilon, float* out_log_prob_action_taken) {
    if (!nn || !state_features_array || !out_log_prob_action_taken) {
        if(out_log_prob_action_taken) *out_log_prob_action_taken = logf(1e-9f); // Small log prob for safety
        return AIM_RL_HOLD_STEADY;
    }

    std::vector<float> action_probs;
    RL_NN_FeedForward_Aiming(nn, state_features_array, action_probs);

    RL_AimingAction_e chosen_action;
    if (((float)rand() / RAND_MAX) < exploration_epsilon) {
        chosen_action = (RL_AimingAction_e)(rand() % NUM_AIMING_RL_ACTIONS);
    } else {
        float r = (float)rand() / RAND_MAX;
        float cumulative_prob = 0.0f;
        chosen_action = AIM_RL_HOLD_STEADY; // Default
        bool action_selected = false;
        for (int i = 0; i < NUM_AIMING_RL_ACTIONS; ++i) {
            if (action_probs.size() <= (size_t)i) break; // Should not happen if NN is correct
            cumulative_prob += action_probs[i];
            if (r <= cumulative_prob) {
                chosen_action = (RL_AimingAction_e)i;
                action_selected = true;
                break;
            }
        }
        if (!action_selected && !action_probs.empty()) { // Fallback if float precision issues
             chosen_action = (RL_AimingAction_e)(action_probs.size() -1);
        }
    }

    if (action_probs.empty() || (size_t)chosen_action >= action_probs.size()) {
        *out_log_prob_action_taken = logf(1.0f / NUM_AIMING_RL_ACTIONS + 1e-9f); // Uniform if error
    } else {
        *out_log_prob_action_taken = logf(action_probs[chosen_action] + 1e-9f);
    }

    return chosen_action;
}

void RL_StoreExperience_Policy(bot_t* pBot, const float* state_features,
                               RL_AimingAction_e action, float reward, float log_prob_action) {
    if (!pBot) return;
    // Assuming pBot->current_aiming_episode_data is std::vector<RL_Aiming_Experience_t>
    // This check should ideally use pBot->aiming_episode_step_count if that's the primary counter
    if (pBot->current_aiming_episode_data.size() >= MAX_AIMING_EPISODE_LENGTH) {
        // Episode is full, an update should have been triggered.
        // Avoid adding more if it's already at max length waiting for update.
        // Or, if this is meant to be a hard cap on buffer size:
        // ALERT(at_console, "Warning: Aiming episode data full for bot %s. Not adding new experience.\n", pBot->name);
        return;
    }
    RL_Aiming_Experience_t exp;
    memcpy(exp.state_features, state_features, sizeof(float) * RL_AIMING_STATE_SIZE);
    exp.action_taken = action;
    exp.reward_received = reward;
    exp.log_prob_action = log_prob_action;
    pBot->current_aiming_episode_data.push_back(exp);
    // pBot->aiming_episode_step_count++; // This should be incremented where action is taken
}

void RL_UpdatePolicyNetwork_REINFORCE(bot_t* pBot, float learning_rate, float discount_factor) {
    if (!pBot || pBot->current_aiming_episode_data.empty() || !pBot->aiming_nn_initialized) return;

    std::vector<float> discounted_returns(pBot->current_aiming_episode_data.size());
    float current_return = 0.0f;

    for (int t = pBot->current_aiming_episode_data.size() - 1; t >= 0; --t) {
        current_return = pBot->current_aiming_episode_data[t].reward_received + discount_factor * current_return;
        discounted_returns[t] = current_return;
    }

    // (Optional) Normalize discounted returns (subtract mean, divide by std_dev)
    // float sum_returns = 0.0f;
    // for(float ret : discounted_returns) sum_returns += ret;
    // float mean_return = sum_returns / discounted_returns.size();
    // float sq_sum_diff = 0.0f;
    // for(float ret : discounted_returns) sq_sum_diff += (ret - mean_return) * (ret - mean_return);
    // float std_dev_return = sqrtf(sq_sum_diff / discounted_returns.size());
    // for (size_t t = 0; t < discounted_returns.size(); ++t) {
    //     discounted_returns[t] = (discounted_returns[t] - mean_return) / (std_dev_return + 1e-9f);
    // }

    // --- Placeholder for Gradient Update ---
    // The actual REINFORCE update rule is:
    // For each parameter theta_i in the network:
    //   theta_i = theta_i + learning_rate * sum_over_episode( G_t * grad_log_pi(a_t | s_t, theta_i) )
    // where grad_log_pi is the gradient of the log probability of the action taken w.r.t. theta_i.
    // This requires backpropagation to calculate these gradients for each weight/bias.
    //
    // For example, for a weight w_jk from hidden neuron j to output neuron k (for action a_t):
    // grad_log_pi = ( (a_t == k) - prob(action k) ) * activation_hidden_j
    // (This is for softmax output and one action selected)
    //
    // This simplified implementation does not perform the actual weight updates.
    // It only calculates G_t and clears the episode data.
    // A full implementation would require a backpropagation pass or numerical gradients here.
    // ------------------------------------

    // ALERT(at_console, "Bot %s: RL_UpdatePolicyNetwork_REINFORCE called. %zu experiences. First G_t: %.2f\n",
    //       pBot->name, pBot->current_aiming_episode_data.size(),
    //       !discounted_returns.empty() ? discounted_returns[0] : 0.0f);

    pBot->current_aiming_episode_data.clear();
    pBot->aiming_episode_step_count = 0;
}

// --- RL Aiming Helper Functions ---

// Externs often needed for these game interaction functions
extern globalvars_t  *gpGlobals; // Already declared at top, but good to remember
// extern edict_t *world; // Not explicitly used in provided snippet, but often needed for traces if not using pEdict->v.world

void PrepareRLAimingState(bot_t *pBot, edict_t *pEnemy, float* state_features_array) {
    if (!pBot || !pEnemy || !state_features_array || !pBot->pEdict || FNullEnt(pEnemy)) {
        memset(state_features_array, 0, sizeof(float) * RL_AIMING_STATE_SIZE);
        return;
    }
    edict_t *pEdict = pBot->pEdict;
    Vector vec_bot_eyes = pEdict->v.origin + pEdict->v.view_ofs;
    Vector vec_to_enemy = (pEnemy->v.origin + pEnemy->v.view_ofs) - vec_bot_eyes;

    Vector current_angles = pEdict->v.v_angle;
    Vector target_angles = UTIL_VecToAngles(vec_to_enemy);

    // Normalize target_angles.y to be within +/- 180 of current_angles.y for sensible delta
    target_angles.y = UTIL_AngleMod(target_angles.y); // Ensure it's -180 to 180 first
    // No, BotFixIdealYaw is better as it ensures the shortest path for yaw delta.
    // The UTIL_AngleMod(target - current) handles shortest path.

    state_features_array[0] = UTIL_AngleMod(target_angles.y - current_angles.y) / 180.0f;
    state_features_array[1] = UTIL_AngleMod(target_angles.x - current_angles.x) / 90.0f;
    state_features_array[2] = vec_to_enemy.Length() / 3000.0f;

    float max_server_speed = CVAR_GET_FLOAT("sv_maxspeed");
    if (max_server_speed <= 0) max_server_speed = 320.0f; // Default fallback

    state_features_array[3] = pEnemy->v.velocity.x / max_server_speed;
    state_features_array[4] = pEnemy->v.velocity.y / max_server_speed;
    // Consider if Z velocity for target is useful, or if X/Y on bot's view plane is better.
    // For now, using world X/Y.

    state_features_array[5] = pEdict->v.velocity.Length() / max_server_speed;
    state_features_array[6] = pEnemy->v.velocity.Length() / max_server_speed;

    state_features_array[7] = 0.0f; // Placeholder for pBot->current_weapon_spread

    float fire_rate = BotGetWeaponFireRate(&pBot->current_weapon);
    if (fire_rate <= 0.0f) fire_rate = 0.1f; // Avoid div by zero, ensure it's a small non-zero time
    state_features_array[8] = (pBot->pEdict->v.nextattack > gpGlobals->time) ? (pBot->pEdict->v.nextattack - gpGlobals->time) / fire_rate : 0.0f;

    TraceResult tr;
    UTIL_TraceLine(vec_bot_eyes, pEnemy->v.origin + pEnemy->v.view_ofs, ignore_monsters, pEdict, &tr);
    state_features_array[9] = (tr.flFraction >= 1.0f || tr.pHit == pEnemy) ? 1.0f : 0.0f;

    UTIL_TraceLine(vec_bot_eyes, pEnemy->v.origin + Vector(0,0,VEC_HULL_MAX_SCALED(pEnemy).z), ignore_monsters, pEdict, &tr); // Approx head
    state_features_array[10] = (tr.flFraction >= 1.0f || tr.pHit == pEnemy) ? 1.0f : 0.0f;

    for(int i=0; i<RL_AIMING_STATE_SIZE; ++i) {
       if (state_features_array[i] > 1.0f) state_features_array[i] = 1.0f;
       else if (state_features_array[i] < -1.0f) state_features_array[i] = -1.0f;
       else if (isnan(state_features_array[i])) state_features_array[i] = 0.0f; // Handle NaN
    }
}

void ExecuteRLAimingAction(bot_t *pBot, RL_AimingAction_e action) {
    if (!pBot || !pBot->pEdict) return;
    edict_t* pEdict = pBot->pEdict;

    float yaw_change = 0.0f;
    float pitch_change = 0.0f;

    switch (action) {
        case AIM_RL_YAW_LEFT_SMALL: yaw_change = -AIM_RL_ADJUST_YAW_SMALL_DEG; break;
        case AIM_RL_YAW_LEFT_MEDIUM: yaw_change = -AIM_RL_ADJUST_YAW_MEDIUM_DEG; break;
        case AIM_RL_YAW_LEFT_LARGE: yaw_change = -AIM_RL_ADJUST_YAW_LARGE_DEG; break;
        case AIM_RL_YAW_RIGHT_SMALL: yaw_change = AIM_RL_ADJUST_YAW_SMALL_DEG; break;
        case AIM_RL_YAW_RIGHT_MEDIUM: yaw_change = AIM_RL_ADJUST_YAW_MEDIUM_DEG; break;
        case AIM_RL_YAW_RIGHT_LARGE: yaw_change = AIM_RL_ADJUST_YAW_LARGE_DEG; break;

        case AIM_RL_PITCH_UP_SMALL: pitch_change = -AIM_RL_ADJUST_PITCH_SMALL_DEG; break;
        case AIM_RL_PITCH_UP_MEDIUM: pitch_change = -AIM_RL_ADJUST_PITCH_MEDIUM_DEG; break;
        case AIM_RL_PITCH_UP_LARGE: pitch_change = -AIM_RL_ADJUST_PITCH_LARGE_DEG; break;
        case AIM_RL_PITCH_DOWN_SMALL: pitch_change = AIM_RL_ADJUST_PITCH_SMALL_DEG; break;
        case AIM_RL_PITCH_DOWN_MEDIUM: pitch_change = AIM_RL_ADJUST_PITCH_MEDIUM_DEG; break;
        case AIM_RL_PITCH_DOWN_LARGE: pitch_change = AIM_RL_ADJUST_PITCH_LARGE_DEG; break;

        case AIM_RL_FIRE_PRIMARY: pEdict->v.button |= IN_ATTACK; break;
        case AIM_RL_YAW_NONE:
        case AIM_RL_PITCH_NONE:
        case AIM_RL_HOLD_STEADY:
        default: break;
    }
    // Directly modify v_angle. The BotThink smoothing might fight this.
    // For RL, we often want direct control for the chosen action's duration.
    // Alternatively, these modify ideal_angles and BotFocusController moves towards them.
    // For this iteration, let's modify v_angles directly.
    pEdict->v.v_angle.y = UTIL_AngleMod(pEdict->v.v_angle.y + yaw_change);
    pEdict->v.v_angle.x = UTIL_AngleMod(pEdict->v.v_angle.x + pitch_change);

    if (pEdict->v.v_angle.x > 89.0f) pEdict->v.v_angle.x = 89.0f;
    if (pEdict->v.v_angle.x < -89.0f) pEdict->v.v_angle.x = -89.0f;
}

float CalculateRLAimingReward(bot_t *pBot, edict_t *pEnemy, RL_AimingAction_e last_action,
                              const float* current_state_features, bool shot_fired_this_step,
                              bool* out_hit_target_this_step) {
    if (!pBot || !pBot->pEdict || !pEnemy || FNullEnt(pEnemy) || !current_state_features || !out_hit_target_this_step) return 0.0f;

    *out_hit_target_this_step = false;
    float reward = 0.0f;
    const float TIME_PENALTY = -0.01f; // Smaller time penalty
    // const float AIM_CLOSER_REWARD = 0.5f; // This might be complex to compare prev state here
    const float ON_TARGET_REWARD = 0.05f;
    const float HIT_REWARD = 10.0f; // Scaled down, can be tuned
    const float MISS_PENALTY = -0.2f; // Scaled down
    const float CLEAR_LOS_REWARD = 0.02f;

    reward += TIME_PENALTY;

    float current_delta_yaw_abs = fabsf(current_state_features[0] * 180.0f);
    float current_delta_pitch_abs = fabsf(current_state_features[1] * 90.0f);

    if (current_delta_yaw_abs < 2.0f && current_delta_pitch_abs < 2.0f) {
        reward += ON_TARGET_REWARD;
    }

    if (current_state_features[9] > 0.5f || current_state_features[10] > 0.5f) { // If LoS to center or head
        reward += CLEAR_LOS_REWARD;
    }

    if (shot_fired_this_step) {
        TraceResult tr;
        Vector vecSrc = pBot->pEdict->v.origin + pBot->pEdict->v.view_ofs;
        MAKE_VECTORS(pBot->pEdict->v.v_angle);
        Vector vecEnd = vecSrc + gpGlobals->v_forward * 3000;

        UTIL_TraceLine(vecSrc, vecEnd, ignore_monsters, pBot->pEdict, &tr);
        if (tr.pHit == pEnemy) {
            reward += HIT_REWARD;
            *out_hit_target_this_step = true;
            // If damage dealt could be known here, it would be a better reward signal.
            // This requires checking pEnemy->v.health before/after or using a damage event.
        } else {
            reward += MISS_PENALTY;
        }
    }
    return reward;
}

const char* RL_AimingActionToString(RL_AimingAction_e action) {
    switch (action) {
        case AIM_RL_YAW_NONE: return "YAW_NONE";
        case AIM_RL_YAW_LEFT_SMALL: return "YAW_L_S";
        case AIM_RL_YAW_LEFT_MEDIUM: return "YAW_L_M";
        case AIM_RL_YAW_LEFT_LARGE: return "YAW_L_L";
        case AIM_RL_YAW_RIGHT_SMALL: return "YAW_R_S";
        case AIM_RL_YAW_RIGHT_MEDIUM: return "YAW_R_M";
        case AIM_RL_YAW_RIGHT_LARGE: return "YAW_R_L";
        case AIM_RL_PITCH_NONE: return "PITCH_NONE";
        case AIM_RL_PITCH_UP_SMALL: return "PITCH_U_S";
        case AIM_RL_PITCH_UP_MEDIUM: return "PITCH_U_M";
        case AIM_RL_PITCH_UP_LARGE: return "PITCH_U_L";
        case AIM_RL_PITCH_DOWN_SMALL: return "PITCH_D_S";
        case AIM_RL_PITCH_DOWN_MEDIUM: return "PITCH_D_M";
        case AIM_RL_PITCH_DOWN_LARGE: return "PITCH_D_L";
        case AIM_RL_FIRE_PRIMARY: return "FIRE";
        case AIM_RL_HOLD_STEADY: return "HOLD";
        default: return "UNK_AIM_ACT";
    }
}
