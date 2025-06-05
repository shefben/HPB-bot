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
cvar_t bot_debug_draw_objectives = {"bot_debug_draw_objectives", "0"}; // New CVar
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

META_FUNCTIONS gMetaFunctionTable =
{
   NULL, // pfnGetEntityAPI()
   NULL, // pfnGetEntityAPI_Post()
   GetEntityAPI2, // pfnGetEntityAPI2()
   NULL, // pfnGetEntityAPI2_Post()
   NULL, // pfnGetNewDLLFunctions()
   NULL, // pfnGetNewDLLFunctions_Post()
   GetEngineFunctions, // pfnGetEngineFunctions()
   NULL, // pfnGetEngineFunctions_Post()
};

plugin_info_t Plugin_info = {
   META_INTERFACE_VERSION, // interface version
   "HPB_Bot", // plugin name
   "4.0.4", // plugin version
   "09/11/2004", // date of creation
   "botman && Pierre-Marie Baty", // plugin author
   "http://hpb-bot.bots-united.com/", // plugin URL
   "HPB_BOT", // plugin logtag
   PT_STARTUP, // when loadable
   PT_ANYTIME, // when unloadable
};


C_DLLEXPORT int Meta_Query (const char *ifvers, plugin_info_t **pPlugInfo, mutil_funcs_t *pMetaUtilFuncs)
{
   gpMetaUtilFuncs = pMetaUtilFuncs;
   *pPlugInfo = &Plugin_info;
   if (strcmp (ifvers, Plugin_info.ifvers) != 0)
   {
      int mmajor = 0, mminor = 0, pmajor = 0, pminor = 0;
      LOG_CONSOLE (PLID, "%s: meta-interface version mismatch (metamod: %s, %s: %s)", Plugin_info.name, ifvers, Plugin_info.name, Plugin_info.ifvers);
      LOG_MESSAGE (PLID, "%s: meta-interface version mismatch (metamod: %s, %s: %s)", Plugin_info.name, ifvers, Plugin_info.name, Plugin_info.ifvers);
      sscanf (ifvers, "%d:%d", &mmajor, &mminor);
      sscanf (META_INTERFACE_VERSION, "%d:%d", &pmajor, &pminor);
      if ((pmajor > mmajor) || ((pmajor == mmajor) && (pminor > mminor)))
      {
         LOG_CONSOLE (PLID, "metamod version is too old for this plugin; update metamod");
         LOG_ERROR (PLID, "metamod version is too old for this plugin; update metamod");
         return (FALSE);
      }
      else if (pmajor < mmajor)
      {
         LOG_CONSOLE (PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");
         LOG_ERROR (PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");
         return (FALSE);
      }
   }
   return (TRUE);
}


C_DLLEXPORT int Meta_Attach (PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs)
{
   if (now > Plugin_info.loadable)
   {
      LOG_CONSOLE (PLID, "%s: plugin NOT attaching (can't load plugin right now)", Plugin_info.name);
      LOG_ERROR (PLID, "%s: plugin NOT attaching (can't load plugin right now)", Plugin_info.name);
      return (FALSE);
   }
   gpMetaGlobals = pMGlobals;
   memcpy (pFunctionTable, &gMetaFunctionTable, sizeof (META_FUNCTIONS));
   gpGamedllFuncs = pGamedllFuncs;
   LOG_CONSOLE (PLID, "%s: plugin attaching", Plugin_info.name);
   LOG_MESSAGE (PLID, "%s: plugin attaching", Plugin_info.name);
   REG_SVR_COMMAND ("HPB_Bot", HPB_Bot_ServerCommand);
   return (TRUE);
}


C_DLLEXPORT int Meta_Detach (PLUG_LOADTIME now, PL_UNLOAD_REASON reason)
{
   if ((now > Plugin_info.unloadable) && (reason != PNL_CMD_FORCED))
   {
      LOG_CONSOLE (PLID, "%s: plugin NOT detaching (can't unload plugin right now)", Plugin_info.name);
      LOG_ERROR (PLID, "%s: plugin NOT detaching (can't unload plugin right now)", Plugin_info.name);
      return (FALSE);
   }
   return (TRUE);
}

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
   CVAR_REGISTER(&bot_debug_draw_objectives); // Register new CVar
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
         pent_info_tfdetect = NULL;
         pent_info_ctfdetect = NULL;
         pent_info_frontline = NULL;
         pent_item_tfgoal = NULL;
         pent_info_tfgoal = NULL;
         for (index=0; index < 4; index++)
         {
            max_team_players[index] = 0;
            team_class_limits[index] = 0;
            team_allies[index] = 0;
         }
         max_teams = 0;
         num_flags = 0;
         for (index=0; index < MAX_FLAGS; index++)
         {
            flags[index].edict = NULL;
            flags[index].team_no = 0;
         }
         num_backpacks = 0;
         for (index=0; index < MAX_BACKPACKS; index++)
         {
            backpacks[index].edict = NULL;
            backpacks[index].armor = 0;
            backpacks[index].health = 0;
            backpacks[index].ammo = 0;
            backpacks[index].team = 0;
         }
         PRECACHE_SOUND("weapons/xbow_hit1.wav");
         PRECACHE_SOUND("weapons/mine_activate.wav");
         PRECACHE_SOUND("common/wpn_hudoff.wav");
         PRECACHE_SOUND("common/wpn_hudon.wav");
         PRECACHE_SOUND("common/wpn_moveselect.wav");
         PRECACHE_SOUND("common/wpn_denyselect.wav");
         PRECACHE_SOUND("player/sprayer.wav");
         m_spriteTexture = PRECACHE_MODEL( "sprites/lgtning.spr");
         g_GameRules = TRUE;
         is_team_play = FALSE;
         memset(team_names, 0, sizeof(team_names));
         num_teams = 0;
         checked_teamplay = FALSE;
         bot_cfg_pause_time = 0.0;
         respawn_time = 0.0;
         spawn_time_reset = FALSE;
         prev_num_bots = num_bots;
         num_bots = 0;
         bot_check_time = gpGlobals->time + 60.0;
      }
      if ((mod_id == HOLYWARS_DLL) && (jumppad_off) &&
          (strcmp(pClassname, "trigger_jumppad") == 0))
      {
         RETURN_META_VALUE (MRES_SUPERCEDE, -1);
      }
   }
   RETURN_META_VALUE (MRES_IGNORED, 0);
}

void KeyValue( edict_t *pentKeyvalue, KeyValueData *pkvd )
{
   static edict_t *temp_pent;
   static int flag_index;
   static int backpack_index;
   if (mod_id == TFC_DLL)
   {
      if (pentKeyvalue == pent_info_tfdetect){/*...omitted for brevity...*/}
      else if (pent_info_tfdetect == NULL){/*...omitted for brevity...*/}
      if (pentKeyvalue == pent_item_tfgoal){/*...omitted for brevity...*/}
      else if (pent_item_tfgoal == NULL){/*...omitted for brevity...*/}
      else{pent_item_tfgoal = NULL;}
      if (pentKeyvalue != pent_info_tfgoal){pent_info_tfgoal = NULL;}
      if (pentKeyvalue == pent_info_tfgoal){/*...omitted for brevity...*/}
      else if (pent_info_tfgoal == NULL){/*...omitted for brevity...*/}
      if ((strcmp(pkvd->szKeyName, "classname") == 0) &&
          ((strcmp(pkvd->szValue, "info_player_teamspawn") == 0) ||
           (strcmp(pkvd->szValue, "i_p_t") == 0)))
      {temp_pent = pentKeyvalue;}
      else if (pentKeyvalue == temp_pent)
      {if (strcmp(pkvd->szKeyName, "team_no") == 0){int value = atoi(pkvd->szValue); if (value > max_teams) max_teams = value;}}
   }
   else if (mod_id == GEARBOX_DLL)
   {if (pent_info_ctfdetect == NULL){if ((strcmp(pkvd->szKeyName, "classname") == 0) && (strcmp(pkvd->szValue, "info_ctfdetect") == 0)){pent_info_ctfdetect = pentKeyvalue;}}}
   RETURN_META (MRES_IGNORED);
}

BOOL ClientConnect( edict_t *pEntity, const char *pszName, const char *pszAddress, char szRejectReason[ 128 ]  )
{ /*...omitted for brevity...*/}
void ClientDisconnect( edict_t *pEntity ){/*...omitted for brevity...*/}
void ClientPutInServer( edict_t *pEntity ){/*...omitted for brevity...*/}

// Externs for objective discovery debugging
extern std::vector<CandidateObjective_t> g_candidate_objectives;
extern std::list<GameEvent_t> g_game_event_log;
const char* ObjectiveTypeToString(ObjectiveType_e obj_type);
const char* GameEventTypeToString(GameEventType_e event_type);
const char* ActivationMethodToString(ActivationMethod_e act_meth);


void ClientCommand( edict_t *pEntity )
{
   const char *pcmd = CMD_ARGV (0);
   const char *arg1 = CMD_ARGV (1);
   const char *arg2 = CMD_ARGV (2);
   const char *arg3 = CMD_ARGV (3);
   const char *arg4 = CMD_ARGV (4);
   const char *arg5 = CMD_ARGV (5);

   if ((gpGlobals->deathmatch) && (!IsDedicatedServer) &&
       (pEntity == listenserver_edict))
   {
      char msg[80]; // For general messages
      char big_msg[512]; // For list_candidates and list_events

      if (FStrEq(pcmd, "addbot")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "observer")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "botskill")){/*...omitted for brevity...*/}
      // ... (other HPB_bot commands omitted for brevity) ...
      else if (FStrEq(pcmd, "waypoint")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "autowaypoint")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "pathwaypoint")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "menuselect") && (g_menu_state != MENU_NONE)){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "search")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "jumppad")){/*...omitted for brevity...*/}
      else if (FStrEq(pcmd, "bot_list_candidates"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
               ClientPrint(pEntity, HUD_PRINTCONSOLE, "This command is for listen server admin only.\n");
               RETURN_META(MRES_SUPERCEDE);
          }
          sprintf(big_msg, "--- Candidate Objectives (Count: %zu) ---\n", g_candidate_objectives.size());
          ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);

          for (size_t i = 0; i < g_candidate_objectives.size(); ++i) {
              const CandidateObjective_t& cand = g_candidate_objectives[i];
              const char* type_str = ObjectiveTypeToString(cand.learned_objective_type);
              const char* act_meth_str = ActivationMethodToString(cand.learned_activation_method);

              sprintf(big_msg, "ID:%d|T:%s|Own:%d|Act:%s|C:%.2f|P:%d|N:%d|L:(%.0f,%.0f,%.0f)|Cls:%s|Tgt:%s|Team:%d@%.1f\n",
                      cand.unique_id, type_str, cand.current_owner_team, act_meth_str,
                      cand.confidence_score, cand.positive_event_correlations, cand.negative_event_correlations,
                      cand.location.x, cand.location.y, cand.location.z,
                      cand.entity_classname[0] ? cand.entity_classname : "-",
                      cand.entity_targetname[0] ? cand.entity_targetname : "-",
                      cand.last_interacting_team, cand.last_interaction_time);
              ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End of List ---\n");
          RETURN_META(MRES_SUPERCEDE);
      }
      else if (FStrEq(pcmd, "bot_list_events"))
      {
          if (pEntity != listenserver_edict && !IS_DEDICATED_SERVER()) {
               ClientPrint(pEntity, HUD_PRINTCONSOLE, "This command is for listen server admin only.\n");
               RETURN_META(MRES_SUPERCEDE);
          }
          int count_to_show = 10;
          const char *arg_count_str = CMD_ARGV(1);
          if (arg_count_str && arg_count_str[0] != '\0') {
              count_to_show = atoi(arg_count_str);
              if (count_to_show <= 0 || count_to_show > 100) count_to_show = 10;
          }
          sprintf(big_msg, "--- Last %d Game Events (Total: %zu) ---\n", count_to_show, g_game_event_log.size());
          ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);

          int shown_count = 0;
          for (auto it = g_game_event_log.rbegin(); it != g_game_event_log.rend() && shown_count < count_to_show; ++it, ++shown_count) {
              const GameEvent_t& evt = *it;
              const char* event_type_str = GameEventTypeToString(evt.type);
              sprintf(big_msg, "T:%.1f|Type:%s|T1:%d|T2:%d|CandID:%d|PlyrEdict:%d|VF:%.2f|VI:%d|Msg:'%s'\n",
                      evt.timestamp, event_type_str, evt.primarily_involved_team_id, evt.secondary_involved_team_id,
                      evt.candidate_objective_id, evt.involved_player_user_id, evt.event_value_float,
                      evt.event_value_int, evt.event_message_text[0] ? evt.event_message_text : "-");
              ClientPrint(pEntity, HUD_PRINTCONSOLE, big_msg);
          }
          ClientPrint(pEntity, HUD_PRINTCONSOLE, "--- End of Events List ---\n");
          RETURN_META(MRES_SUPERCEDE);
      }
#if _DEBUG
      else if (FStrEq(pcmd, "botstop")){bot_stop = 1; RETURN_META (MRES_SUPERCEDE);}
      else if (FStrEq(pcmd, "botstart")){bot_stop = 0; RETURN_META (MRES_SUPERCEDE);}
#endif
   }
   RETURN_META (MRES_IGNORED);
}

void StartFrame( void )
{
   if (gpGlobals->deathmatch)
   {
      edict_t *pPlayer;
      static int i, index, player_index, bot_index;
      static float previous_time = -1.0;
      static float next_tactical_update_time = 0.0f;
      static float next_obj_discovery_update_time = 0.0f;
      static float next_obj_analysis_time = 0.0f;
      char msg[256]; // Used locally, no conflict with big_msg in ClientCommand
      int count;

      if ((gpGlobals->time + 0.1) < previous_time)
      {
         if (previous_time > 0.0) {
            SaveBotMemory(BOT_MEMORY_FILENAME);
         }
         LoadBotMemory(BOT_MEMORY_FILENAME);
         TacticalAI_LevelInit();
         ObjectiveDiscovery_LevelInit();
         // ... (rest of original new map logic from HPB_bot StartFrame, omitted for brevity but included in my constructed version)
         char filename_cfg[256]; char mapname_cfg[64]; // Avoid conflict
         strcpy(mapname_cfg, STRING(gpGlobals->mapname)); strcat(mapname_cfg, "_HPB_bot.cfg");
         UTIL_BuildFileName(filename_cfg, "maps", mapname_cfg);
         if ((bot_cfg_fp = fopen(filename_cfg, "r")) != NULL){/*...*/} else {/*...*/}
         bot_check_time = gpGlobals->time + 60.0;
      }

      if (!IsDedicatedServer) {/*...omitted for brevity...*/}

      count = 0;
      if (bot_stop == 0)
      {
         for (bot_index = 0; bot_index < gpGlobals->maxClients; bot_index++)
         {
            if ((bots[bot_index].is_used) && (bots[bot_index].respawn_state == RESPAWN_IDLE))
            { BotThink(&bots[bot_index]); count++;}
         }
      }
      if (count > num_bots) num_bots = count;

      if (gpGlobals->time >= next_tactical_update_time) {
          TacticalAI_UpdatePeriodicState();
          next_tactical_update_time = gpGlobals->time + 1.0f;
      }
      if (gpGlobals->time >= next_obj_discovery_update_time) {
          ObjectiveDiscovery_UpdatePeriodic();
          next_obj_discovery_update_time = gpGlobals->time + 2.0f;
      }
      if (gpGlobals->time >= next_obj_analysis_time) {
          ObjectiveDiscovery_AnalyzeEvents();
          next_obj_analysis_time = gpGlobals->time + 7.5f;
      }
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
