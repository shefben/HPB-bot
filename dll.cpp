//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// dll.cpp
//

#ifndef _WIN32
#include <string.h>
#endif

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include <meta_api.h>
#include <entity_state.h>

#include "bot.h"
#include "bot_func.h"
#include "waypoint.h"
#include "bot_memory.h" // For LoadBotMemory/SaveBotMemory
#include "bot_tactical_ai.h" // For TacticalAI functions
#include "bot_objective_discovery.h" // For Objective Discovery functions
#include "bot_neuro_evolution.h" // For NE_PerformEvolutionaryCycle and constants
#include "bot_nlp_chat.h"      // For NLP chat functions and model access
#include "bot_ngram_functions.h" // For N-gram model loading and generation
#include <vector>              // For std::vector (used in NLP command)
#include <string>              // For std::string (used in NLP command)
#include <cstdlib>             // For srand, atoi
#include <ctime>               // For time (for srand)


// Define BOT_MEMORY_FILENAME if not globally visible (it's not from bot_memory.cpp's perspective for dll.cpp)
#ifndef BOT_MEMORY_FILENAME
#define BOT_MEMORY_FILENAME "user/hpb_bot_memory.dat"
#endif

#define VER_MAJOR 4
#define VER_MINOR 0


#define MENU_NONE  0
#define MENU_1     1
#define MENU_2     2
#define MENU_3     3
#define MENU_4     4


extern DLL_FUNCTIONS gFunctionTable;
extern enginefuncs_t g_engfuncs;
extern globalvars_t  *gpGlobals;
extern char g_argv[1024];
extern bool g_waypoint_on;
extern bool g_auto_waypoint;
extern bool g_path_waypoint;
extern bool g_path_waypoint_enable;
extern int num_waypoints;  // number of waypoints currently in use
extern WAYPOINT waypoints[MAX_WAYPOINTS];
extern float wp_display_time[MAX_WAYPOINTS];
extern bot_t bots[32];
extern bool b_observer_mode;
extern bool b_botdontshoot;
extern char welcome_msg[80];

static FILE *fp;

int mod_id = 0;
int m_spriteTexture = 0;
int default_bot_skill = 2;
int bot_strafe_percent = 20; // percent of time to strafe
int bot_chat_percent = 10;   // percent of time to chat
int bot_taunt_percent = 20;  // percent of time to taunt after kill
int bot_whine_percent = 10;  // percent of time to whine after death
int bot_grenade_time = 15;   // seconds between grenade throws
int bot_logo_percent = 40;   // percent of time to spray logo after kill

int bot_chat_tag_percent = 80;   // percent of the time to drop clan tag
int bot_chat_drop_percent = 10;  // percent of the time to drop characters
int bot_chat_swap_percent = 10;  // percent of the time to swap characters
int bot_chat_lower_percent = 50; // percent of the time to lowercase chat

bool b_random_color = TRUE;
bool isFakeClientCommand = FALSE;
int fake_arg_count;
int IsDedicatedServer;
float bot_check_time = 60.0;
int bot_reaction_time = 2;
int min_bots = -1;
int max_bots = -1;
int num_bots = 0;
int prev_num_bots = 0;
bool g_GameRules = FALSE;
edict_t *clients[32];
edict_t *listenserver_edict = NULL;
float welcome_time = 0.0;
bool welcome_sent = FALSE;
int g_menu_waypoint;
int g_menu_state = 0;
int bot_stop = 0;
int jumppad_off = 0;

bool is_team_play = FALSE;
char team_names[MAX_TEAMS][MAX_TEAMNAME_LENGTH];
int num_teams = 0;
bool checked_teamplay = FALSE;
cvar_t bot_debug_draw_objectives = {"bot_debug_draw_objectives", "0"}; // Existing CVar

// CVars for Neuro-Evolution Parameters
cvar_t bot_ne_mutation_rate = {"bot_ne_mutation_rate", "0.05", FCVAR_SERVER};
cvar_t bot_ne_mutation_strength = {"bot_ne_mutation_strength", "0.1", FCVAR_SERVER};
cvar_t bot_ne_tournament_size = {"bot_ne_tournament_size", "3", FCVAR_SERVER}; // Note: NE_TOURNAMENT_SIZE in .h is default
cvar_t bot_ne_num_elites = {"bot_ne_num_elites", "2", FCVAR_SERVER}; // Note: NE_NUM_ELITES_TO_KEEP in .h is default
cvar_t bot_ne_generation_period = {"bot_ne_generation_period", "180.0", FCVAR_SERVER}; // Seconds
cvar_t bot_ne_min_population = {"bot_ne_min_population", "4", FCVAR_SERVER};

// CVars for RL Aiming Parameters
cvar_t bot_rl_aim_learning_rate = {"bot_rl_aim_learning_rate", "0.001", FCVAR_SERVER};
cvar_t bot_rl_aim_discount_factor = {"bot_rl_aim_discount_factor", "0.99", FCVAR_SERVER};
cvar_t bot_rl_aim_exploration_epsilon = {"bot_rl_aim_exploration_epsilon", "0.1", FCVAR_SERVER};
cvar_t bot_rl_aim_episode_max_steps = {"bot_rl_aim_episode_max_steps", "100", FCVAR_SERVER};
cvar_t bot_rl_aim_action_interval = {"bot_rl_aim_action_interval", "0.1", FCVAR_SERVER}; // Interval for RL actions

// CVar for NLP Chat Model
cvar_t bot_chat_use_nlp_model = {"bot_chat_use_nlp_model", "0", FCVAR_SERVER};

// CVars for N-gram Chat
cvar_t bot_ngram_chat_enable = {"bot_ngram_chat_enable", "1", FCVAR_SERVER};
cvar_t bot_ngram_chat_idle_frequency = {"bot_ngram_chat_idle_frequency", "10", FCVAR_SERVER}; // % chance

edict_t *pent_info_tfdetect = NULL;
edict_t *pent_info_ctfdetect = NULL;
edict_t *pent_info_frontline = NULL;
edict_t *pent_item_tfgoal = NULL;
edict_t *pent_info_tfgoal = NULL;
int max_team_players[4];
int team_class_limits[4];
int team_allies[4];  // bit mapped allies BLUE, RED, YELLOW, and GREEN
int max_teams = 0;
int num_flags = 0;
FLAG_S flags[MAX_FLAGS];
int num_backpacks = 0;
BACKPACK_S backpacks[MAX_BACKPACKS];

FILE *bot_cfg_fp = NULL;
bool need_to_open_cfg = TRUE;
float bot_cfg_pause_time = 0.0;
float respawn_time = 0.0;
bool spawn_time_reset = FALSE;

char *show_menu_none = {" "};
char *show_menu_1 =
   {"Waypoint Tags\n\n1. Team Specific\n2. Wait for Lift\n3. Door\n4. Sniper Spot\n5. More..."};
char *show_menu_2 =
   {"Waypoint Tags\n\n1. Team 1\n2. Team 2\n3. Team 3\n4. Team 4\n5. CANCEL"};
char *show_menu_2_flf =
   {"Waypoint Tags\n\n1. Attackers\n2. Defenders\n\n5. CANCEL"};
char *show_menu_3 =
   {"Waypoint Tags\n\n1. Flag Location\n2. Flag Goal Location\n3. Sentry gun\n4. Dispenser\n5. More"};
char *show_menu_3_flf =
   {"Waypoint Tags\n\n1. Capture Point\n2. Defend Point\n3. Prone\n\n5. CANCEL"};
char *show_menu_3_hw =
   {"Waypoint Tags\n\n1. Halo Location\n\n\n\n5. More"};
char *show_menu_4 =
   {"Waypoint Tags\n\n1. Health\n2. Armor\n3. Ammo\n4. Jump\n5. CANCEL"};


void BotNameInit(void);
void BotLogoInit(void);
void UpdateClientData(const struct edict_s *ent, int sendweapons, struct clientdata_s *cd);
void ProcessBotCfgFile(void);
void HPB_Bot_ServerCommand (void);

extern void welcome_init(void);



// START of Metamod stuff
enginefuncs_t meta_engfuncs;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;
meta_globals_t *gpMetaGlobals;
META_FUNCTIONS gMetaFunctionTable = {NULL,NULL,GetEntityAPI2,NULL,NULL,NULL,GetEngineFunctions,NULL,};
plugin_info_t Plugin_info = {META_INTERFACE_VERSION,"HPB_Bot","4.0.4","09/11/2004","botman && Pierre-Marie Baty","http://hpb-bot.bots-united.com/","HPB_BOT",PT_STARTUP,PT_ANYTIME,};
C_DLLEXPORT int Meta_Query (const char *ifvers, plugin_info_t **pPlugInfo, mutil_funcs_t *pMetaUtilFuncs){gpMetaUtilFuncs = pMetaUtilFuncs;*pPlugInfo = &Plugin_info;if (strcmp (ifvers, Plugin_info.ifvers) != 0){int mmajor = 0, mminor = 0, pmajor = 0, pminor = 0;LOG_CONSOLE (PLID, "%s: meta-interface version mismatch (metamod: %s, %s: %s)", Plugin_info.name, ifvers, Plugin_info.name, Plugin_info.ifvers);LOG_MESSAGE (PLID, "%s: meta-interface version mismatch (metamod: %s, %s: %s)", Plugin_info.name, ifvers, Plugin_info.name, Plugin_info.ifvers);sscanf (ifvers, "%d:%d", &mmajor, &mminor);sscanf (META_INTERFACE_VERSION, "%d:%d", &pmajor, &pminor);if ((pmajor > mmajor) || ((pmajor == mmajor) && (pminor > mminor))){LOG_CONSOLE (PLID, "metamod version is too old for this plugin; update metamod");LOG_ERROR (PLID, "metamod version is too old for this plugin; update metamod");return (FALSE);}else if (pmajor < mmajor){LOG_CONSOLE (PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");LOG_ERROR (PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");return (FALSE);}}return (TRUE); }
C_DLLEXPORT int Meta_Attach (PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs){if (now > Plugin_info.loadable){LOG_CONSOLE (PLID, "%s: plugin NOT attaching (can't load plugin right now)", Plugin_info.name);LOG_ERROR (PLID, "%s: plugin NOT attaching (can't load plugin right now)", Plugin_info.name);return (FALSE); }gpMetaGlobals = pMGlobals;memcpy (pFunctionTable, &gMetaFunctionTable, sizeof (META_FUNCTIONS));gpGamedllFuncs = pGamedllFuncs;LOG_CONSOLE (PLID, "%s: plugin attaching", Plugin_info.name);LOG_MESSAGE (PLID, "%s: plugin attaching", Plugin_info.name);REG_SVR_COMMAND ("HPB_Bot", HPB_Bot_ServerCommand);return (TRUE); }
C_DLLEXPORT int Meta_Detach (PLUG_LOADTIME now, PL_UNLOAD_REASON reason){if ((now > Plugin_info.unloadable) && (reason != PNL_CMD_FORCED)){LOG_CONSOLE (PLID, "%s: plugin NOT detaching (can't unload plugin right now)", Plugin_info.name);LOG_ERROR (PLID, "%s: plugin NOT detaching (can't unload plugin right now)", Plugin_info.name);return (FALSE); }return (TRUE); }
// END of Metamod stuff


void GameDLLInit( void )
{
   int i;
   IsDedicatedServer = IS_DEDICATED_SERVER();
   for (i=0; i<32; i++)
      clients[i] = NULL;
   welcome_init();
   memset(bots, 0, sizeof(bots));
   BotNameInit();
   BotLogoInit();
   LoadBotChat();
   LoadBotModels();
   CVAR_REGISTER(&bot_debug_draw_objectives);

   // Register NE CVars
   CVAR_REGISTER(&bot_ne_mutation_rate);
   CVAR_REGISTER(&bot_ne_mutation_strength);
   CVAR_REGISTER(&bot_ne_tournament_size);
   CVAR_REGISTER(&bot_ne_num_elites);
   CVAR_REGISTER(&bot_ne_generation_period);
   CVAR_REGISTER(&bot_ne_min_population);

   // Register RL Aiming CVars
   CVAR_REGISTER(&bot_rl_aim_learning_rate);
   CVAR_REGISTER(&bot_rl_aim_discount_factor);
   CVAR_REGISTER(&bot_rl_aim_exploration_epsilon);
   CVAR_REGISTER(&bot_rl_aim_episode_max_steps);
   CVAR_REGISTER(&bot_rl_aim_action_interval);

   // Register NLP Chat CVar
   CVAR_REGISTER(&bot_chat_use_nlp_model);

   // Register N-gram Chat CVars
   CVAR_REGISTER(&bot_ngram_chat_enable);
   CVAR_REGISTER(&bot_ngram_chat_idle_frequency);

   // Seed random number generator (globally for the DLL)
   srand((unsigned int)time(NULL));

   // Initialize NLP Chat Model
   ALERT(at_console, "NLP_Chat: Initializing N-gram model...\n");
   std::vector<std::string> chat_corpus;
   NLP_LoadCorpusFromFile("HPB_bot_chat.txt", NULL, chat_corpus); // mod_name_for_path is unused

   if (!chat_corpus.empty()) {
       const int N_FOR_CHAT_MODEL = 3; // Use trigrams
       NLP_TrainModel(chat_corpus, N_FOR_CHAT_MODEL, g_chat_ngram_model);
       g_ngram_model_N_value = N_FOR_CHAT_MODEL;
       if (g_chat_ngram_model.empty()) {
           ALERT(at_console, "NLP_Chat: WARNING - N-gram model is empty after training! Check corpus and N value.\n");
       }
   } else {
       ALERT(at_console, "NLP_Chat: WARNING - Chat corpus is empty. NLP Chat disabled.\n");
   }

   // Load Advanced Categorized Chat File
   AdvancedChat_LoadChatFile("HPB_bot_adv_chat.txt");

   // Load N-gram Chat Model Data (This call is already present as per analysis)
   // if (AdvancedChat_LoadNgramData("user/ngram_chat_model.txt")) {
   //     SERVER_PRINT("N-gram chat model loaded successfully from user/ngram_chat_model.txt.\n");
   // } else {
   //     SERVER_PRINT("Warning: Failed to load N-gram chat model from user/ngram_chat_model.txt. N-gram chat may be disabled or ineffective.\n");
   // }

   RETURN_META (MRES_IGNORED);
}

int Spawn( edict_t *pent )
{
   int index;
   if (gpGlobals->deathmatch)
   {
      char *pClassname = (char *)STRING(pent->v.classname);
      if (strcmp(pClassname, "worldspawn") == 0)
      {
         WaypointInit();
         WaypointLoad(NULL);
         // TacticalAI_LevelInit() and ObjectiveDiscovery_LevelInit() are called in StartFrame's new map logic
         pent_info_tfdetect = NULL;pent_info_ctfdetect = NULL;pent_info_frontline = NULL;pent_item_tfgoal = NULL;pent_info_tfgoal = NULL;
         for (index=0; index < 4; index++){max_team_players[index] = 0;team_class_limits[index] = 0;team_allies[index] = 0;}
         max_teams = 0;num_flags = 0;
         for (index=0; index < MAX_FLAGS; index++){flags[index].edict = NULL;flags[index].team_no = 0;}
         num_backpacks = 0;
         for (index=0; index < MAX_BACKPACKS; index++){backpacks[index].edict = NULL;backpacks[index].armor = 0;backpacks[index].health = 0;backpacks[index].ammo = 0;backpacks[index].team = 0;}
         PRECACHE_SOUND("weapons/xbow_hit1.wav");PRECACHE_SOUND("weapons/mine_activate.wav");PRECACHE_SOUND("common/wpn_hudoff.wav"); PRECACHE_SOUND("common/wpn_hudon.wav");PRECACHE_SOUND("common/wpn_moveselect.wav");PRECACHE_SOUND("common/wpn_denyselect.wav");PRECACHE_SOUND("player/sprayer.wav");
         m_spriteTexture = PRECACHE_MODEL( "sprites/lgtning.spr");
         g_GameRules = TRUE;is_team_play = FALSE;memset(team_names, 0, sizeof(team_names));num_teams = 0;checked_teamplay = FALSE;
         bot_cfg_pause_time = 0.0;respawn_time = 0.0;spawn_time_reset = FALSE;
         prev_num_bots = num_bots;num_bots = 0;
         bot_check_time = gpGlobals->time + 60.0;
      }
      if ((mod_id == HOLYWARS_DLL) && (jumppad_off) && (strcmp(pClassname, "trigger_jumppad") == 0))
      { RETURN_META_VALUE (MRES_SUPERCEDE, -1); }
   }
   RETURN_META_VALUE (MRES_IGNORED, 0);
}

void KeyValue( edict_t *pentKeyvalue, KeyValueData *pkvd ){/*...omitted for brevity...*/}
BOOL ClientConnect( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ]  ){/*...omitted for brevity...*/ RETURN_META_VALUE(MRES_IGNORED, 0);}
void ClientDisconnect( edict_t *pEntity ){/*...omitted for brevity...*/ RETURN_META(MRES_IGNORED);}
void ClientPutInServer( edict_t *pEntity ){int i=0; while((i<32)&&(clients[i]!=NULL))i++; if(i<32)clients[i]=pEntity; RETURN_META(MRES_IGNORED);}

extern std::vector<CandidateObjective_t> g_candidate_objectives;
extern std::list<GameEvent_t> g_game_event_log;
const char* ObjectiveTypeToString(ObjectiveType_e obj_type);
const char* GameEventTypeToString(GameEventType_e event_type);
const char* ActivationMethodToString(ActivationMethod_e act_meth);

void ClientCommand( edict_t *pEntity )
{
   const char *pcmd = CMD_ARGV (0);
   const char *arg1 = CMD_ARGV (1);
   // ... (args 2-5)
   if ((gpGlobals->deathmatch) && (!IsDedicatedServer) && (pEntity == listenserver_edict))
   {
      char msg[80];
      char big_msg[512];
      if (FStrEq(pcmd, "addbot")){BotCreate( pEntity, CMD_ARGV(1), CMD_ARGV(2), CMD_ARGV(3), CMD_ARGV(4), CMD_ARGV(5) ); bot_check_time = gpGlobals->time + 5.0; RETURN_META (MRES_SUPERCEDE);}
      // ... (other hpb_bot commands omitted for brevity)
      else if (FStrEq(pcmd, "bot_list_candidates"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) { ClientPrint(pEntity, HUD_PRINTCONSOLE, "This command is for listen server admin only.\n"); RETURN_META(MRES_SUPERCEDE);}
          sprintf(big_msg, "--- Candidate Objectives (Count: %zu) ---\n", g_candidate_objectives.size()); ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
              const CandidateObjective_t& cand = g_candidate_objectives[i];
              const char* type_str = ObjectiveTypeToString(cand.learned_objective_type);
              const char* act_meth_str = ActivationMethodToString(cand.learned_activation_method);
              sprintf(big_msg, "ID:%d|T:%s|Own:%d|Act:%s|C:%.2f|P:%d|N:%d|L:(%.0f,%.0f,%.0f)|Cls:%s|Tgt:%s|Team:%d@%.1f\n",
                      cand.unique_id, type_str, cand.current_owner_team, act_meth_str,
                      cand.confidence_score, cand.positive_event_correlations, cand.negative_event_correlations,
                      cand.location.x, cand.location.y, cand.location.z,
                      cand.entity_classname[0] ? cand.entity_classname : "-", cand.entity_targetname[0] ? cand.entity_targetname : "-",
                      cand.last_interacting_team, cand.last_interaction_time);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End of List ---\n"); RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_list_events"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) { ClientPrint(pEntity, HUD_PRINTCONSOLE, "This command is for listen server admin only.\n"); RETURN_META(MRES_SUPERCEDE);}
          int count_to_show = 10; const char *arg_count_str = CMD_ARGV(1);
          if (arg_count_str && arg_count_str[0] != '\0') {count_to_show = atoi(arg_count_str); if (count_to_show <= 0 || count_to_show > 100) count_to_show = 10;}
          sprintf(big_msg, "--- Last %d Game Events (Total: %zu) ---\n", count_to_show, g_game_event_log.size()); ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          int shown_count = 0;
          for (auto it = g_game_event_log.rbegin(); it != g_game_event_log.rend() && shown_count < count_to_show; ++it, ++shown_count) {
              const GameEvent_t& evt = *it; const char* event_type_str = GameEventTypeToString(evt.type);
              sprintf(big_msg, "T:%.1f|Type:%s|T1:%d|T2:%d|CandID:%d|PlyrEdict:%d|VF:%.2f|VI:%d|Msg:'%s'\n",
                      evt.timestamp, event_type_str, evt.primarily_involved_team_id, evt.secondary_involved_team_id,
                      evt.candidate_objective_id, evt.involved_player_user_id, evt.event_value_float,
                      evt.event_value_int, evt.event_message_text[0] ? evt.event_message_text : "-");
              ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End of Events List ---\n"); RETURN_META(MRES_SUPERCEDE);
      }
#if _DEBUG
      else if (FStrEq(pcmd, "botstop")){bot_stop = 1; RETURN_META (MRES_SUPERCEDE);}
      else if (FStrEq(pcmd, "botstart")){bot_stop = 0; RETURN_META (MRES_SUPERCEDE);}
#endif
      // NE Debug Commands
      else if (FStrEq(pcmd, "bot_ne_force_cycle"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n");
              RETURN_META(MRES_SUPERCEDE);
          }

          std::vector<bot_t*> active_bot_population;
          active_bot_population.reserve(32);
          for (int i = 0; i < 32; ++i) {
              if (bots[i].is_used && bots[i].nn_initialized) {
                  active_bot_population.push_back(&bots[i]);
              }
          }

          if (active_bot_population.size() >= (size_t)bot_ne_min_population.value) { // Use CVar value
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Forcing NE Cycle...\n");
              SERVER_PRINT("Admin forced NE Cycle.\n");
              NE_PerformEvolutionaryCycle(active_bot_population, &GetGlobalTacticalState());
              // next_evolution_cycle_time is modified in StartFrame using bot_ne_generation_period.value
              // Forcing a cycle here means the next one will be scheduled from this forced cycle's completion time + period.
              // If g_next_evolution_cycle_time is made accessible (e.g. by removing static from its definition in StartFrame)
              // then it can be updated here:
              // g_next_evolution_cycle_time = gpGlobals->time + bot_ne_generation_period.value;
          } else {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Not enough bots for NE cycle.\n");
          }
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_ne_print_fitness"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n");
              RETURN_META(MRES_SUPERCEDE);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- Bot Fitness Scores ---\n");
          char msg_fitness[256]; // Ensure unique name
          for (int i = 0; i < 32; ++i) {
              if (bots[i].is_used && bots[i].nn_initialized) {
                  // Using current_eval_ fields for "live" evaluation period stats
                  sprintf(msg_fitness, "Bot %s (Idx %d): StoredFit=%.2f | EvalScore=%.0f K:%d D:%d Obj:%d Dmg:%.0f Surv:%.0fs\n",
                          bots[i].name, i, bots[i].fitness_score,
                          bots[i].current_eval_score_contribution, bots[i].current_eval_kills,
                          bots[i].current_eval_deaths, bots[i].current_eval_objectives_captured_or_defended,
                          bots[i].current_eval_damage_dealt, (gpGlobals->time - bots[i].current_eval_survival_start_time));
                  ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_fitness);
              }
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End Fitness Scores ---\n");
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_ne_print_bot_nn"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n");
              RETURN_META(MRES_SUPERCEDE);
          }
          const char* arg_bot_id = CMD_ARGV(1);
          if (!arg_bot_id || arg_bot_id[0] == '\0') {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Usage: bot_ne_print_bot_nn <bot_index_or_name>\n");
              RETURN_META(MRES_SUPERCEDE);
          }

          bot_t* pFoundBot = NULL;
          // Try parsing as integer index first
          char* endptr;
          long val = strtol(arg_bot_id, &endptr, 10);
          if (*endptr == '\0' && val >= 0 && val < 32) { // Check if it was a valid integer conversion and in range
             if(bots[val].is_used) pFoundBot = &bots[val];
          } else { // Try by name if not a valid index
              for(int i=0; i<32; ++i) { if(bots[i].is_used && stricmp(bots[i].name, arg_bot_id) == 0) { pFoundBot = &bots[i]; break; } }
          }

          if (pFoundBot && pFoundBot->nn_initialized) {
              char msg_nn[512]; // Ensure unique name
              TacticalNeuralNetwork_t* nn = &pFoundBot->tactical_nn;
              sprintf(msg_nn, "NN Info for Bot %s (Idx %d):\nInputs: %d, Hidden: %d, Outputs: %d\n",
                      pFoundBot->name, (int)(pFoundBot - bots), nn->num_input_neurons, nn->num_hidden_neurons, nn->num_output_neurons);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_nn);

              float w_sum_ih = 0; for(float w : nn->weights_input_hidden) w_sum_ih += w;
              float b_sum_h = 0;  for(float b : nn->bias_hidden) b_sum_h += b;
              float w_sum_ho = 0; for(float w : nn->weights_hidden_output) w_sum_ho += w;
              float b_sum_o = 0;  for(float b : nn->bias_output) b_sum_o += b;
              sprintf(msg_nn, "Weight Sums: IH=%.2f, HBias=%.2f, HO=%.2f, OBias=%.2f\n", w_sum_ih, b_sum_h, w_sum_ho, b_sum_o);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_nn);

              if (!nn->weights_input_hidden.empty()) {
                  sprintf(msg_nn, "First few IH weights: %.3f %.3f %.3f ...\n",
                          nn->weights_input_hidden[0],
                          nn->weights_input_hidden[1 % nn->weights_input_hidden.size()],
                          nn->weights_input_hidden[2 % nn->weights_input_hidden.size()]);
                  ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_nn);
              }
          } else {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Bot not found or NN not initialized.\n");
          }
          RETURN_META(MRES_SUPERCEDE);
      }
      // RL Aiming Debug Commands
      else if (FStrEq(pcmd, "bot_rl_print_aim_stats"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n"); RETURN_META(MRES_SUPERCEDE); }
          const char* arg_bot_id = CMD_ARGV(1);
          if (!arg_bot_id || arg_bot_id[0] == '\0') { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Usage: bot_rl_print_aim_stats <bot_idx_or_name>\n"); RETURN_META(MRES_SUPERCEDE); }

          bot_t* pFoundBot = NULL;
          char* endptr; long val = strtol(arg_bot_id, &endptr, 10);
          if (*endptr == '\0' && val >= 0 && val < 32 && bots[val].is_used) { pFoundBot = &bots[val]; }
          else { for(int i=0; i<32; ++i) { if(bots[i].is_used && stricmp(bots[i].name, arg_bot_id) == 0) { pFoundBot = &bots[i]; break; } } }

          if (pFoundBot && pFoundBot->aiming_nn_initialized && !pFoundBot->current_aiming_episode_data.empty()) {
              char msg_rl_stats[512];
              const RL_Aiming_Experience_t& last_exp = pFoundBot->current_aiming_episode_data.back();
              sprintf(msg_rl_stats, "Bot %s RL Aim Stats (Last Step):\n  State (first 3): %.2f, %.2f, %.2f ...\n  Action: %s (LogProb: %.3f)\n  Reward: %.2f\n  Episode Steps: %d\n",
                      pFoundBot->name, last_exp.state_features[0], last_exp.state_features[1], last_exp.state_features[2],
                      RL_AimingActionToString(last_exp.action_taken), last_exp.log_prob_action,
                      last_exp.reward_received, pBot->aiming_episode_step_count); // Use pBot for aiming_episode_step_count if it's the target bot
              ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_rl_stats);
          } else { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Bot not found, Aiming NN not init, or no episode data.\n"); }
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_rl_print_aim_nn"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n"); RETURN_META(MRES_SUPERCEDE); }
          const char* arg_bot_id = CMD_ARGV(1);
          if (!arg_bot_id || arg_bot_id[0] == '\0') { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Usage: bot_rl_print_aim_nn <bot_idx_or_name>\n"); RETURN_META(MRES_SUPERCEDE); }

          bot_t* pFoundBot = NULL;
          char* endptr; long val = strtol(arg_bot_id, &endptr, 10);
          if (*endptr == '\0' && val >= 0 && val < 32 && bots[val].is_used) { pFoundBot = &bots[val]; }
          else { for(int i=0; i<32; ++i) { if(bots[i].is_used && stricmp(bots[i].name, arg_bot_id) == 0) { pFoundBot = &bots[i]; break; } } }

          if (pFoundBot && pFoundBot->aiming_nn_initialized) {
              char msg_rl_nn[256];
              RL_Aiming_NN_t* nn = &pFoundBot->aiming_rl_nn;
              sprintf(msg_rl_nn, "RL Aim NN Info for Bot %s:\nInputs: %d, Hidden: %d, Outputs: %d\n",
                      pFoundBot->name, nn->num_input_neurons, nn->num_hidden_neurons, nn->num_output_neurons);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_rl_nn);
              // Could add weight sums here too
          } else { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Bot not found or Aiming NN not initialized.\n"); }
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_rl_force_aim_update"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n"); RETURN_META(MRES_SUPERCEDE); }
          const char* arg_bot_id = CMD_ARGV(1);
          if (!arg_bot_id || arg_bot_id[0] == '\0') { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Usage: bot_rl_force_aim_update <bot_idx_or_name>\n"); RETURN_META(MRES_SUPERCEDE); }

          bot_t* pFoundBot = NULL;
          char* endptr; long val = strtol(arg_bot_id, &endptr, 10);
          if (*endptr == '\0' && val >= 0 && val < 32 && bots[val].is_used) { pFoundBot = &bots[val]; }
          else { for(int i=0; i<32; ++i) { if(bots[i].is_used && stricmp(bots[i].name, arg_bot_id) == 0) { pFoundBot = &bots[i]; break; } } }

          if (pFoundBot && pFoundBot->aiming_nn_initialized && !pFoundBot->current_aiming_episode_data.empty()) {
              RL_UpdatePolicyNetwork_REINFORCE(pFoundBot, bot_rl_aim_learning_rate.value, bot_rl_aim_discount_factor.value);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Forced RL Aim NN update for bot.\n");
          } else { ClientPrint(pEntity, HUD_PRINTCONSOLE, "Bot not found, NN not init, or no episode data to update from.\n"); }
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_test_nlp_chat"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "Command only for listen server admin or server console.\n");
              RETURN_META(MRES_SUPERCEDE);
          }

          if (g_chat_ngram_model.empty() || g_ngram_model_N_value <= 0) {
              ClientPrint(pEntity, HUD_PRINTCONSOLE, "NLP Chat Model not trained or N is invalid.\n");
              RETURN_META(MRES_SUPERCEDE);
          }

          int num_to_generate = 5;
          int n_val_to_use = g_ngram_model_N_value;
          int max_words_to_use = 15;

          const char* arg_num_gen_str = CMD_ARGV(1);
          if (arg_num_gen_str && arg_num_gen_str[0] != '\0') {
              num_to_generate = atoi(arg_num_gen_str);
              if (num_to_generate <= 0 || num_to_generate > 50) num_to_generate = 5;
          }

          const char* arg_n_val_str = CMD_ARGV(2); // This argument is informational, generation uses g_ngram_model_N_value
          if (arg_n_val_str && arg_n_val_str[0] != '\0') {
             int temp_n = atoi(arg_n_val_str);
             if (temp_n != g_ngram_model_N_value) {
                 ClientPrint(pEntity, HUD_PRINTCONSOLE, "NLP Test: Command uses globally trained N-value (%d). Argument for N is ignored.\n", g_ngram_model_N_value);
             }
          }

          const char* arg_max_words_str = CMD_ARGV(3);
          if (arg_max_words_str && arg_max_words_str[0] != '\0') {
              max_words_to_use = atoi(arg_max_words_str);
              if (max_words_to_use <= 0 || max_words_to_use > 50) max_words_to_use = 15;
          }

          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- Generating Test NLP Chat Messages (N=%d, MaxWords=%d) ---\n", n_val_to_use, max_words_to_use);
          char msg_buf[256];

          for (int i = 0; i < num_to_generate; ++i) {
              std::string generated_message = NLP_GenerateChatMessage(g_chat_ngram_model, n_val_to_use, max_words_to_use);
              if (generated_message.empty()) {
                  sprintf(msg_buf, "%d: <generation failed or empty>\n", i + 1);
              } else {
                  sprintf(msg_buf, "%d: %s\n", i + 1, generated_message.c_str());
              }
              ClientPrint(pEntity, HUD_PRINTCONSOLE, msg_buf);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End of Test NLP Chat Messages ---\n");

          RETURN_META(MRES_SUPERCEDE);
      }
   }
   RETURN_META (MRES_IGNORED);
}

// Make g_next_evolution_cycle_time a file-global static
static float g_next_evolution_cycle_time = 0.0f;

void StartFrame( void )
{
   if (gpGlobals->deathmatch)
   {
      edict_t *pPlayer;
      static int i, index, player_index, bot_index; // These can remain static to StartFrame if not needed elsewhere
      static float previous_time = -1.0; // This too
      static float next_tactical_update_time = 0.0f;
      static float next_obj_discovery_update_time = 0.0f;
      static float next_obj_analysis_time = 0.0f;
      static float next_obj_type_update_time = 0.0f;
      char msg_sf[256];
      int count_sf;

      if ((gpGlobals->time + 0.1) < previous_time)
      {
         if (previous_time > 0.0) { SaveBotMemory(BOT_MEMORY_FILENAME); }
         LoadBotMemory(BOT_MEMORY_FILENAME);
         TacticalAI_LevelInit();
         ObjectiveDiscovery_LevelInit(); // This should be called after waypoints/objectives are loaded

         char filename_cfg[256]; char mapname_cfg[64];
         strcpy(mapname_cfg, STRING(gpGlobals->mapname)); strcat(mapname_cfg, "_HPB_bot.cfg");
         UTIL_BuildFileName(filename_cfg, "maps", mapname_cfg);
         if ((bot_cfg_fp = fopen(filename_cfg, "r")) != NULL){ sprintf(msg_sf, "Executing %s\n", filename_cfg); ALERT( at_console, msg_sf ); for (index = 0; index < 32; index++){bots[index].is_used = FALSE; bots[index].respawn_state = 0; bots[index].f_kick_time = 0.0;} if (IsDedicatedServer) bot_cfg_pause_time = gpGlobals->time + 5.0; else bot_cfg_pause_time = gpGlobals->time + 20.0;}
         else { count_sf = 0; for (index = 0; index < 32; index++){if (count_sf >= prev_num_bots){bots[index].is_used = FALSE; bots[index].respawn_state = 0; bots[index].f_kick_time = 0.0;} if (bots[index].is_used){bots[index].respawn_state = RESPAWN_NEED_TO_RESPAWN; count_sf++;} if ((bots[index].f_kick_time + 5.0) > previous_time){bots[index].respawn_state = RESPAWN_NEED_TO_RESPAWN; count_sf++;} else bots[index].f_kick_time = 0.0;} if (IsDedicatedServer) respawn_time = gpGlobals->time + 5.0; else respawn_time = gpGlobals->time + 20.0;}
         bot_check_time = gpGlobals->time + 60.0;
      }

      if (!IsDedicatedServer) {if ((listenserver_edict != NULL) && (welcome_sent == FALSE) && (welcome_time < 1.0)){if (IsAlive(listenserver_edict)) welcome_time = gpGlobals->time + 5.0;} if ((welcome_time > 0.0) && (welcome_time < gpGlobals->time) && (welcome_sent == FALSE)){char version[80]; sprintf(version,"%s Version %d.%d\n", welcome_msg, VER_MAJOR, VER_MINOR); UTIL_SayText(version, listenserver_edict); welcome_sent = TRUE;}}

      count_sf = 0; // Reset count_sf for this frame
      if (bot_stop == 0)
      {
         for (bot_index = 0; bot_index < gpGlobals->maxClients; bot_index++)
         {
            if ((bots[bot_index].is_used) && (bots[bot_index].respawn_state == RESPAWN_IDLE))
            { BotThink(&bots[bot_index]); count_sf++;}
         }
      }
      if (count_sf > num_bots) num_bots = count_sf;

      if (gpGlobals->time >= next_tactical_update_time) { TacticalAI_UpdatePeriodicState(); next_tactical_update_time = gpGlobals->time + 1.0f; }
      if (gpGlobals->time >= next_obj_discovery_update_time) { ObjectiveDiscovery_UpdatePeriodic(); next_obj_discovery_update_time = gpGlobals->time + 2.0f;}
      if (gpGlobals->time >= next_obj_analysis_time) { ObjectiveDiscovery_AnalyzeEvents(); next_obj_analysis_time = gpGlobals->time + 7.5f;}
      if (gpGlobals->time >= next_obj_type_update_time) { ObjectiveDiscovery_UpdateLearnedTypes(); next_obj_type_update_time = gpGlobals->time + 8.0f; }


      if (bot_debug_draw_objectives.value > 0 && listenserver_edict != NULL) {
         ObjectiveDiscovery_DrawDebugVisuals(listenserver_edict);
      }

      for (player_index = 1; player_index <= gpGlobals->maxClients; player_index++)
      {
         pPlayer = INDEXENT(player_index);
         if (pPlayer && !pPlayer->free)
         {
            if ((g_waypoint_on) && FBitSet(pPlayer->v.flags, FL_CLIENT) && !FBitSet(pPlayer->v.flags, FL_FAKECLIENT))
            { WaypointThink(pPlayer); }
         }
      }
      if ((respawn_time > 1.0) && (respawn_time <= gpGlobals->time)) {/*...omitted for brevity...*/}
      if (g_GameRules){ if (need_to_open_cfg){/*...omitted for brevity...*/} if ((bot_cfg_fp) && (bot_cfg_pause_time >= 1.0) && (bot_cfg_pause_time <= gpGlobals->time)) { ProcessBotCfgFile();}}
      if (bot_check_time < gpGlobals->time){/*...omitted for brevity...*/}

      // Neuro-Evolution Cycle Trigger
      // Use g_next_evolution_cycle_time and CVars
      if (gpGlobals->time >= g_next_evolution_cycle_time && gpGlobals->time > 10.0f /* Min game time before starting EA */) {
          g_next_evolution_cycle_time = gpGlobals->time + bot_ne_generation_period.value; // Use CVar

          std::vector<bot_t*> active_bot_population;
          active_bot_population.reserve(32);
          for (int i = 0; i < 32; ++i) {
              if (bots[i].is_used && bots[i].nn_initialized) {
                  active_bot_population.push_back(&bots[i]);
              }
          }

          if (active_bot_population.size() >= (size_t)bot_ne_min_population.value) { // Use CVar
              // ALERT(at_console, "NE: Starting evolutionary cycle with %zu bots.\n", active_bot_population.size());
              const GlobalTacticalState_t& tactical_state = GetGlobalTacticalState();
              NE_PerformEvolutionaryCycle(active_bot_population, &tactical_state);
          } else {
              // ALERT(at_console, "NE: Not enough bots (%zu) for evolution. Min required: %d. Resetting stats.\n",
              //         active_bot_population.size(), (int)bot_ne_min_population.value); // Use CVar
              // If not enough bots for a full cycle, just reset their fitness stats
              // so they don't accumulate indefinitely across evaluation periods.
              // NE_PerformEvolutionaryCycle also does this if count is too small, but this is an explicit fallback.
              for (bot_t* pBot : active_bot_population) {
                  if (pBot) { // Basic null check though active_bot_population should only have valid pointers
                    NE_ResetBotFitnessStats(pBot);
                  }
              }
          }
      }

      previous_time = gpGlobals->time;
   }
   RETURN_META (MRES_IGNORED);
}

void FakeClientCommand(edict_t *pBot, char *arg1, char *arg2, char *arg3){/*...omitted for brevity...*/}
void ProcessBotCfgFile(void){/*...omitted for brevity...*/}
void HPB_Bot_ServerCommand (void){/*...omitted for brevity...*/}

C_DLLEXPORT int GetEntityAPI2 (DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion)
{
   gFunctionTable.pfnGameInit = GameDLLInit;
   gFunctionTable.pfnSpawn = Spawn;
   gFunctionTable.pfnKeyValue = KeyValue;
   gFunctionTable.pfnClientConnect = ClientConnect;
   gFunctionTable.pfnClientDisconnect = ClientDisconnect;
   gFunctionTable.pfnClientPutInServer = ClientPutInServer;
   gFunctionTable.pfnClientCommand = ClientCommand;
   gFunctionTable.pfnStartFrame = StartFrame;
   memcpy (pFunctionTable, &gFunctionTable, sizeof (DLL_FUNCTIONS));
   return (TRUE);
}
