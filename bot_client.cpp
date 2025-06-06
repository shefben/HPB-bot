//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// bot_client.cpp
//

#ifndef _WIN32
#include <string.h>
#endif

#include <extdll.h>
#include <dllapi.h>
#include <h_export.h>
#include <meta_api.h>

#include "bot.h"
#include "bot_func.h"
#include "bot_client.h"
#include "bot_weapons.h"
#include "bot_objective_discovery.h" // For AddGameEvent
#include "bot_tactical_ai.h"       // For TacticalAI_On* event handlers
#include "bot_nlp_chat.h"          // For NLP chat generation
#include <vector>                  // For std::vector (used by NLP chat)
#include <string>                  // For std::string (used by NLP chat)


// types of damage to ignore...
#define IGNORE_DAMAGE (DMG_CRUSH | DMG_BURN | DMG_FREEZE | DMG_FALL | \
                       DMG_SHOCK | DMG_DROWN | DMG_NERVEGAS | DMG_RADIATION | \
                       DMG_DROWNRECOVER | DMG_ACID | DMG_SLOWBURN | \
                       DMG_SLOWFREEZE | 0xFF000000)

extern int mod_id;
extern bot_t bots[32];
extern int num_logos;
extern edict_t *holywars_saint;
extern int halo_status;
extern int holywars_gamemode;

extern int bot_taunt_count;
extern int recent_bot_taunt[];
extern bot_chat_t bot_taunt[MAX_BOT_CHAT];
extern int bot_whine_count; // Added for whine fallback
extern int recent_bot_whine[]; // Added for whine fallback
extern bot_chat_t bot_whine[MAX_BOT_CHAT]; // Added for whine fallback


// CVars
extern cvar_t bot_chat_use_nlp_model; // Defined in dll.cpp

// NLP Model
extern NgramModel_t g_chat_ngram_model; // Defined in bot_nlp_chat.cpp
extern int g_ngram_model_N_value;    // Defined in bot_nlp_chat.cpp


bot_weapon_t weapon_defs[MAX_WEAPONS]; // array of weapon definitions


// This message is sent when the TFC VGUI menu is displayed.
void BotClient_TFC_VGUI(void *p, int bot_index)
{
   static int state = 0;   // current state machine state

   if (state == 0)
   {
      if ((*(int *)p) == 2)  // is it a team select menu?

         bots[bot_index].start_action = MSG_TFC_TEAM_SELECT;

      else if ((*(int *)p) == 3)  // is is a class selection menu?

         bots[bot_index].start_action = MSG_TFC_CLASS_SELECT;
   }

   state++;

   if (state == 1)
      state = 0;
}


// This message is sent when the Counter-Strike VGUI menu is displayed.
void BotClient_CS_VGUI(void *p, int bot_index)
{
   static int state = 0;   // current state machine state

   if (state == 0)
   {
      if ((*(int *)p) == 2)  // is it a team select menu?

         bots[bot_index].start_action = MSG_CS_TEAM_SELECT;

      else if ((*(int *)p) == 26)  // is is a terrorist model select menu?

         bots[bot_index].start_action = MSG_CS_T_SELECT;

      else if ((*(int *)p) == 27)  // is is a counter-terrorist model select menu?

         bots[bot_index].start_action = MSG_CS_CT_SELECT;
   }

   state++;

   if (state == 5)  // ignore other fields in VGUI message
      state = 0;
}


// This message is sent when a menu is being displayed in Counter-Strike.
void BotClient_CS_ShowMenu(void *p, int bot_index)
{
   static int state = 0;   // current state machine state

   if (state < 3)
   {
      state++;  // ignore first 3 fields of message
      return;
   }

   if (strcmp((char *)p, "#Team_Select") == 0)  // team select menu?
   {
      bots[bot_index].start_action = MSG_CS_TEAM_SELECT;
   }
   else if (strcmp((char *)p, "#Terrorist_Select") == 0)  // T model select?
   {
      bots[bot_index].start_action = MSG_CS_T_SELECT;
   }
   else if (strcmp((char *)p, "#CT_Select") == 0)  // CT model select menu?
   {
      bots[bot_index].start_action = MSG_CS_CT_SELECT;
   }

   state = 0;  // reset state machine
}


// This message is sent when the OpFor VGUI menu is displayed.
void BotClient_Gearbox_VGUI(void *p, int bot_index)
{
   static int state = 0;   // current state machine state

   if (state == 0)
   {
      if ((*(int *)p) == 2)  // is it a team select menu?

         bots[bot_index].start_action = MSG_OPFOR_TEAM_SELECT;

      else if ((*(int *)p) == 3)  // is is a class selection menu?

         bots[bot_index].start_action = MSG_OPFOR_CLASS_SELECT;
   }

   state++;

   if (state == 1)
      state = 0;
}


// This message is sent when the FrontLineForce VGUI menu is displayed.
void BotClient_FLF_VGUI(void *p, int bot_index)
{
   static int state = 0;   // current state machine state

   if (p == NULL)  // handle pfnMessageEnd case
   {
      state = 0;
      return;
   }

   if (state == 0)
   {
      if ((*(int *)p) == 2)  // is it a team select menu?
         bots[bot_index].start_action = MSG_FLF_TEAM_SELECT;
      else if ((*(int *)p) == 3)  // is it a class selection menu?
         bots[bot_index].start_action = MSG_FLF_CLASS_SELECT;
      else if ((*(int *)p) == 70)  // is it a weapon selection menu?
         bots[bot_index].start_action = MSG_FLF_WEAPON_SELECT;
      else if ((*(int *)p) == 72)  // is it a submachine gun selection menu?
         bots[bot_index].start_action = MSG_FLF_SUBMACHINE_SELECT;
      else if ((*(int *)p) == 73)  // is it a shotgun selection menu?
         bots[bot_index].start_action = MSG_FLF_SHOTGUN_SELECT;
      else if ((*(int *)p) == 75)  // is it a rifle selection menu?
         bots[bot_index].start_action = MSG_FLF_RIFLE_SELECT;
      else if ((*(int *)p) == 76)  // is it a pistol selection menu?
         bots[bot_index].start_action = MSG_FLF_PISTOL_SELECT;
      else if ((*(int *)p) == 78)  // is it a heavyweapons selection menu?
         bots[bot_index].start_action = MSG_FLF_HEAVYWEAPONS_SELECT;
   }

   state++;
}


// This message is sent when a client joins the game.  All of the weapons
// are sent with the weapon ID and information about what ammo is used.
void BotClient_Valve_WeaponList(void *p, int bot_index)
{
   static int state = 0;   // current state machine state
   static bot_weapon_t bot_weapon;

   if (state == 0)
   {
      state++;
      strcpy(bot_weapon.szClassname, (char *)p);
   }
   else if (state == 1)
   {
      state++;
      bot_weapon.iAmmo1 = *(int *)p;  // ammo index 1
   }
   else if (state == 2)
   {
      state++;
      bot_weapon.iAmmo1Max = *(int *)p;  // max ammo1
   }
   else if (state == 3)
   {
      state++;
      bot_weapon.iAmmo2 = *(int *)p;  // ammo index 2
   }
   else if (state == 4)
   {
      state++;
      bot_weapon.iAmmo2Max = *(int *)p;  // max ammo2
   }
   else if (state == 5)
   {
      state++;
      bot_weapon.iSlot = *(int *)p;  // slot for this weapon
   }
   else if (state == 6)
   {
      state++;
      bot_weapon.iPosition = *(int *)p;  // position in slot
   }
   else if (state == 7)
   {
      state++;
      bot_weapon.iId = *(int *)p;  // weapon ID
   }
   else if (state == 8)
   {
      state = 0;

      bot_weapon.iFlags = *(int *)p;  // flags for weapon (WTF???)

      // store away this weapon with it's ammo information...
      if (mod_id == DMC_DLL)
         weapon_defs[bot_weapon.iSlot] = bot_weapon;
      else
         weapon_defs[bot_weapon.iId] = bot_weapon;
   }
}

void BotClient_TFC_WeaponList(void *p, int bot_index) { BotClient_Valve_WeaponList(p, bot_index); }
void BotClient_CS_WeaponList(void *p, int bot_index) { BotClient_Valve_WeaponList(p, bot_index); }
void BotClient_Gearbox_WeaponList(void *p, int bot_index) { BotClient_Valve_WeaponList(p, bot_index); }
void BotClient_FLF_WeaponList(void *p, int bot_index) { BotClient_Valve_WeaponList(p, bot_index); }
void BotClient_DMC_WeaponList(void *p, int bot_index) { BotClient_Valve_WeaponList(p, bot_index); }

void BotClient_Valve_CurrentWeapon(void *p, int bot_index)
{
   static int state = 0;   // current state machine state
   static int iState;
   static int iId;
   static int iClip;

   if (state == 0) { state++; iState = *(int *)p; }
   else if (state == 1) { state++; iId = *(int *)p; }
   else if (state == 2)
   {
      state = 0; iClip = *(int *)p;
      if (mod_id == DMC_DLL) { if ((iState == 1) && (iId <= 128)) bots[bot_index].pEdict->v.weapons |= iId; }
      else { if (iId <= 31) { if (iState == 1) {
         bots[bot_index].current_weapon.iId = iId;
         bots[bot_index].current_weapon.iClip = iClip;
         bots[bot_index].current_weapon.iAmmo1 = bots[bot_index].m_rgAmmo[weapon_defs[iId].iAmmo1];
         bots[bot_index].current_weapon.iAmmo2 = bots[bot_index].m_rgAmmo[weapon_defs[iId].iAmmo2];
      }}}
   }
}
void BotClient_TFC_CurrentWeapon(void *p, int bot_index) { BotClient_Valve_CurrentWeapon(p, bot_index); }
void BotClient_CS_CurrentWeapon(void *p, int bot_index) { BotClient_Valve_CurrentWeapon(p, bot_index); }
void BotClient_Gearbox_CurrentWeapon(void *p, int bot_index) { BotClient_Valve_CurrentWeapon(p, bot_index); }
void BotClient_FLF_CurrentWeapon(void *p, int bot_index) { BotClient_Valve_CurrentWeapon(p, bot_index); }
void BotClient_DMC_CurrentWeapon(void *p, int bot_index) { BotClient_Valve_CurrentWeapon(p, bot_index); }

void BotClient_DMC_QItems (void *p, int bot_index) { bots[bot_index].pEdict->v.weapons |= (*(int *) p & 0x000000FF); }

void BotClient_Valve_AmmoX(void *p, int bot_index)
{
   static int state = 0; static int index; static int ammount; int ammo_index;
   if (state == 0) { state++; index = *(int *)p; }
   else if (state == 1) { state = 0; ammount = *(int *)p;
      bots[bot_index].m_rgAmmo[index] = ammount;
      ammo_index = bots[bot_index].current_weapon.iId;
      bots[bot_index].current_weapon.iAmmo1 = bots[bot_index].m_rgAmmo[weapon_defs[ammo_index].iAmmo1];
      bots[bot_index].current_weapon.iAmmo2 = bots[bot_index].m_rgAmmo[weapon_defs[ammo_index].iAmmo2];
   }
}
void BotClient_TFC_AmmoX(void *p, int bot_index) { BotClient_Valve_AmmoX(p, bot_index); }
void BotClient_CS_AmmoX(void *p, int bot_index) { BotClient_Valve_AmmoX(p, bot_index); }
void BotClient_Gearbox_AmmoX(void *p, int bot_index) { BotClient_Valve_AmmoX(p, bot_index); }
void BotClient_FLF_AmmoX(void *p, int bot_index) { BotClient_Valve_AmmoX(p, bot_index); }
void BotClient_DMC_AmmoX(void *p, int bot_index) { BotClient_Valve_AmmoX(p, bot_index); }

void BotClient_Valve_AmmoPickup(void *p, int bot_index)
{
   static int state = 0; static int index; static int ammount; int ammo_index;
   if (state == 0) { state++; index = *(int *)p; }
   else if (state == 1) { state = 0; ammount = *(int *)p;
      bots[bot_index].m_rgAmmo[index] = ammount;
      ammo_index = bots[bot_index].current_weapon.iId;
      bots[bot_index].current_weapon.iAmmo1 = bots[bot_index].m_rgAmmo[weapon_defs[ammo_index].iAmmo1];
      bots[bot_index].current_weapon.iAmmo2 = bots[bot_index].m_rgAmmo[weapon_defs[ammo_index].iAmmo2];
   }
}
void BotClient_TFC_AmmoPickup(void *p, int bot_index) { BotClient_Valve_AmmoPickup(p, bot_index); }
void BotClient_CS_AmmoPickup(void *p, int bot_index) { BotClient_Valve_AmmoPickup(p, bot_index); }
void BotClient_Gearbox_AmmoPickup(void *p, int bot_index) { BotClient_Valve_AmmoPickup(p, bot_index); }
void BotClient_FLF_AmmoPickup(void *p, int bot_index) { BotClient_Valve_AmmoPickup(p, bot_index); }
void BotClient_DMC_AmmoPickup(void *p, int bot_index) { BotClient_Valve_AmmoPickup(p, bot_index); }

void BotClient_TFC_SecAmmoVal(void *p, int bot_index)
{
   static int state = 0; static int type; static int ammount;
   if (state == 0) { state++; type = *(int *)p; }
   else if (state == 1) { state = 0; ammount = *(int *)p;
      if (type == 0) bots[bot_index].gren1 = ammount; else if (type == 1) bots[bot_index].gren2 = ammount;
   }
}

void BotClient_Valve_WeaponPickup(void *p, int bot_index) {}
void BotClient_TFC_WeaponPickup(void *p, int bot_index) {}
void BotClient_CS_WeaponPickup(void *p, int bot_index) {}
void BotClient_Gearbox_WeaponPickup(void *p, int bot_index) {}
void BotClient_FLF_WeaponPickup(void *p, int bot_index) {}
void BotClient_DMC_WeaponPickup(void *p, int bot_index) {}

void BotClient_Valve_ItemPickup(void *p, int bot_index) {}
void BotClient_TFC_ItemPickup(void *p, int bot_index) {}
void BotClient_CS_ItemPickup(void *p, int bot_index) {}
void BotClient_Gearbox_ItemPickup(void *p, int bot_index) {}
void BotClient_FLF_ItemPickup(void *p, int bot_index) {}
void BotClient_DMC_ItemPickup(void *p, int bot_index) {}

void BotClient_Valve_Health(void *p, int bot_index) {}
void BotClient_TFC_Health(void *p, int bot_index) {}
void BotClient_CS_Health(void *p, int bot_index) {}
void BotClient_Gearbox_Health(void *p, int bot_index) {}
void BotClient_FLF_Health(void *p, int bot_index) {}
void BotClient_DMC_Health(void *p, int bot_index) {}

void BotClient_Valve_Battery(void *p, int bot_index) {}
void BotClient_TFC_Battery(void *p, int bot_index) {}
void BotClient_CS_Battery(void *p, int bot_index) {}
void BotClient_Gearbox_Battery(void *p, int bot_index) {}
void BotClient_FLF_Battery(void *p, int bot_index) {}
void BotClient_DMC_Battery(void *p, int bot_index) {}

void BotClient_Valve_Damage(void *p, int bot_index)
{
   static int state = 0; static int damage_armor; static int damage_taken;
   static int damage_bits; static Vector damage_origin;
   if (state == 0) { state++; damage_armor = *(int *)p; }
   else if (state == 1) { state++; damage_taken = *(int *)p; }
   else if (state == 2) { state++; damage_bits = *(int *)p; }
   else if (state == 3) { state++; damage_origin.x = *(float *)p; }
   else if (state == 4) { state++; damage_origin.y = *(float *)p; }
   else if (state == 5) { state = 0; damage_origin.z = *(float *)p;
      if ((damage_armor > 0) || (damage_taken > 0)) {
         if (damage_bits & IGNORE_DAMAGE) return;
         if (bots[bot_index].pBotEnemy == NULL) {
            Vector v_enemy = damage_origin - bots[bot_index].pEdict->v.origin;
            Vector bot_angles = UTIL_VecToAngles( v_enemy );
            bots[bot_index].pEdict->v.ideal_yaw = bot_angles.y;
            BotFixIdealYaw(bots[bot_index].pEdict);
            bots[bot_index].b_use_health_station = FALSE;
            bots[bot_index].b_use_HEV_station = FALSE;
            bots[bot_index].b_use_capture = FALSE;
         }
      }
   }
}
void BotClient_TFC_Damage(void *p, int bot_index) { BotClient_Valve_Damage(p, bot_index); }
void BotClient_CS_Damage(void *p, int bot_index) { BotClient_Valve_Damage(p, bot_index); }
void BotClient_Gearbox_Damage(void *p, int bot_index) { BotClient_Valve_Damage(p, bot_index); }
void BotClient_FLF_Damage(void *p, int bot_index) { BotClient_Valve_Damage(p, bot_index); }
void BotClient_DMC_Damage(void *p, int bot_index) { BotClient_Valve_Damage(p, bot_index); }

void BotClient_CS_Money(void *p, int bot_index)
{
   static int state = 0;
   if (state == 0) { state++; bots[bot_index].bot_money = *(int *)p; }
   else { state = 0; }
}

void BotClient_Valve_DeathMsg(void *p, int bot_index) // bot_index is not used here.
{
   static int state = 0;
   static int killer_idx_parsed;
   static int victim_idx_parsed;
   static edict_t *killer_edict;
   static edict_t *victim_edict;
   static int index;
   char weapon_name_parsed[64];

   char chat_text[81];
   char chat_name[64];
   char temp_name[64];
   const char *bot_name;
   // Need these for fallback logic if NLP fails or is disabled
   int whine_index;
   bool used;
   int i, recent_count;


   if (state == 0) { state++; killer_idx_parsed = *(int *)p; }
   else if (state == 1) { state++; victim_idx_parsed = *(int *)p; }
   else if (state == 2)
   {
      state = 0;
      strncpy(weapon_name_parsed, (char*)p, sizeof(weapon_name_parsed)-1);
      weapon_name_parsed[sizeof(weapon_name_parsed)-1] = '\0';

      killer_edict = INDEXENT(killer_idx_parsed);
      victim_edict = INDEXENT(victim_idx_parsed);

      int killer_team_id = (killer_edict && killer_edict->v.team) ? (int)killer_edict->v.team : -1;
      int victim_team_id = (victim_edict && victim_edict->v.team) ? (int)victim_edict->v.team : -1;
      if (victim_edict) {
         AddGameEvent(EVENT_PLAYER_DIED_NEAR_CANDIDATE, gpGlobals->time,
                      victim_team_id, killer_team_id, -1,
                      victim_idx_parsed, 0.0f, killer_idx_parsed, weapon_name_parsed);
      }

      index = UTIL_GetBotIndex(killer_edict); // index is now killer bot's array index

      if (index != -1) // if killer is a bot
      {
         if (killer_idx_parsed != victim_idx_parsed)
         {
            if ((RANDOM_LONG(1, 100) <= bots[index].logo_percent) && (num_logos))
            {
               bots[index].b_spray_logo = TRUE;
               bots[index].f_spray_logo_time = gpGlobals->time;
            }
         }

         if (victim_edict != NULL)
         {
            if ((bot_taunt_count > 0) &&
                (RANDOM_LONG(1,100) <= bots[index].taunt_percent))
            {
               bots[index].b_bot_say = TRUE;
               bots[index].f_bot_say = gpGlobals->time + 5.0 + RANDOM_FLOAT(0.0, 5.0);

               bool nlp_taunt_generated = false;
               if (bot_chat_use_nlp_model.value > 0.0f && !g_chat_ngram_model.empty() && g_ngram_model_N_value > 0) {
                   std::string nlp_message = NLP_GenerateChatMessage(g_chat_ngram_model, g_ngram_model_N_value, 15);
                   if (!nlp_message.empty()) {
                       strncpy(bots[index].bot_say_msg, nlp_message.c_str(), sizeof(bots[index].bot_say_msg) - 1);
                       bots[index].bot_say_msg[sizeof(bots[index].bot_say_msg) - 1] = '\0';
                       nlp_taunt_generated = true;
                   }
               }

               if (!nlp_taunt_generated) { // Fallback to original logic
                   recent_count = 0;
                   int taunt_index; // Declare taunt_index here
                   while (recent_count < 5)
                   {
                      taunt_index = RANDOM_LONG(0, bot_taunt_count-1);
                      used = FALSE;
                      for (i=0; i < 5; i++) { if (recent_bot_taunt[i] == taunt_index) used = TRUE; }
                      if (used) recent_count++; else break;
                   }
                   for (i=4; i > 0; i--) recent_bot_taunt[i] = recent_bot_taunt[i-1];
                   recent_bot_taunt[0] = taunt_index;
                   if (bot_taunt[taunt_index].can_modify) BotChatText(bot_taunt[taunt_index].text, chat_text);
                   else strcpy(chat_text, bot_taunt[taunt_index].text);
                   if (victim_edict->v.netname) {
                      strncpy(temp_name, STRING(victim_edict->v.netname), 31); temp_name[31] = 0;
                      BotChatName(temp_name, chat_name);
                   } else strcpy(chat_name, "NULL");
                   bot_name = STRING(bots[index].pEdict->v.netname);
                   BotChatFillInName(bots[index].bot_say_msg, chat_text, chat_name, bot_name);
               }
            }
         }
      }

      index = UTIL_GetBotIndex(victim_edict); // index is now victim bot's array index

      if (index != -1) // if victim is a bot
      {
         if ((killer_idx_parsed == 0) || (killer_idx_parsed == victim_idx_parsed)) {
            bots[index].killer_edict = NULL;
         } else {
            bots[index].killer_edict = INDEXENT(killer_idx_parsed);
         }

         // This is where the original whine logic was, now moved inside the if block below
         if ((bots[index].killer_edict != NULL || killer_idx_parsed == 0) && // Check if killed by world or other player
              (bot_whine_count > 0) &&
             ((bots[index].f_bot_spawn_time + 15.0) <= gpGlobals->time) &&
             (RANDOM_LONG(1,100) <= bots[index].whine_percent))
         {
            bots[index].b_bot_say = TRUE;
            bots[index].f_bot_say = gpGlobals->time + 5.0 + RANDOM_FLOAT(0.0, 5.0);

            bool nlp_message_generated_whine = false;
            if (bot_chat_use_nlp_model.value > 0.0f && !g_chat_ngram_model.empty() && g_ngram_model_N_value > 0) {
                std::string nlp_message = NLP_GenerateChatMessage(g_chat_ngram_model, g_ngram_model_N_value, 15);
                if (!nlp_message.empty()) {
                    strncpy(bots[index].bot_say_msg, nlp_message.c_str(), sizeof(bots[index].bot_say_msg) - 1);
                    bots[index].bot_say_msg[sizeof(bots[index].bot_say_msg) - 1] = '\0';
                    nlp_message_generated_whine = true;
                }
            }

            if (!nlp_message_generated_whine) { // Fallback to original logic
                recent_count = 0;
                while (recent_count < 5)
                {
                   whine_index = RANDOM_LONG(0, bot_whine_count-1);
                   used = FALSE;
                   for (i=0; i < 5; i++) { if (recent_bot_whine[i] == whine_index) used = TRUE; }
                   if (used) recent_count++; else break;
                }
                for (i=4; i > 0; i--) recent_bot_whine[i] = recent_bot_whine[i-1];
                recent_bot_whine[0] = whine_index;
                if (bot_whine[whine_index].can_modify) BotChatText(bot_whine[whine_index].text, chat_text);
                else strcpy(chat_text, bot_whine[whine_index].text);
                if (bots[index].killer_edict && bots[index].killer_edict->v.netname) {
                   strncpy(temp_name, STRING(bots[index].killer_edict->v.netname), 31);
                   temp_name[31] = 0;
                   BotChatName(temp_name, chat_name);
                } else strcpy(chat_name, "NULL"); // For world kills
                bot_name = STRING(bots[index].pEdict->v.netname);
                BotChatFillInName(bots[index].bot_say_msg, chat_text, chat_name, bot_name);
            }
         }
      }
   }
}

void BotClient_TFC_DeathMsg(void *p, int bot_index) { BotClient_Valve_DeathMsg(p, bot_index); }
void BotClient_CS_DeathMsg(void *p, int bot_index) { BotClient_Valve_DeathMsg(p, bot_index); }
void BotClient_Gearbox_DeathMsg(void *p, int bot_index) { BotClient_Valve_DeathMsg(p, bot_index); }
void BotClient_FLF_DeathMsg(void *p, int bot_index) { BotClient_Valve_DeathMsg(p, bot_index); }
void BotClient_DMC_DeathMsg(void *p, int bot_index) { BotClient_Valve_DeathMsg(p, bot_index); }

// This message gets sent when a text message is displayed
void BotClient_TFC_TextMsg(void *p, int bot_index)
{
   static int state = 0;   // current state machine state
   static int msg_dest = 0;

   if (p == NULL)  // handle pfnMessageEnd case
   {
      state = 0;
      return;
   }

   if (state == 0)
   {
      state++;
      msg_dest = *(int *)p;  // HUD_PRINTCENTER, etc.
   }
   else if (state == 1)
   {
      const char* message_text = (char*)p;
      bool event_logged_for_discovery = false;
      bool tactical_event_handled = false;

      if (strstr(message_text, "Team has scored!")) {
      }
      else if (strstr(message_text, "Round Over") && strstr(message_text, "Team Wins")) {
      }


      if (strcmp(message_text, "#Sentry_finish") == 0)
      {
         bots[bot_index].sentrygun_level = 1;
         event_logged_for_discovery = true;
         tactical_event_handled = true;
      }
      else if (strcmp(message_text, "#Sentry_upgrade") == 0)
      {
         bots[bot_index].sentrygun_level += 1;
         bots[bot_index].pBotEnemy = NULL;
         bots[bot_index].enemy_attack_count = 0;
         event_logged_for_discovery = true;
         tactical_event_handled = true;
      }
      else if (strcmp(message_text, "#Sentry_destroyed") == 0)
      {
         bots[bot_index].sentrygun_waypoint = -1;
         bots[bot_index].sentrygun_level = 0;
         event_logged_for_discovery = true;
         tactical_event_handled = true;
      }
      else if (strcmp(message_text, "#Dispenser_finish") == 0)
      {
         bots[bot_index].dispenser_built = 1;
         event_logged_for_discovery = true;
         tactical_event_handled = true;
      }
      else if (strcmp(message_text, "#Dispenser_destroyed") == 0)
      {
         bots[bot_index].dispenser_waypoint = -1;
         bots[bot_index].dispenser_built = 0;
         event_logged_for_discovery = true;
         tactical_event_handled = true;
      }
      else if (strstr(message_text, "picked up the") && strstr(message_text, "Flag!")) {
      }
      else if (strstr(message_text, "dropped the") && strstr(message_text, "Flag!")) {
      }
      else if (strstr(message_text, "returned the") && strstr(message_text, "Flag!")) {
      }
      else if (strstr(message_text, "captured the") && strstr(message_text, "Flag!")) {
      }

      if (!tactical_event_handled && !event_logged_for_discovery && msg_dest == HUD_PRINTCENTER) {
      }
   }
}

void BotClient_FLF_TextMsg(void *p, int bot_index)
{
   static int state = 0;
   static int msg_dest = 0;

   if (p == NULL) { state = 0; return; }
   if (state == 0) { state++; msg_dest = *(int *)p; }
   else if (state == 1) {
      if (strcmp((char *)p, "You are Attacking\n") == 0) bots[bot_index].defender = 0;
      else if (strcmp((char *)p, "You are Defending\n") == 0) bots[bot_index].defender = 1;
   }
}

void BotClient_FLF_WarmUp(void *p, int bot_index) { bots[bot_index].warmup = *(int *)p; }

void BotClient_FLF_WarmUpAll(void *p, int bot_index)
{
   for (int i=0; i < 32; i++) { if (bots[i].is_used) bots[i].warmup = *(int *)p; }
}

void BotClient_FLF_WinMessage(void *p, int bot_index)
{
   for (int i=0; i < 32; i++) { if (bots[i].is_used) bots[i].round_end = 1; }
}

void BotClient_FLF_HideWeapon(void *p, int bot_index)
{
   int hide = *(int *)p;
   if ((hide == 0) && (bots[bot_index].b_use_capture)) {
      bots[bot_index].b_use_capture = FALSE;
      bots[bot_index].f_use_capture_time = 0.0;
   }
   if ((hide) && (bots[bot_index].b_use_capture)) bots[bot_index].f_use_capture_time = gpGlobals->time + 30;
}

void BotClient_Valve_ScreenFade(void *p, int bot_index)
{
   static int state = 0; static int duration; static int hold_time; static int fade_flags; int length;
   if (state == 0) { state++; duration = *(int *)p; }
   else if (state == 1) { state++; hold_time = *(int *)p; }
   else if (state == 2) { state++; fade_flags = *(int *)p; }
   else if (state == 6) { state = 0;
      length = (duration + hold_time) / 4096;
      bots[bot_index].blinded_time = gpGlobals->time + length - 2.0;
   }
   else { state++; }
}
void BotClient_TFC_ScreenFade(void *p, int bot_index) { BotClient_Valve_ScreenFade(p, bot_index); }
void BotClient_CS_ScreenFade(void *p, int bot_index) { BotClient_Valve_ScreenFade(p, bot_index); }
void BotClient_Gearbox_ScreenFade(void *p, int bot_index) { BotClient_Valve_ScreenFade(p, bot_index); }
void BotClient_FLF_ScreenFade(void *p, int bot_index) { BotClient_Valve_ScreenFade(p, bot_index); }

void BotClient_HolyWars_Halo(void *p, int edict_idx) // Changed bot_index to edict_idx for clarity
{
   int type = *(int *)p;
   if (type == 0) { holywars_saint = NULL; halo_status = HW_WAIT_SPAWN; }
   else if (type == 1) { holywars_saint = NULL; }
   else if (type == 2) { halo_status = HW_NEW_SAINT; }
   else if (type == 3) { holywars_saint = INDEXENT(edict_idx); halo_status = HW_NEW_SAINT; } // Use INDEXENT
}

void BotClient_HolyWars_GameMode(void *p, int bot_index) { holywars_gamemode = *(int *)p; }

void BotClient_HolyWars_HudText(void *p, int bot_index)
{
   const char* message_text = (const char*)p;
   if (strncmp(message_text, "Voting for", 10) == 0) {
      bots[bot_index].vote_in_progress = TRUE;
      bots[bot_index].f_vote_time = gpGlobals->time + RANDOM_LONG(2.0, 5.0);
   }
}

void BotClient_CS_HLTV(void *p, int bot_index)
{
   static int state = 0; static int players; int index_loop; // Renamed index to avoid conflict
   if (state == 0) players = *(int *) p;
   else if (state == 1) {
      if ((players == 0) && (*(int *) p == 0)) {
         for (index_loop = 0; index_loop < 32; index_loop++) {
            if (bots[index_loop].is_used) BotSpawnInit (&bots[index_loop]);
         }
      }
   }
}

[end of bot_client.cpp]
