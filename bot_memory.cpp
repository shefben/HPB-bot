#include <extdll.h> // For general SDK types, engine functions
#include <dllapi.h> // For DLL function exports/imports if any are used directly
#include <h_export.h> // For gpGlobals
#include <meta_api.h> // For SERVER_PRINT or ALERT
#include <stdio.h>    // For FILE operations
#include <string.h>   // For strncpy, memset

#include "bot_memory.h" // Defines V3 structures now
#include "bot.h"
#include "waypoint.h"
#include "bot_neuro_evolution.h" // For Tactical NN funcs and MAX_NN_WEIGHT_SIZE
#include "bot_rl_aiming.h"       // For RL Aiming NN funcs and constants
#include "bot_objective_discovery.h" // For g_candidate_objectives, CandidateObjective_t, etc.

// Extern Global Vars
extern WAYPOINT waypoints[MAX_WAYPOINTS];
extern int num_waypoints;
extern PATH *paths[MAX_WAYPOINTS];
extern bot_t bots[32];
extern std::vector<CandidateObjective_t> g_candidate_objectives; // From bot_objective_discovery.cpp
extern int g_dynamic_candidate_id_counter; // From bot_objective_discovery.cpp
extern bool g_waypoint_paths; // From waypoint.cpp, indicates if paths are built


// Definition for MIN_CONFIDENCE_TO_SAVE_OBJECTIVE if not globally available
// This should ideally be a shared constant, e.g., in bot_objective_discovery.h or bot_memory.h
const float MIN_CONFIDENCE_TO_SAVE_OBJECTIVE = 0.25f;

void SaveBotMemory(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        SERVER_PRINT("ERROR: Could not open bot memory file for writing: %s\n", filename);
        return;
    }

    bot_memory_file_hdr_t header;
    memset(&header, 0, sizeof(bot_memory_file_hdr_t)); // Zero out header

    strncpy(header.file_signature, "HPB_BOT_MEM_V3", sizeof(header.file_signature) - 1);
    header.file_signature[sizeof(header.file_signature) - 1] = '\0';
    header.file_version = 3;

    if (gpGlobals && gpGlobals->mapname != 0 && STRING(gpGlobals->mapname)[0] != '\0') {
        strncpy(header.map_name, STRING(gpGlobals->mapname), sizeof(header.map_name) - 1);
    } else {
        strncpy(header.map_name, "unknown", sizeof(header.map_name) - 1);
    }
    header.map_name[sizeof(header.map_name) - 1] = '\0';

    header.num_waypoints_in_file = num_waypoints;
    header.num_bot_slots_in_file = 32;

    // Count savable discovered objectives
    header.num_discovered_objectives = 0;
    for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
        if (g_candidate_objectives[i].confidence_score >= MIN_CONFIDENCE_TO_SAVE_OBJECTIVE) {
            header.num_discovered_objectives++;
        }
    }
    // Limit to a max savable number if necessary, e.g. MAX_SAVED_OBJECTIVES
    // if (header.num_discovered_objectives > MAX_SAVED_OBJECTIVES_CONST) header.num_discovered_objectives = MAX_SAVED_OBJECTIVES_CONST;

    // Write the header
    if (fwrite(&header, sizeof(bot_memory_file_hdr_t), 1, fp) != 1) {
        SERVER_PRINT("ERROR: Failed to write bot memory file header (V3) to: %s\n", filename);
        fclose(fp);
        return;
    }

    // Save Waypoint Data
    if (fwrite(&num_waypoints, sizeof(int), 1, fp) != 1) {
        SERVER_PRINT("ERROR: Failed to write num_waypoints to %s\n", filename);
        fclose(fp); return;
    }
    if (num_waypoints > 0) {
        if (fwrite(waypoints, sizeof(WAYPOINT), num_waypoints, fp) != (size_t)num_waypoints) {
            SERVER_PRINT("ERROR: Failed to write waypoints array to %s\n", filename);
            fclose(fp); return;
        }
        for (int i = 0; i < num_waypoints; ++i) {
            short path_count = 0;
            PATH *p = paths[i];
            while (p) {
                for (int j=0; j<MAX_PATH_INDEX; ++j)
                    if(p->index[j]!=-1) path_count++;
                p = p->next;
            }
            if (fwrite(&path_count, sizeof(short), 1, fp) != 1) {
                SERVER_PRINT("ERROR: Failed to write path_count for waypoint %d to %s\n", i, filename);
                fclose(fp); return;
            }
            p = paths[i];
            while (p) {
                for (int j=0; j<MAX_PATH_INDEX; ++j)
                    if(p->index[j]!=-1)
                        if(fwrite(&p->index[j],sizeof(short),1,fp)!=1) {
                            SERVER_PRINT("ERROR: Failed to write path index for waypoint %d to %s\n", i, filename);
                            fclose(fp); return;
                        }
                p=p->next;
            }
        }
    }

    // Save Persistent Bot Data
    for (int i = 0; i < 32; ++i) {
        persistent_bot_data_t pbd;
        memset(&pbd, 0, sizeof(persistent_bot_data_t)); // Initialize to zero
        bot_t* current_bot = &bots[i];

        pbd.is_used_in_save = current_bot->is_used;
        if (current_bot->is_used) {
            strncpy(pbd.name, current_bot->name, BOT_NAME_LEN); pbd.name[BOT_NAME_LEN] = '\0';
            strncpy(pbd.skin, current_bot->skin, BOT_SKIN_LEN); pbd.skin[BOT_SKIN_LEN] = '\0';
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
            strncpy(pbd.logo_name, current_bot->logo_name, sizeof(pbd.logo_name) - 1); pbd.logo_name[sizeof(pbd.logo_name) - 1] = '\0';
            for(int w = 0; w < 6; ++w) pbd.weapon_points[w] = current_bot->weapon_points[w];
            pbd.sentrygun_waypoint = current_bot->sentrygun_waypoint;
            pbd.dispenser_waypoint = current_bot->dispenser_waypoint;
            pbd.bot_team = current_bot->bot_team;
            pbd.bot_class = current_bot->bot_class;

            // Tactical NN Weights
            if (current_bot->nn_initialized) {
                pbd.has_saved_nn_weights = true;
                NN_FlattenWeights(&current_bot->tactical_nn, pbd.tactical_nn_weights);
            } else {
                pbd.has_saved_nn_weights = false;
                 memset(pbd.tactical_nn_weights, 0, sizeof(pbd.tactical_nn_weights)); // Zero out if not saved
            }

            // RL Aiming NN Weights
            if (current_bot->aiming_nn_initialized) {
                pbd.has_saved_aiming_nn = true;
                RL_NN_FlattenWeights_Aiming(&current_bot->aiming_rl_nn, pbd.aiming_rl_nn_weights);
            } else {
                pbd.has_saved_aiming_nn = false;
                memset(pbd.aiming_rl_nn_weights, 0, sizeof(pbd.aiming_rl_nn_weights)); // Zero out if not saved
            }
        }
        if (fwrite(&pbd, sizeof(persistent_bot_data_t), 1, fp) != 1) {
            SERVER_PRINT("ERROR: Failed to write persistent_bot_data for bot %d to %s\n", i, filename);
            fclose(fp); return;
        }
    }

    // Save Discovered Objectives
    if (header.num_discovered_objectives > 0) {
        int saved_count = 0;
        for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
            const CandidateObjective_t& cand = g_candidate_objectives[i];
            if (cand.confidence_score >= MIN_CONFIDENCE_TO_SAVE_OBJECTIVE) {
                if (saved_count >= header.num_discovered_objectives) break;

                SavedDiscoveredObjective_t saved_obj;
                memset(&saved_obj, 0, sizeof(SavedDiscoveredObjective_t));
                saved_obj.location = cand.location;
                strncpy(saved_obj.entity_classname, cand.entity_classname, sizeof(saved_obj.entity_classname) - 1);
                saved_obj.entity_classname[sizeof(saved_obj.entity_classname)-1] = '\0';
                strncpy(saved_obj.entity_targetname, cand.entity_targetname, sizeof(saved_obj.entity_targetname) - 1);
                saved_obj.entity_targetname[sizeof(saved_obj.entity_targetname)-1] = '\0';
                saved_obj.unique_id = cand.unique_id;
                saved_obj.learned_objective_type = cand.learned_objective_type;
                saved_obj.confidence_score = cand.confidence_score;
                saved_obj.positive_event_correlations = cand.positive_event_correlations;
                saved_obj.negative_event_correlations = cand.negative_event_correlations;
                saved_obj.current_owner_team = cand.current_owner_team;
                saved_obj.learned_activation_method = cand.learned_activation_method;

                if (fwrite(&saved_obj, sizeof(SavedDiscoveredObjective_t), 1, fp) != 1) {
                    SERVER_PRINT("ERROR: Failed to write SavedDiscoveredObjective_t for ID %d to %s\n", cand.unique_id, filename);
                    fclose(fp); return;
                }
                saved_count++;
            }
        }
    }

    fclose(fp);
    SERVER_PRINT("Bot memory (V3) saved to %s\n", filename);
}

bool LoadBotMemory(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        SERVER_PRINT("INFO: Bot memory file not found: %s. Starting fresh.\n", filename);
        return false;
    }

    bot_memory_file_hdr_t header;
    if (fread(&header, sizeof(bot_memory_file_hdr_t), 1, fp) != 1) {
        SERVER_PRINT("ERROR: Could not read bot memory file header from %s\n", filename);
        fclose(fp); return false;
    }

    if (strncmp(header.file_signature, "HPB_BOT_MEM_V3", sizeof(header.file_signature)) != 0 || header.file_version != 3) {
        SERVER_PRINT("Bot Memory: File version mismatch or old format. Expected V3. Found sig: '%.15s', ver: %d. Aborting load.\n", header.file_signature, header.file_version);
        fclose(fp);
        return false;
    }

    bool load_waypoints_for_this_map = true;
    if (gpGlobals && gpGlobals->mapname != 0 && STRING(gpGlobals->mapname)[0] != '\0') {
         if (strncmp(header.map_name, STRING(gpGlobals->mapname), sizeof(header.map_name)) != 0) {
             SERVER_PRINT("Bot Memory: Map name in file ('%s') does not match current map ('%s'). Waypoints & Discovered Objectives specific to map will not be loaded.\n", header.map_name, STRING(gpGlobals->mapname));
             load_waypoints_for_this_map = false;
         }
    } else if (strncmp(header.map_name, "unknown", sizeof(header.map_name)) != 0 && strlen(header.map_name) > 0) {
        // Current map is unknown/empty, but file has a specific map. Don't load map-specific data.
        SERVER_PRINT("Bot Memory: Current map name is empty/unknown. File map is '%s'. Waypoints & Discovered Objectives specific to map will not be loaded.\n", header.map_name);
        load_waypoints_for_this_map = false;
    }


    int loaded_num_waypoints_from_file = 0;
    if (fread(&loaded_num_waypoints_from_file, sizeof(int), 1, fp) != 1) {
        SERVER_PRINT("ERROR: Failed to read num_waypoints from %s\n", filename);
        fclose(fp); return false;
    }

    if (load_waypoints_for_this_map) {
        WaypointInit();
        num_waypoints = loaded_num_waypoints_from_file;
        if (num_waypoints < 0 || num_waypoints > MAX_WAYPOINTS) {
            SERVER_PRINT("ERROR: Invalid num_waypoints (%d) from file %s. Must be 0-%d.\n", num_waypoints, filename, MAX_WAYPOINTS);
            num_waypoints = 0; // Reset
            fclose(fp); return false;
        }
        if (num_waypoints > 0) {
            if (fread(waypoints, sizeof(WAYPOINT), num_waypoints, fp) != (size_t)num_waypoints) {
                SERVER_PRINT("ERROR: Failed to read waypoints array from %s\n", filename);
                num_waypoints = 0; fclose(fp); return false;
            }
            for (int i = 0; i < num_waypoints; ++i) {
                short path_count = 0;
                if (fread(&path_count, sizeof(short), 1, fp) != 1) {
                    SERVER_PRINT("ERROR: Failed to read path_count for waypoint %d from %s\n", i, filename);
                    num_waypoints = 0; fclose(fp); return false;
                }
                if (path_count < 0 || path_count > num_waypoints * MAX_PATH_INDEX) { // Sanity check path_count
                     SERVER_PRINT("ERROR: Invalid path_count %d for waypoint %d in %s\n", path_count, i, filename);
                     num_waypoints = 0; fclose(fp); return false;
                }
                for (short j = 0; j < path_count; ++j) {
                    short path_idx = -1;
                    if (fread(&path_idx,sizeof(short),1,fp)!=1) {
                        SERVER_PRINT("ERROR: Failed to read path index for waypoint %d, path %d from %s\n", i, j, filename);
                        num_waypoints = 0; fclose(fp); return false;
                    }
                    if(path_idx >=0 && path_idx < num_waypoints) WaypointAddPath(i, path_idx, 0); // Assuming 0 for flags if not saved
                    else if (path_idx != -1) { // Allow -1 as a potential terminator if used by some old system
                        SERVER_PRINT("WARNING: Invalid path index %d for waypoint %d, path %d in %s\n", path_idx, i, j, filename);
                    }
                }
            }
        }
        g_waypoint_paths = TRUE; WaypointRouteInit();
        SERVER_PRINT("Bot Memory: Loaded %d waypoints for map %s.\n", num_waypoints, header.map_name);
    } else {
        long waypoint_data_size_to_skip = 0;
        if (loaded_num_waypoints_from_file > 0) {
             waypoint_data_size_to_skip += sizeof(WAYPOINT) * loaded_num_waypoints_from_file;
             for (int i = 0; i < loaded_num_waypoints_from_file; ++i) {
                 short path_count_to_skip = 0;
                 long current_pos = ftell(fp);
                 if (fread(&path_count_to_skip, sizeof(short), 1, fp) != 1) {
                    SERVER_PRINT("ERROR: Failed to read path_count for skipping waypoint data for waypoint %d, file %s\n", i, filename);
                    fclose(fp); return false;
                 }
                 // No need to seek back, just use the value read for calculation
                 if (path_count_to_skip < 0 || path_count_to_skip > loaded_num_waypoints_from_file * MAX_PATH_INDEX) { // Sanity check
                     SERVER_PRINT("ERROR: Invalid path_count %d for skipping waypoint %d in %s\n", path_count_to_skip, i, filename);
                     fclose(fp); return false;
                 }
                 waypoint_data_size_to_skip += sizeof(short) * path_count_to_skip;
             }
        }
        if (waypoint_data_size_to_skip > 0) {
            if (fseek(fp, waypoint_data_size_to_skip, SEEK_CUR) != 0) {
                SERVER_PRINT("ERROR: Failed to seek past waypoint data in %s\n", filename);
                fclose(fp); return false;
            }
        }
        SERVER_PRINT("Bot Memory: Skipped loading waypoints from file due to map mismatch or 0 waypoints.\n");
    }

    // Load Persistent Bot Data
    if (header.num_bot_slots_in_file < 0 || header.num_bot_slots_in_file > 32) {
        SERVER_PRINT("ERROR: Invalid num_bot_slots_in_file (%d) in %s. Must be 0-32.\n", header.num_bot_slots_in_file, filename);
        fclose(fp); return false;
    }
    for (int i = 0; i < header.num_bot_slots_in_file; ++i) {
        persistent_bot_data_t pbd;
        if (fread(&pbd, sizeof(persistent_bot_data_t), 1, fp) != 1) {
            SERVER_PRINT("ERROR: Failed to read persistent_bot_data for bot slot %d from %s\n", i, filename);
            fclose(fp); return false;
        }

        bots[i].is_used = pbd.is_used_in_save;
        if (pbd.is_used_in_save) {
            strncpy(bots[i].name, pbd.name, BOT_NAME_LEN); bots[i].name[BOT_NAME_LEN] = '\0';
            strncpy(bots[i].skin, pbd.skin, BOT_SKIN_LEN); bots[i].skin[BOT_SKIN_LEN] = '\0';
            bots[i].bot_skill = pbd.bot_skill;
            bots[i].chat_percent = pbd.chat_percent;
            bots[i].taunt_percent = pbd.taunt_percent;
            bots[i].whine_percent = pbd.whine_percent;
            bots[i].logo_percent = pbd.logo_percent;
            bots[i].chat_tag_percent = pbd.chat_tag_percent;
            bots[i].chat_drop_percent = pbd.chat_drop_percent;
            bots[i].chat_swap_percent = pbd.chat_swap_percent;
            bots[i].chat_lower_percent = pbd.chat_lower_percent;
            bots[i].reaction_time = pbd.reaction_time;
            bots[i].top_color = pbd.top_color;
            bots[i].bottom_color = pbd.bottom_color;
            strncpy(bots[i].logo_name, pbd.logo_name, sizeof(bots[i].logo_name) - 1); bots[i].logo_name[sizeof(bots[i].logo_name) - 1] = '\0';
            for(int w = 0; w < 6; ++w) bots[i].weapon_points[w] = pbd.weapon_points[w];
            bots[i].sentrygun_waypoint = pbd.sentrygun_waypoint;
            bots[i].dispenser_waypoint = pbd.dispenser_waypoint;
            bots[i].bot_team = pbd.bot_team;
            bots[i].bot_class = pbd.bot_class;
            bots[i].loaded_from_persistence = true; // Mark as loaded

            if (pbd.has_saved_nn_weights) {
                NN_Initialize(&bots[i].tactical_nn, NN_INPUT_SIZE, NN_HIDDEN_SIZE, NUM_TACTICAL_DIRECTIVES, false, pbd.tactical_nn_weights);
                bots[i].nn_initialized = true;
            } else {
                bots[i].nn_initialized = false;
            }

            if (pbd.has_saved_aiming_nn) {
                RL_NN_Initialize_Aiming(&bots[i].aiming_rl_nn, RL_AIMING_STATE_SIZE, RL_AIMING_HIDDEN_LAYER_SIZE, RL_AIMING_OUTPUT_SIZE, false, pbd.aiming_rl_nn_weights);
                bots[i].aiming_nn_initialized = true;
            } else {
                bots[i].aiming_nn_initialized = false;
            }
        } else {
             bots[i].is_used = false; // Ensure bot is marked not used if slot data says so
             bots[i].nn_initialized = false;
             bots[i].aiming_nn_initialized = false;
             bots[i].loaded_from_persistence = false;
        }
    }
    SERVER_PRINT("Bot Memory: Processed %d bot slots.\n", header.num_bot_slots_in_file);

    // Load Discovered Objectives
    if (load_waypoints_for_this_map && header.num_discovered_objectives > 0 ) {
        if (header.num_discovered_objectives < 0 || header.num_discovered_objectives > MAX_OBJECTIVES_IN_DISCOVERY_LIST * 2) { // Sanity check
            SERVER_PRINT("ERROR: Invalid num_discovered_objectives (%d) in %s.\n", header.num_discovered_objectives, filename);
            // Potentially skip this section or return false
        } else {
            int actual_loaded_objectives = 0;
            for (int i = 0; i < header.num_discovered_objectives; ++i) {
                SavedDiscoveredObjective_t saved_obj;
                if (fread(&saved_obj, sizeof(SavedDiscoveredObjective_t), 1, fp) != 1) {
                    SERVER_PRINT("ERROR: Failed to read SavedDiscoveredObjective_t record %d from %s\n", i, filename);
                    break;
                }

                CandidateObjective_t* existing_cand = GetCandidateObjectiveById(saved_obj.unique_id);
                if (existing_cand) {
                    existing_cand->learned_objective_type = saved_obj.learned_objective_type;
                    existing_cand->confidence_score = saved_obj.confidence_score;
                    existing_cand->positive_event_correlations = saved_obj.positive_event_correlations;
                    existing_cand->negative_event_correlations = saved_obj.negative_event_correlations;
                    existing_cand->current_owner_team = saved_obj.current_owner_team;
                    existing_cand->learned_activation_method = saved_obj.learned_activation_method;
                    actual_loaded_objectives++;
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
                        new_cand.current_owner_team = saved_obj.current_owner_team;
                        new_cand.learned_activation_method = saved_obj.learned_activation_method;
                        new_cand.has_been_shared_as_confirmed = false;
                        new_cand.last_positive_correlation_update_time = 0.0f;
                        g_candidate_objectives.push_back(new_cand);
                        actual_loaded_objectives++;

                        if (new_cand.unique_id >= MAX_WAYPOINTS + 1 && new_cand.unique_id >= g_dynamic_candidate_id_counter) {
                            g_dynamic_candidate_id_counter = new_cand.unique_id + 1;
                        }
                    } else {
                        SERVER_PRINT("Bot Memory: Max objective list capacity reached, cannot load objective ID %d.\n", saved_obj.unique_id);
                    }
                }
            }
            SERVER_PRINT("Bot Memory: Loaded/Updated %d discovered objectives for map %s.\n", actual_loaded_objectives, header.map_name);
        }
    } else if (!load_waypoints_for_this_map && header.num_discovered_objectives > 0) {
        if (header.num_discovered_objectives > 0 && header.num_discovered_objectives < MAX_OBJECTIVES_IN_DISCOVERY_LIST * 2 /*sanity check*/) {
            long obj_data_size_to_skip = sizeof(SavedDiscoveredObjective_t) * header.num_discovered_objectives;
            if (fseek(fp, obj_data_size_to_skip, SEEK_CUR) != 0) {
                SERVER_PRINT("ERROR: Failed to seek past discovered objectives data in %s\n", filename);
                fclose(fp); return false;
            }
            SERVER_PRINT("Bot Memory: Skipped loading %d discovered objectives due to map mismatch.\n", header.num_discovered_objectives);
        } else if (header.num_discovered_objectives !=0) { // If it's not positive and less than sanity bound, it's an invalid number.
             SERVER_PRINT("WARNING: Invalid num_discovered_objectives (%d) in header, cannot skip reliably.\n", header.num_discovered_objectives);
        }
    }

    fclose(fp);
    SERVER_PRINT("Bot memory (V3) loaded successfully from %s for map %s.\n", filename, header.map_name);
    return true;
}
