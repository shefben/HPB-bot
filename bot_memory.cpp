#include <extdll.h>
#include <dllapi.h>
#include <h_export.h> // For gpGlobals, etc.
#include <meta_api.h> // For logging / alerts if needed

#include "bot_memory.h" // The header created in the previous step
#include "waypoint.h"   // For WAYPOINT, PATH, paths, waypoints, num_waypoints
#include "bot.h"        // For bot_t, bots array
#include "bot_objective_discovery.h" // For g_candidate_objectives, CandidateObjective_t, GetCandidateObjectiveById
#include "bot_neuro_evolution.h"   // For NN_FlattenWeights, NN_Initialize, and NN constants
#include "bot_rl_aiming.h"         // For RL Aiming NN functions and constants

#include <string.h> // For strncpy, memset
#include <stdio.h> // For FILE operations

// External declarations for globals from other files (if not already in a common bot header)
// These are often defined in the main bot C file or a specific globals C file.
// Ensure these are actually external if not available via included headers.
extern WAYPOINT waypoints[MAX_WAYPOINTS];
extern int num_waypoints;
extern PATH *paths[MAX_WAYPOINTS]; // Array of path head pointers
extern bot_t bots[32];             // Global array of bots
// extern edict_t *gpGlobalsEdict; // gpGlobals is usually directly available via h_export.h

// If pev is needed for bot_t members, it's usually part of the bot_t struct
// or accessed via ENTITY_GET_PRIVATE_DATA or similar if bots are edicts.
// For this structure, bot_t seems to be a plain struct.

// From waypoint.cpp, handles waypoint path data memory.
extern bool g_waypoint_paths;
// Waypoint system functions, assumed to be declared in waypoint.h
// extern void WaypointInit(void); // Declared in waypoint.h
// extern void WaypointAddPath(short int add_index, short int path_index, int flags); // Adjusted for flags
// extern void WaypointRouteInit(void); // Declared in waypoint.h

void SaveBotMemory(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        ALERT(at_console, "ERROR: Could not open bot memory file for writing: %s\n", filename);
        return;
    }

    bot_memory_file_hdr_t header;
    strncpy(header.file_signature, "HPB_BOT_MEM_V1", sizeof(header.file_signature) - 1);
    header.file_signature[sizeof(header.file_signature) - 1] = '\0';
    header.file_version = 1;

    if (gpGlobals && gpGlobals->mapname != 0) {
         strncpy(header.map_name, STRING(gpGlobals->mapname), sizeof(header.map_name) - 1);
    } else {
         strncpy(header.map_name, "unknown", sizeof(header.map_name) - 1);
    }
    header.map_name[sizeof(header.map_name) - 1] = '\0';

    header.num_waypoints_in_file = num_waypoints;
    header.num_bot_slots_in_file = 32; // Assuming fixed size of 32 bot slots

    // Count savable discovered objectives
    header.num_discovered_objectives = 0;
    const float MIN_CONFIDENCE_TO_SAVE_OBJECTIVE = 0.25f;
    if (!g_candidate_objectives.empty()) { // Check if vector is not empty
        for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
            if (g_candidate_objectives[i].confidence_score >= MIN_CONFIDENCE_TO_SAVE_OBJECTIVE) {
                header.num_discovered_objectives++;
            }
        }
    }


    // Write the header
    if (fwrite(&header, sizeof(bot_memory_file_hdr_t), 1, fp) != 1) {
        ALERT(at_console, "ERROR: Failed to write bot memory file header to: %s\n", filename);
        fclose(fp);
        return;
    }

    // Save Waypoint Data
    // Write the actual number of waypoints
    if (fwrite(&num_waypoints, sizeof(int), 1, fp) != 1) {
        ALERT(at_console, "ERROR: Failed to write num_waypoints to: %s\n", filename);
        fclose(fp);
        return;
    }

    // Write the waypoints array (only the used portion up to num_waypoints)
    if (num_waypoints > 0) {
        if (fwrite(waypoints, sizeof(WAYPOINT), num_waypoints, fp) != (size_t)num_waypoints) {
            ALERT(at_console, "ERROR: Failed to write waypoints array to: %s\n", filename);
            fclose(fp);
            return;
        }
    }

    // Save Discovered Objectives
    if (header.num_discovered_objectives > 0) {
        for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
            const CandidateObjective_t& cand = g_candidate_objectives[i];
            if (cand.confidence_score >= MIN_CONFIDENCE_TO_SAVE_OBJECTIVE) {
                SavedDiscoveredObjective_t saved_obj;
                memset(&saved_obj, 0, sizeof(SavedDiscoveredObjective_t));
                saved_obj.location = cand.location;
                strncpy(saved_obj.entity_classname, cand.entity_classname, sizeof(saved_obj.entity_classname) - 1);
                saved_obj.entity_classname[sizeof(saved_obj.entity_classname) -1] = '\0';
                strncpy(saved_obj.entity_targetname, cand.entity_targetname, sizeof(saved_obj.entity_targetname) - 1);
                saved_obj.entity_targetname[sizeof(saved_obj.entity_targetname) -1] = '\0';
                saved_obj.unique_id = cand.unique_id;
                saved_obj.learned_objective_type = cand.learned_objective_type;
                saved_obj.confidence_score = cand.confidence_score;
                saved_obj.positive_event_correlations = cand.positive_event_correlations;
                saved_obj.negative_event_correlations = cand.negative_event_correlations;
                // Save new fields
                saved_obj.current_owner_team = cand.current_owner_team;
                saved_obj.learned_activation_method = cand.learned_activation_method;

                if (fwrite(&saved_obj, sizeof(SavedDiscoveredObjective_t), 1, fp) != 1) {
                    ALERT(at_console, "ERROR: Failed to write SavedDiscoveredObjective_t for unique_id %d\n", cand.unique_id);
                    // Potentially break or mark file as corrupt
                    break;
                }
            }
        }
    }

    // Save Paths
    for (int i = 0; i < num_waypoints; ++i) {
        short path_count = 0;
        PATH *current_path_node = paths[i];
        while (current_path_node) {
            for (int j = 0; j < MAX_PATH_INDEX; ++j) {
                if (current_path_node->index[j] != -1) {
                    path_count++;
                }
            }
            current_path_node = current_path_node->next;
        }

        if (fwrite(&path_count, sizeof(short), 1, fp) != 1) {
             ALERT(at_console, "ERROR: Failed to write path_count for waypoint %d to: %s\n", i, filename);
             fclose(fp);
             return;
        }

        current_path_node = paths[i]; // Reset to head for writing
        while (current_path_node) {
            for (int j = 0; j < MAX_PATH_INDEX; ++j) {
                if (current_path_node->index[j] != -1) {
                    // Also need to save the flags for this path connection
                    if (fwrite(&(current_path_node->index[j]), sizeof(short), 1, fp) != 1) {
                        ALERT(at_console, "ERROR: Failed to write path index for waypoint %d to: %s\n", i, filename);
                        fclose(fp);
                        return;
                    }
                     if (fwrite(&(current_path_node->flags[j]), sizeof(int), 1, fp) != 1) { // Assuming flags is int
                        ALERT(at_console, "ERROR: Failed to write path flags for waypoint %d to: %s\n", i, filename);
                        fclose(fp);
                        return;
                    }
                }
            }
            current_path_node = current_path_node->next;
        }
    }

    // Save Persistent Bot Data
    for (int i = 0; i < 32; ++i) { // Iterate through all 32 bot slots
        persistent_bot_data_t pbd;
        bot_t* current_bot = &bots[i];

        pbd.is_used_in_save = current_bot->is_used;

        if (current_bot->is_used) {
            strncpy(pbd.name, current_bot->name, BOT_NAME_LEN);
            pbd.name[BOT_NAME_LEN] = '\0'; // Ensure null termination

            strncpy(pbd.skin, current_bot->skin, BOT_SKIN_LEN);
            pbd.skin[BOT_SKIN_LEN] = '\0'; // Ensure null termination

            pbd.bot_skill = current_bot->bot_skill;

            pbd.chat_percent = current_bot->chat_percent;
            pbd.taunt_percent = current_bot->taunt_percent;
            pbd.whine_percent = current_bot->whine_percent;
            pbd.logo_percent = current_bot->logo_percent;

            pbd.chat_tag_percent = current_bot->chat_tag_percent;
            pbd.chat_drop_percent = current_bot->chat_drop_percent;
            pbd.chat_swap_percent = current_bot->chat_swap_percent;
            pbd.chat_lower_percent = current_bot->chat_lower_percent;

            pbd.reaction_time = current_bot->reaction_time;

            pbd.top_color = current_bot->top_color;
            pbd.bottom_color = current_bot->bottom_color;
            strncpy(pbd.logo_name, current_bot->logo_name, sizeof(pbd.logo_name) - 1);
            pbd.logo_name[sizeof(pbd.logo_name) - 1] = '\0'; // Ensure null termination

            for(int w = 0; w < 6; ++w) { // Assuming 6 weapon point slots
                pbd.weapon_points[w] = current_bot->weapon_points[w];
            }
            pbd.sentrygun_waypoint = current_bot->sentrygun_waypoint;
            pbd.dispenser_waypoint = current_bot->dispenser_waypoint;

            pbd.bot_team = current_bot->bot_team;
            pbd.bot_class = current_bot->bot_class;

            // Save NN weights
            if (current_bot->nn_initialized) {
                pbd.has_saved_nn_weights = true;
                NN_FlattenWeights(&current_bot->tactical_nn, pbd.tactical_nn_weights);
            } else {
                pbd.has_saved_nn_weights = false;
                memset(pbd.tactical_nn_weights, 0, sizeof(pbd.tactical_nn_weights));
            }

            // Save RL Aiming NN weights
            if (current_bot->aiming_nn_initialized) {
                pbd.has_saved_aiming_nn = true;
                RL_NN_FlattenWeights_Aiming(&current_bot->aiming_rl_nn, pbd.aiming_rl_nn_weights);
            } else {
                pbd.has_saved_aiming_nn = false;
                memset(pbd.aiming_rl_nn_weights, 0, sizeof(pbd.aiming_rl_nn_weights));
            }

        } else {
            memset(&pbd, 0, sizeof(persistent_bot_data_t)); // Zero out the whole struct
            pbd.is_used_in_save = false;
            // All bools like has_saved_nn_weights and has_saved_aiming_nn will be false due to memset.
        }

        if (fwrite(&pbd, sizeof(persistent_bot_data_t), 1, fp) != 1) {
            ALERT(at_console, "ERROR: Failed to write persistent bot data for bot %d to: %s\n", i, filename);
            fclose(fp);
            return;
        }
    }

    fclose(fp);
    ALERT(at_console, "Bot memory saved to %s\n", filename);
}

void LoadBotMemory(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        ALERT(at_console, "INFO: Bot memory file not found or could not be opened: %s. Starting fresh.\n", filename);
        return;
    }

    bot_memory_file_hdr_t header;
    if (fread(&header, sizeof(bot_memory_file_hdr_t), 1, fp) != 1) {
        ALERT(at_console, "ERROR: Could not read bot memory file header from: %s\n", filename);
        fclose(fp);
        return;
    }

    // Validate signature and version
    if (strncmp(header.file_signature, "HPB_BOT_MEM_V1", sizeof(header.file_signature)) != 0) {
        ALERT(at_console, "ERROR: Invalid bot memory file signature in: %s\n", filename);
        fclose(fp);
        return;
    }
    if (header.file_version != 1) {
        ALERT(at_console, "ERROR: Unsupported bot memory file version %d in: %s\n", header.file_version, filename);
        fclose(fp);
        return;
    }

    bool load_waypoints_for_this_map = true;
    if (gpGlobals && gpGlobals->mapname != 0) {
        if (strncmp(header.map_name, STRING(gpGlobals->mapname), sizeof(header.map_name)) != 0) {
            ALERT(at_console, "WARNING: Bot memory map '%s' does not match current map '%s'. Waypoints and associated bot data will not be loaded from this file.\n", header.map_name, STRING(gpGlobals->mapname));
            load_waypoints_for_this_map = false;
        }
    } else if (strncmp(header.map_name, "unknown", sizeof(header.map_name)) != 0) {
        ALERT(at_console, "WARNING: Current map is unknown, bot memory map is '%s'. Waypoints and associated bot data will not be loaded from this file.\n", header.map_name);
        load_waypoints_for_this_map = false;
    }

    if (!load_waypoints_for_this_map) {
        // As per simplified plan, if map doesn't match, skip loading from this file entirely.
        fclose(fp);
        return;
    }

    // Waypoints ARE being loaded for this map
    WaypointInit(); // Clear existing waypoint data

    int loaded_num_waypoints_from_file;
    if (fread(&loaded_num_waypoints_from_file, sizeof(int), 1, fp) != 1) {
        ALERT(at_console, "ERROR: Failed to read num_waypoints from: %s\n", filename);
        fclose(fp);
        return;
    }

    num_waypoints = loaded_num_waypoints_from_file; // Set global

    if (num_waypoints > MAX_WAYPOINTS) {
        ALERT(at_console, "ERROR: num_waypoints (%d) from file exceeds MAX_WAYPOINTS (%d) in: %s\n", num_waypoints, MAX_WAYPOINTS, filename);
        num_waypoints = 0; // Reset to prevent overflow
        fclose(fp);
        return;
    }

    if (num_waypoints > 0) {
        if (fread(waypoints, sizeof(WAYPOINT), num_waypoints, fp) != (size_t)num_waypoints) {
            ALERT(at_console, "ERROR: Failed to read waypoints array from: %s\n", filename);
            num_waypoints = 0; // Reset
            fclose(fp);
            return;
        }
    }

    // Load Paths
    for (int i = 0; i < num_waypoints; ++i) {
        short path_count = 0;
        if (fread(&path_count, sizeof(short), 1, fp) != 1) {
            ALERT(at_console, "ERROR: Failed to read path_count for waypoint %d from: %s\n", i, filename);
            num_waypoints = 0;
            fclose(fp);
            return;
        }
        for (short j = 0; j < path_count; ++j) {
            short path_dest_index = -1;
            int path_flags = 0; // Assuming flags are int, matching save logic

            if (fread(&path_dest_index, sizeof(short), 1, fp) != 1) {
                ALERT(at_console, "ERROR: Failed to read path destination index for waypoint %d from: %s\n", i, filename);
                num_waypoints = 0;
                fclose(fp);
                return;
            }
            if (fread(&path_flags, sizeof(int), 1, fp) != 1) { // Load path flags
                ALERT(at_console, "ERROR: Failed to read path flags for waypoint %d from: %s\n", i, filename);
                num_waypoints = 0;
                fclose(fp);
                return;
            }

            if (path_dest_index >= 0 && path_dest_index < num_waypoints) {
                 WaypointAddPath(i, path_dest_index, path_flags); // Reconstruct path with flags
            } else if (path_dest_index != -1) {
                 ALERT(at_console, "WARNING: Invalid path destination index %d (flags %d) for waypoint %d in %s\n", path_dest_index, path_flags, i, filename);
            }
        }
    }

    g_waypoint_paths = TRUE;
    WaypointRouteInit();
    ALERT(at_console, "Waypoints loaded from %s for map %s.\n", filename, header.map_name);

    // Load Persistent Bot Data
    if (header.num_bot_slots_in_file > 32) {
         ALERT(at_console, "ERROR: File contains data for %d bot slots, but system max is 32, in %s\n", header.num_bot_slots_in_file, filename);
         fclose(fp);
         return;
    }

    for (int i = 0; i < header.num_bot_slots_in_file; ++i) {
        // Ensure we don't read past 32 slots even if header.num_bot_slots_in_file is larger but <=32
        if (i >= 32) break;

        persistent_bot_data_t pbd;
        if (fread(&pbd, sizeof(persistent_bot_data_t), 1, fp) != 1) {
            ALERT(at_console, "ERROR: Failed to read persistent bot data for bot slot %d from: %s\n", i, filename);
            fclose(fp);
            return;
        }

        if (pbd.is_used_in_save) {
            bot_t* target_bot = &bots[i];

            strncpy(target_bot->name, pbd.name, BOT_NAME_LEN);
            target_bot->name[BOT_NAME_LEN] = '\0';
            strncpy(target_bot->skin, pbd.skin, BOT_SKIN_LEN);
            target_bot->skin[BOT_SKIN_LEN] = '\0';

            target_bot->bot_skill = pbd.bot_skill;

            target_bot->chat_percent = pbd.chat_percent;
            target_bot->taunt_percent = pbd.taunt_percent;
            target_bot->whine_percent = pbd.whine_percent;
            target_bot->logo_percent = pbd.logo_percent;

            target_bot->chat_tag_percent = pbd.chat_tag_percent;
            target_bot->chat_drop_percent = pbd.chat_drop_percent;
            target_bot->chat_swap_percent = pbd.chat_swap_percent;
            target_bot->chat_lower_percent = pbd.chat_lower_percent;

            target_bot->reaction_time = pbd.reaction_time;

            target_bot->top_color = pbd.top_color;
            target_bot->bottom_color = pbd.bottom_color;
            strncpy(target_bot->logo_name, pbd.logo_name, sizeof(target_bot->logo_name) - 1);
            target_bot->logo_name[sizeof(target_bot->logo_name) - 1] = '\0';

            for(int w = 0; w < 6; ++w) { // Assuming 6 weapon point slots
                target_bot->weapon_points[w] = pbd.weapon_points[w];
            }
            target_bot->sentrygun_waypoint = pbd.sentrygun_waypoint;
            target_bot->dispenser_waypoint = pbd.dispenser_waypoint;

            target_bot->bot_team = pbd.bot_team;
            target_bot->bot_class = pbd.bot_class;

            // target_bot->is_used will be set by BotCreate or similar logic later.
            // The loaded data here is for personality/skill, not current 'in-game' state.
            target_bot->loaded_from_persistence = true; // Mark as loaded

            // Load NN weights if available
            if (pbd.has_saved_nn_weights) {
                NN_Initialize(&target_bot->tactical_nn, NN_INPUT_SIZE, NN_HIDDEN_SIZE, NUM_TACTICAL_DIRECTIVES,
                              false, /*initialize_with_random_weights=false*/
                              pbd.tactical_nn_weights /*initial_weights_data*/);
                target_bot->nn_initialized = true;
            } else {
                // If no saved weights, BotCreate will initialize it randomly
                target_bot->nn_initialized = false;
            }

            // Load RL Aiming NN weights if available
            if (pbd.has_saved_aiming_nn) {
                RL_NN_Initialize_Aiming(&target_bot->aiming_rl_nn,
                                      RL_AIMING_STATE_SIZE,
                                      RL_AIMING_HIDDEN_LAYER_SIZE,
                                      RL_AIMING_OUTPUT_SIZE,
                                      false, // initialize_with_random_weights = false
                                      pbd.aiming_rl_nn_weights); // initial_weights_data
                target_bot->aiming_nn_initialized = true;
            } else {
                target_bot->aiming_nn_initialized = false;
            }

        } else {
            // If slot wasn't used in save, ensure flags are false
            if (i < 32 && !bots[i].is_used) { // Check against current bot usage, though this loop is for all saved slots
                 bots[i].loaded_from_persistence = false;
                 bots[i].nn_initialized = false;
                 bots[i].aiming_nn_initialized = false;
            }
        }
    }

    // Load Discovered Objectives
    if (header.file_version >= 1 && header.num_discovered_objectives > 0 && header.num_discovered_objectives < (MAX_OBJECTIVES_IN_DISCOVERY_LIST * 2)) { // Sanity check
        int loaded_count = 0;
        for (int i = 0; i < header.num_discovered_objectives; ++i) {
            SavedDiscoveredObjective_t saved_obj;
            if (fread(&saved_obj, sizeof(SavedDiscoveredObjective_t), 1, fp) != 1) {
                ALERT(at_console, "ERROR: Failed to read SavedDiscoveredObjective_t record %d\n", i);
                break;
            }

            CandidateObjective_t* existing_cand = GetCandidateObjectiveById(saved_obj.unique_id);
            if (existing_cand) {
                existing_cand->learned_objective_type = saved_obj.learned_objective_type;
                existing_cand->confidence_score = saved_obj.confidence_score;
                existing_cand->positive_event_correlations = saved_obj.positive_event_correlations;
                existing_cand->negative_event_correlations = saved_obj.negative_event_correlations;
                // Load new fields
                existing_cand->current_owner_team = saved_obj.current_owner_team;
                existing_cand->learned_activation_method = saved_obj.learned_activation_method;
            } else {
                if (g_candidate_objectives.size() < MAX_OBJECTIVES_IN_DISCOVERY_LIST) {
                    CandidateObjective_t new_cand;
                    memset(&new_cand, 0, sizeof(CandidateObjective_t));
                    new_cand.location = saved_obj.location;
                    strncpy(new_cand.entity_classname, saved_obj.entity_classname, sizeof(new_cand.entity_classname) - 1);
                    new_cand.entity_classname[sizeof(new_cand.entity_classname)-1] = '\0';
                    strncpy(new_cand.entity_targetname, saved_obj.entity_targetname, sizeof(new_cand.entity_targetname) - 1);
                    new_cand.entity_targetname[sizeof(new_cand.entity_targetname)-1] = '\0';
                    new_cand.unique_id = saved_obj.unique_id;
                    new_cand.learned_objective_type = saved_obj.learned_objective_type;
                    new_cand.confidence_score = saved_obj.confidence_score;
                    new_cand.positive_event_correlations = saved_obj.positive_event_correlations;
                    new_cand.negative_event_correlations = saved_obj.negative_event_correlations;
                    new_cand.last_interacting_team = -1;
                    new_cand.last_interaction_time = 0.0f;
                    // Load new fields
                    new_cand.current_owner_team = saved_obj.current_owner_team;
                    new_cand.learned_activation_method = saved_obj.learned_activation_method;
                    new_cand.last_positive_correlation_update_time = 0.0f; // Reset or load if saved

                    g_candidate_objectives.push_back(new_cand);

                    if (new_cand.unique_id >= (MAX_WAYPOINTS + 1) && new_cand.unique_id >= g_dynamic_candidate_id_counter) {
                        g_dynamic_candidate_id_counter = new_cand.unique_id + 1;
                    }
                    loaded_count++;
                }
            }
        }
        if (loaded_count > 0) { // Changed from header.num_discovered_objectives to actual loaded_count
             ALERT(at_console, "Loaded/Updated %d discovered objectives from bot memory.\n", loaded_count);
        }
    }


    fclose(fp);
    ALERT(at_console, "Bot memory loaded from %s\n", filename);
}
