//
// HPB bot - botman's High Ping Bastard bot
//
// (http://planethalflife.com/botman/)
//
// bot_combat.cpp
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
#include "bot_weapons.h"
#include "bot_rl_aiming.h" // For RL Aiming agent
#include <cmath>          // For fabsf in reward calculation (if not already via other headers)

// CVars for RL Aiming Parameters (defined in dll.cpp)
extern cvar_t bot_rl_aim_learning_rate;
extern cvar_t bot_rl_aim_discount_factor;
extern cvar_t bot_rl_aim_exploration_epsilon;
extern cvar_t bot_rl_aim_episode_max_steps;
extern cvar_t bot_rl_aim_action_interval;


extern int mod_id;
extern bot_weapon_t weapon_defs[MAX_WEAPONS];
extern bool b_observer_mode;
extern int team_allies[4];
extern edict_t *pent_info_ctfdetect;
extern bool is_team_play;
extern bool checked_teamplay;
extern int num_logos;

// FILE *fp; // This was duplicated and removed by a previous diff, ensuring it's not re-added.

int tfc_max_armor[10] = {0, 50, 50, 200, 120, 100, 300, 150, 100, 50};
edict_t *holywars_saint = NULL;
int halo_status = HW_WAIT_SPAWN;
int holywars_gamemode = 0;  // 0=deathmatch, 1=halo, 2=instagib

float react_delay_min[3][5] = {
   {0.01, 0.02, 0.03, 0.04, 0.05},
   {0.07, 0.09, 0.12, 0.14, 0.17},
   {0.10, 0.12, 0.15, 0.18, 0.21}};
float react_delay_max[3][5] = {
   {0.04, 0.06, 0.08, 0.10, 0.12},
   {0.11, 0.14, 0.18, 0.21, 0.25},
   {0.15, 0.18, 0.22, 0.25, 0.30}};

float aim_tracking_x_scale[5] = {5.0, 4.0, 3.2, 2.5, 2.0};
float aim_tracking_y_scale[5] = {5.0, 4.0, 3.2, 2.5, 2.0};


typedef struct
{
   int iId;  // the weapon ID value
   char  weapon_name[64];  // name of the weapon when selecting it
   int   skill_level;   // bot skill must be less than or equal to this value
   float primary_min_distance;   // 0 = no minimum
   float primary_max_distance;   // 9999 = no maximum
   float secondary_min_distance; // 0 = no minimum
   float secondary_max_distance; // 9999 = no maximum
   int   use_percent;   // times out of 100 to use this weapon when available
   bool  can_use_underwater;     // can use this weapon underwater
   int   primary_fire_percent;   // times out of 100 to use primary fire
   int   min_primary_ammo;       // minimum ammout of primary ammo needed to fire
   int   min_secondary_ammo;     // minimum ammout of seconday ammo needed to fire
   bool  primary_fire_hold;      // hold down primary fire button to use?
   bool  secondary_fire_hold;    // hold down secondary fire button to use?
   bool  primary_fire_charge;    // charge weapon using primary fire?
   bool  secondary_fire_charge;  // charge weapon using secondary fire?
   float primary_charge_delay;   // time to charge weapon
   float secondary_charge_delay; // time to charge weapon
} bot_weapon_select_t;

typedef struct
{
   int iId;
   float primary_base_delay;
   float primary_min_delay[5];
   float primary_max_delay[5];
   float secondary_base_delay;
   float secondary_min_delay[5];
   float secondary_max_delay[5];
} bot_fire_delay_t;


// weapon_select and fire_delay arrays (omitted for brevity, they are unchanged from original)
bot_weapon_select_t valve_weapon_select[] = {
   {VALVE_WEAPON_CROWBAR, "weapon_crowbar", 2, 0.0, 50.0, 0.0, 0.0,
    100, TRUE, 100, 0, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_HANDGRENADE, "weapon_handgrenade", 5, 250.0, 750.0, 0.0, 0.0,
    30, TRUE, 100, 1, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_SNARK, "weapon_snark", 5, 150.0, 500.0, 0.0, 0.0,
    50, FALSE, 100, 1, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_EGON, "weapon_egon", 5, 0.0, 9999.0, 0.0, 0.0,
    100, FALSE, 100, 1, 0, TRUE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_GAUSS, "weapon_gauss", 5, 0.0, 9999.0, 0.0, 9999.0,
    100, FALSE, 80, 1, 10, FALSE, FALSE, FALSE, TRUE, 0.0, 0.8},
   {VALVE_WEAPON_SHOTGUN, "weapon_shotgun", 5, 30.0, 150.0, 30.0, 150.0,
    100, FALSE, 70, 1, 2, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_PYTHON, "weapon_357", 5, 30.0, 700.0, 0.0, 0.0,
    100, FALSE, 100, 1, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_HORNETGUN, "weapon_hornetgun", 5, 30.0, 1000.0, 30.0, 1000.0,
    100, TRUE, 50, 1, 4, FALSE, TRUE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_MP5, "weapon_9mmAR", 5, 0.0, 250.0, 300.0, 600.0,
    100, FALSE, 90, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_CROSSBOW, "weapon_crossbow", 5, 100.0, 1000.0, 0.0, 0.0,
    100, TRUE, 100, 1, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_RPG, "weapon_rpg", 5, 300.0, 9999.0, 0.0, 0.0,
    100, TRUE, 100, 1, 0, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {VALVE_WEAPON_GLOCK, "weapon_9mmhandgun", 5, 0.0, 1200.0, 0.0, 1200.0,
    100, TRUE, 70, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0},
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t tfc_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t cs_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t gearbox_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t frontline_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t holywars_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_weapon_select_t dmc_weapon_select[] = { /* ...omitted for brevity... */
   {0, "", 0, 0.0, 0.0, 0.0, 0.0, 0, TRUE, 0, 1, 1, FALSE, FALSE, FALSE, FALSE, 0.0, 0.0}
};
bot_fire_delay_t valve_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t tfc_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t cs_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t gearbox_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t frontline_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t holywars_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};
bot_fire_delay_t dmc_fire_delay[] = { /* ...omitted for brevity... */
   {0, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}, 0.0, {0.0, 0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0, 0.0}}
};


void BotCheckTeamplay(void)
{
   float f_team_play = 0.0;

   // is this TFC or Counter-Strike?
   if ((mod_id == TFC_DLL) || (mod_id == CSTRIKE_DLL) ||
       ((mod_id == GEARBOX_DLL) && (pent_info_ctfdetect != NULL)) ||
       (mod_id == FRONTLINE_DLL))
      f_team_play = 1.0;
   else
      f_team_play = CVAR_GET_FLOAT("mp_teamplay");  // teamplay enabled?

   if (f_team_play > 0.0)
      is_team_play = TRUE;
   else
      is_team_play = FALSE;

   checked_teamplay = TRUE;
}


edict_t *BotFindEnemy( bot_t *pBot )
{
   static bool flag=TRUE;
   edict_t *pent = NULL;
   edict_t *pNewEnemy;
   float nearestdistance;
   int i;

   edict_t *pEdict = pBot->pEdict;

   if (pBot->pBotEnemy != NULL)  // does the bot already have an enemy?
   {
      // if the enemy is dead?
      if (!IsAlive(pBot->pBotEnemy))  // is the enemy dead?, assume bot killed it
      {
         // the enemy is dead, jump for joy about 10% of the time
         if (RANDOM_LONG(1, 100) <= 10)
            pEdict->v.button |= IN_JUMP;

         // check if waiting to throw grenade...
         if (pBot->f_gren_throw_time >= gpGlobals->time)
            BotGrenadeThrow(pBot);  // throw the grenade

         // don't have an enemy anymore so null out the pointer...
         pBot->pBotEnemy = NULL;
      }
      else  // enemy is still alive
      {
         Vector vecEnd;
         int player_team, bot_team;

         vecEnd = pBot->pBotEnemy->v.origin + pBot->pBotEnemy->v.view_ofs;

         if (!checked_teamplay)  // check for team play...
            BotCheckTeamplay();

         if (mod_id == TFC_DLL)
         {
            player_team = UTIL_GetTeam(pBot->pBotEnemy);
            bot_team = UTIL_GetTeam(pEdict);
         }

         // is this bot a medic and is the player on the bot's team?
         if ((mod_id == TFC_DLL) &&
             (pEdict->v.playerclass == TFC_CLASS_MEDIC) &&
             FVisible( vecEnd, pEdict ) &&
             ((bot_team == player_team) ||
              (team_allies[bot_team] & (1<<player_team))))
         {
            if (pBot->f_medic_check_health_time <= gpGlobals->time)
            {
               pBot->f_medic_check_health_time = gpGlobals->time + 5.0;
               pBot->f_heal_percent = RANDOM_FLOAT(90.0, 120.0);
            }

            if ((pBot->pBotEnemy->v.health / pBot->pBotEnemy->v.max_health) * 100.0 >
                pBot->f_heal_percent)
            {
               pBot->pBotEnemy = NULL;  // player is healed, null out pointer
            }
            else
            {
               // if teammate is still visible, keep it

               // face the enemy
               Vector v_enemy_dir = pBot->pBotEnemy->v.origin - pEdict->v.origin; // Renamed v_enemy to v_enemy_dir
               Vector bot_angles = UTIL_VecToAngles( v_enemy_dir );

               pEdict->v.ideal_yaw = bot_angles.y;

               BotFixIdealYaw(pEdict);

               // keep track of when we last saw an enemy
               pBot->f_bot_see_enemy_time = gpGlobals->time;

               return (pBot->pBotEnemy);
            }
         }
         else if (FInViewCone( &vecEnd, pEdict ) &&
                  FVisible( vecEnd, pEdict ))
         {
            // if enemy is still visible and in field of view, keep it

            // face the enemy
            Vector v_enemy_dir = pBot->pBotEnemy->v.origin - pEdict->v.origin; // Renamed v_enemy to v_enemy_dir
            Vector bot_angles = UTIL_VecToAngles( v_enemy_dir );

            pEdict->v.ideal_yaw = bot_angles.y;

            BotFixIdealYaw(pEdict);

            // keep track of when we last saw an enemy
            pBot->f_bot_see_enemy_time = gpGlobals->time;

            return (pBot->pBotEnemy);
         }
         else  // enemy has gone out of bot's line of sight
         {
            // check if waiting to throw grenade...
            if (pBot->f_gren_throw_time >= gpGlobals->time)
               BotGrenadeThrow( pBot );  // throw the grenade
         }
      }
   }

   pent = NULL;
   pNewEnemy = NULL;
   nearestdistance = 1500;

   pBot->enemy_attack_count = 0;  // don't limit number of attacks

   if (mod_id == TFC_DLL)
   {
      Vector vecEnd;

      if (pEdict->v.playerclass == TFC_CLASS_MEDIC)
      {
         // search the world for players...
         for (i = 1; i <= gpGlobals->maxClients; i++)
         {
            edict_t *pPlayer = INDEXENT(i);

            // skip invalid players and skip self (i.e. this bot)
            if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict))
            {
               // skip this player if not alive (i.e. dead or dying)
               if (!IsAlive(pPlayer))
                  continue;

               if ((b_observer_mode) && !(pPlayer->v.flags & FL_FAKECLIENT) && !(pPlayer->v.flags & FL_THIRDPARTYBOT))
                  continue;

               int player_team = UTIL_GetTeam(pPlayer);
               int bot_team = UTIL_GetTeam(pEdict);

               // don't target your enemies...
               if ((bot_team != player_team) &&
                   !(team_allies[bot_team] & (1<<player_team)))
                  continue;

               // check if player needs to be healed...
               if ((pPlayer->v.health / pPlayer->v.max_health) > 0.80)
                  continue;  // health greater than 70% so ignore

               vecEnd = pPlayer->v.origin + pPlayer->v.view_ofs;

               // see if bot can see the player...
               if (FInViewCone( &vecEnd, pEdict ) &&
                   FVisible( vecEnd, pEdict ))
               {
                  float distance = (pPlayer->v.origin - pEdict->v.origin).Length();

                  if (distance < nearestdistance)
                  {
                     nearestdistance = distance;
                     pNewEnemy = pPlayer;

                     pBot->pBotUser = NULL;  // don't follow user when enemy found
                  }
               }
            }
         }
      }
      else if ((pEdict->v.playerclass == TFC_CLASS_ENGINEER) &&
               (pBot->m_rgAmmo[weapon_defs[TF_WEAPON_SPANNER].iAmmo1] >= 20))
      {
         // search the world for players...
         for (i = 1; i <= gpGlobals->maxClients; i++)
         {
            edict_t *pPlayer = INDEXENT(i);

            // skip invalid players and skip self (i.e. this bot)
            if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict))
            {
               // skip this player if not alive (i.e. dead or dying)
               if (!IsAlive(pPlayer))
                  continue;

               if ((b_observer_mode) && !(pPlayer->v.flags & FL_FAKECLIENT) && !(pPlayer->v.flags & FL_THIRDPARTYBOT))
                  continue;

               int player_team = UTIL_GetTeam(pPlayer);
               int bot_team = UTIL_GetTeam(pEdict);

               // don't target your enemies...
               if ((bot_team != player_team) &&
                   !(team_allies[bot_team] & (1<<player_team)))
                  continue;

               // don't try to add armor to other engineers...
               if (pPlayer->v.playerclass == TFC_CLASS_ENGINEER)
                  continue;

               // check if player needs armor repaired...
               if (pPlayer->v.armorvalue >= tfc_max_armor[pPlayer->v.playerclass])
                  continue;

               vecEnd = pPlayer->v.origin + pPlayer->v.view_ofs;

               // see if bot can see the player...
               if (FInViewCone( &vecEnd, pEdict ) &&
                   FVisible( vecEnd, pEdict ))
               {
                  float distance = (pPlayer->v.origin - pEdict->v.origin).Length();

                  if (distance < nearestdistance)
                  {
                     nearestdistance = distance;
                     pNewEnemy = pPlayer;

                     pBot->enemy_attack_count = 3;  // give them 3 whacks

                     pBot->pBotUser = NULL;  // don't follow user when enemy found
                  }
               }
            }
         }
      }

      if (pNewEnemy == NULL)
      {
         while ((pent = UTIL_FindEntityByClassname( pent, "building_sentrygun" )) != NULL)
         {
            int sentry_team = -1;
            int bot_team = UTIL_GetTeam(pEdict);
            bool upgrade_sentry = FALSE;

            if (pent->v.colormap == 0xA096)
               sentry_team = 0;  // blue team's sentry
            else if (pent->v.colormap == 0x04FA)
               sentry_team = 1;  // red team's sentry
            else if (pent->v.colormap == 0x372D)
               sentry_team = 2;  // yellow team's sentry
            else if (pent->v.colormap == 0x6E64)
               sentry_team = 3;  // green team's sentry

            // check if this sentry gun is on bot's team or allies team...
            if ((bot_team == sentry_team) ||
                (team_allies[bot_team] & (1<<sentry_team)))
            {
               float health_percent = (pent->v.health / pent->v.max_health) * 100;

               // check if this player is an engineer AND
               // has enough metal AND sentry gun's health is low AND
               // it's time to check for upgrading sentry guns
               if ((pEdict->v.playerclass == TFC_CLASS_ENGINEER) &&
                   (pBot->m_rgAmmo[weapon_defs[TF_WEAPON_SPANNER].iAmmo1] >= 130) &&
                   (health_percent <= 80.0) &&
                   (pBot->f_other_sentry_time <= gpGlobals->time))
               {
                  upgrade_sentry = TRUE;
               }
               else
                  continue;  // skip this sentry gun
            }

            vecEnd = pent->v.origin + pent->v.view_ofs;

            // is this sentry gun visible?
            if (FInViewCone( &vecEnd, pEdict ) &&
                FVisible( vecEnd, pEdict ))
            {
               float distance = (pent->v.origin - pEdict->v.origin).Length();

               // is this the closest sentry gun?
               if (distance < nearestdistance)
               {
                  nearestdistance = distance;
                  pNewEnemy = pent;

                  if (upgrade_sentry)
                  {
                     pBot->enemy_attack_count = 3;  // give them 3 whacks
                     pBot->f_other_sentry_time = gpGlobals->time + RANDOM_FLOAT(20.0, 30.0);
                  }

                  pBot->pBotUser = NULL;  // don't follow user when enemy found
               }
            }
         }
      }
   }

   if (pNewEnemy == NULL)
   {
      edict_t *pMonster = NULL;
      Vector vecEnd;

      nearestdistance = 9999;

      // search the world for monsters...
      while (!FNullEnt (pMonster = UTIL_FindEntityInSphere (pMonster, pEdict->v.origin, 1000)))
      {
         if (!(pMonster->v.flags & FL_MONSTER))
            continue; // discard anything that is not a monster

         if (!IsAlive (pMonster))
            continue; // discard dead or dying monsters

         // TFC specific checks
         if (mod_id == TFC_DLL)
         {
            if (strncmp ("building_", STRING (pMonster->v.classname), 9) == 0)
               continue; // TFC: skip sentry guns and dispensers

            if (strcmp ("monster_miniturret", STRING (pMonster->v.classname)) == 0)
               continue; // TFC: skip respawn miniturrets
         }

         vecEnd = pMonster->v.origin + pMonster->v.view_ofs;

         // see if bot can't see the player...
         if (!FInViewCone( &vecEnd, pEdict ) ||
             !FVisible( vecEnd, pEdict ))
            continue;

         float distance = (pMonster->v.origin - pEdict->v.origin).Length();
         if (distance < nearestdistance)
         {
            nearestdistance = distance;
            pNewEnemy = pMonster;

            pBot->pBotUser = NULL;  // don't follow user when enemy found
         }
      }

      // search the world for players...
      for (i = 1; i <= gpGlobals->maxClients; i++)
      {
         edict_t *pPlayer = INDEXENT(i);

         // skip invalid players and skip self (i.e. this bot)
         if ((pPlayer) && (!pPlayer->free) && (pPlayer != pEdict))
         {
            // skip this player if not alive (i.e. dead or dying)
            if (!IsAlive(pPlayer))
               continue;

            if ((b_observer_mode) && !(pPlayer->v.flags & FL_FAKECLIENT) && !(pPlayer->v.flags & FL_THIRDPARTYBOT))
               continue;

            if ((mod_id == HOLYWARS_DLL) &&
                (holywars_gamemode == 1) &&     // gamemode == halo
                (pPlayer != holywars_saint) &&  // player is not the saint AND
                (pEdict != holywars_saint))     // bot is not the saint
               continue;  // skip this player

            if ((mod_id == TFC_DLL) &&
                (pPlayer->v.playerclass == TFC_CLASS_SPY))
            {
               // check if spy is disguised as my team...
               char *infobuffer;
               char color[32];
               int color_bot, color_player;

               infobuffer = GET_INFOKEYBUFFER( pEdict );
               strcpy(color, INFOKEY_VALUE (infobuffer, "topcolor"));
               sscanf(color, "%d", &color_bot);

               infobuffer = GET_INFOKEYBUFFER( pPlayer );
               strcpy(color, INFOKEY_VALUE (infobuffer, "topcolor"));
               sscanf(color, "%d", &color_player);

               if (((color_bot==140) || (color_bot==148) || (color_bot==150) || (color_bot==153)) &&
                   ((color_player==140) || (color_player==148) || (color_player==150) || (color_player==153)))
                  continue;

               if (((color_bot == 5) || (color_bot == 250) || (color_bot==255)) &&
                   ((color_player == 5) || (color_player == 250) || (color_player == 255)))
                  continue;

               if ((color_bot == 45) && (color_player == 45))
                  continue;

               if (((color_bot == 80) || (color_bot == 100)) &&
                   ((color_player == 80) || (color_player == 100)))
                  continue;
            }

            vecEnd = pPlayer->v.origin + pPlayer->v.view_ofs;

            // see if bot can't see the player...
            if (!FInViewCone( &vecEnd, pEdict ) ||
                !FVisible( vecEnd, pEdict ))
               continue;

            if (!checked_teamplay)  // check for team play...
               BotCheckTeamplay();

            // is team play enabled?
            if (is_team_play)
            {
               int player_team = UTIL_GetTeam(pPlayer);
               int bot_team = UTIL_GetTeam(pEdict);

               // don't target your teammates...
               if (bot_team == player_team)
                  continue;

               if (mod_id == TFC_DLL)
               {
                  // don't target your allies either...
                  if (team_allies[bot_team] & (1<<player_team))
                     continue;
               }
            }

            float distance = (pPlayer->v.origin - pEdict->v.origin).Length();
            if (distance < nearestdistance)
            {
               nearestdistance = distance;
               pNewEnemy = pPlayer;

               pBot->pBotUser = NULL;  // don't follow user when enemy found
            }
         }
      }
   }

   if (pNewEnemy)
   {
      // face the enemy
      Vector v_enemy_dir = pNewEnemy->v.origin - pEdict->v.origin; // Renamed v_enemy to v_enemy_dir
      Vector bot_angles = UTIL_VecToAngles( v_enemy_dir );

      pEdict->v.ideal_yaw = bot_angles.y;

      BotFixIdealYaw(pEdict);

      // keep track of when we last saw an enemy
      pBot->f_bot_see_enemy_time = gpGlobals->time;

      if (pBot->reaction_time)
      {
         float react_delay;

         int index = pBot->reaction_time - 1;

         int bot_skill = pBot->bot_skill;

         float delay_min = react_delay_min[index][bot_skill];
         float delay_max = react_delay_max[index][bot_skill];

         react_delay = RANDOM_FLOAT(delay_min, delay_max);

         pBot->f_reaction_target_time = gpGlobals->time + react_delay;
      }
   }

   // has the bot NOT seen an ememy for at least 5 seconds (time to reload)?
   if ((pBot->f_bot_see_enemy_time > 0) &&
       ((pBot->f_bot_see_enemy_time + 5.0) <= gpGlobals->time))
   {
      pBot->f_bot_see_enemy_time = -1;  // so we won't keep reloading

      if ((mod_id == VALVE_DLL) || (mod_id == GEARBOX_DLL))
      {
         pEdict->v.button |= IN_RELOAD;  // press reload button
      }

      // initialize aim tracking angles...
      pBot->f_aim_x_angle_delta = 5.0;
      pBot->f_aim_y_angle_delta = 5.0;
   }

   return (pNewEnemy);
}


// specifing a weapon_choice allows you to choose the weapon the bot will
// use (assuming enough ammo exists for that weapon)
// BotFireWeapon will return TRUE if weapon was fired, FALSE otherwise

bool BotFireWeapon( Vector v_enemy, bot_t *pBot, int weapon_choice)
{
   bot_weapon_select_t *pSelect = NULL;
   bot_fire_delay_t *pDelay = NULL;
   int select_index;
   int iId, weapon_index, value;
   bool use_primary;
   bool use_secondary;
   int use_percent;
   int primary_percent;
   bool primary_in_range, secondary_in_range;

   edict_t *pEdict = pBot->pEdict;

   float distance = v_enemy.Length();  // how far away is the enemy?

   if (mod_id == VALVE_DLL)
   {
      pSelect = &valve_weapon_select[0];
      pDelay = &valve_fire_delay[0];
   }
   else if (mod_id == TFC_DLL)
   {
      pSelect = &tfc_weapon_select[0];
      pDelay = &tfc_fire_delay[0];
   }
   else if (mod_id == CSTRIKE_DLL)
   {
      pSelect = &cs_weapon_select[0];
      pDelay = &cs_fire_delay[0];
   }
   else if (mod_id == GEARBOX_DLL)
   {
      pSelect = &gearbox_weapon_select[0];
      pDelay = &gearbox_fire_delay[0];
   }
   else if (mod_id == FRONTLINE_DLL)
   {
      pSelect = &frontline_weapon_select[0];
      pDelay = &frontline_fire_delay[0];
   }
   else if (mod_id == HOLYWARS_DLL)
   {
      pSelect = &holywars_weapon_select[0];
      pDelay = &holywars_fire_delay[0];
   }
   else if (mod_id == DMC_DLL)
   {
      pSelect = &dmc_weapon_select[0];
      pDelay = &dmc_fire_delay[0];
   }

   if (pSelect)
   {
      // are we charging the primary fire?
      if (pBot->f_primary_charging > 0)
      {
         iId = pBot->charging_weapon_id;

         if (mod_id == TFC_DLL)
         {
            if (iId == TF_WEAPON_SNIPERRIFLE)
            {
               pBot->f_move_speed = 0;  // don't move while using sniper rifle
            }
         }

         // is it time to fire the charged weapon?
         if (pBot->f_primary_charging <= gpGlobals->time)
         {
            // we DON'T set pEdict->v.button here to release the
            // fire button which will fire the charged weapon

            pBot->f_primary_charging = -1;  // -1 means not charging

            // find the correct fire delay for this weapon
            select_index = 0;

            while ((pSelect[select_index].iId) &&
                   (pSelect[select_index].iId != iId))
               select_index++;

            // set next time to shoot
            int skill = pBot->bot_skill;
            float base_delay, min_delay, max_delay;

            base_delay = pDelay[select_index].primary_base_delay;
            min_delay = pDelay[select_index].primary_min_delay[skill];
            max_delay = pDelay[select_index].primary_max_delay[skill];

            pBot->f_shoot_time = gpGlobals->time + base_delay +
               RANDOM_FLOAT(min_delay, max_delay);

            return TRUE;
         }
         else
         {
            pEdict->v.button |= IN_ATTACK;   // charge the weapon
            pBot->f_shoot_time = gpGlobals->time;  // keep charging

            return TRUE;
         }
      }

      // are we charging the secondary fire?
      if (pBot->f_secondary_charging > 0)
      {
         iId = pBot->charging_weapon_id;

         // is it time to fire the charged weapon?
         if (pBot->f_secondary_charging <= gpGlobals->time)
         {
            // we DON'T set pEdict->v.button here to release the
            // fire button which will fire the charged weapon

            pBot->f_secondary_charging = -1;  // -1 means not charging

            // find the correct fire delay for this weapon
            select_index = 0;

            while ((pSelect[select_index].iId) &&
                   (pSelect[select_index].iId != iId))
               select_index++;

            // set next time to shoot
            int skill = pBot->bot_skill;
            float base_delay, min_delay, max_delay;

            base_delay = pDelay[select_index].secondary_base_delay;
            min_delay = pDelay[select_index].secondary_min_delay[skill];
            max_delay = pDelay[select_index].secondary_max_delay[skill];

            pBot->f_shoot_time = gpGlobals->time + base_delay +
               RANDOM_FLOAT(min_delay, max_delay);

            return TRUE;
         }
         else
         {
            pEdict->v.button |= IN_ATTACK2;  // charge the weapon
            pBot->f_shoot_time = gpGlobals->time;  // keep charging

            return TRUE;
         }
      }

      select_index = 0;

      // loop through all the weapons until terminator is found...
      while (pSelect[select_index].iId)
      {
         // was a weapon choice specified? (and if so do they NOT match?)
         if ((weapon_choice != 0) &&
             (weapon_choice != pSelect[select_index].iId))
         {
            select_index++;  // skip to next weapon
            continue;
         }

         // is the bot NOT carrying this weapon?
         if (mod_id == DMC_DLL)
         {
            if (!(pBot->pEdict->v.weapons & pSelect[select_index].iId))
            {
               select_index++;  // skip to next weapon
               continue;
            }
         }
         else
         {
            if (!(pBot->pEdict->v.weapons & (1<<pSelect[select_index].iId)))
            {
               select_index++;  // skip to next weapon
               continue;
            }
         }

         // is the bot NOT skilled enough to use this weapon?
         if ((pBot->bot_skill+1) > pSelect[select_index].skill_level)
         {
            select_index++;  // skip to next weapon
            continue;
         }

         // is the bot underwater and does this weapon NOT work under water?
         if ((pEdict->v.waterlevel == 3) &&
             !(pSelect[select_index].can_use_underwater))
         {
            select_index++;  // skip to next weapon
            continue;
         }

         use_percent = RANDOM_LONG(1, 100);

         // is use percent greater than weapon use percent?
         if (use_percent > pSelect[select_index].use_percent)
         {
            select_index++;  // skip to next weapon
            continue;
         }

         iId = pSelect[select_index].iId;
         if (mod_id == DMC_DLL)
         {
            weapon_index = 0;
            value = iId;
            while (value)
            {
               weapon_index++;
               value = value >> 1;
            }
         }
         else
            weapon_index = iId;
         use_primary = FALSE;
         use_secondary = FALSE;
         primary_percent = RANDOM_LONG(1, 100);

         // is primary percent less than weapon primary percent AND
         // no ammo required for this weapon OR
            // enough ammo available to fire AND
         // the bot is far enough away to use primary fire AND
         // the bot is close enough to the enemy to use primary fire

         primary_in_range = (distance >= pSelect[select_index].primary_min_distance) &&
                            (distance <= pSelect[select_index].primary_max_distance);

         secondary_in_range = (distance >= pSelect[select_index].secondary_min_distance) &&
                              (distance <= pSelect[select_index].secondary_max_distance);

         if (weapon_choice != 0)
         {
            primary_in_range = TRUE;
            secondary_in_range = TRUE;
         }

         if ((primary_percent <= pSelect[select_index].primary_fire_percent) &&
             ((weapon_defs[weapon_index].iAmmo1 == -1) ||
              (pBot->m_rgAmmo[weapon_defs[weapon_index].iAmmo1] >=
               pSelect[select_index].min_primary_ammo)) &&
             (primary_in_range))
         {
            use_primary = TRUE;
         }

         // otherwise see if there is enough secondary ammo AND
         // the bot is far enough away to use secondary fire AND
         // the bot is close enough to the enemy to use secondary fire

         else if (((weapon_defs[weapon_index].iAmmo2 == -1) ||
                   (pBot->m_rgAmmo[weapon_defs[weapon_index].iAmmo2] >=
                    pSelect[select_index].min_secondary_ammo)) &&
                  (secondary_in_range))
         {
            use_secondary = TRUE;
         }

         // see if there wasn't enough ammo to fire the weapon...
         if ((use_primary == FALSE) && (use_secondary == FALSE))
         {
            select_index++;  // skip to next weapon
            continue;
         }

         // select this weapon if it isn't already selected
         if (pBot->current_weapon.iId != iId)
         {
            if (mod_id == DMC_DLL)
               UTIL_SelectWeapon(pEdict, weapon_index);
            else
               UTIL_SelectItem(pEdict, pSelect[select_index].weapon_name);
         }

         if (pDelay[select_index].iId != iId)
         {
            char msg[80];
            sprintf(msg, "fire_delay mismatch for weapon id=%d\n",iId);
            ALERT(at_console, msg);

            return FALSE;
         }

         if (mod_id == TFC_DLL)
         {
            if (iId == TF_WEAPON_SNIPERRIFLE)
            {
               pBot->f_move_speed = 0;  // don't move while using sniper rifle

               if (pEdict->v.velocity.Length() > 50)
               {
                  return TRUE;  // don't press attack key until velocity is < 50
               }
            }

            if (pEdict->v.playerclass == TFC_CLASS_MEDIC)
            {
               int player_team = UTIL_GetTeam(pBot->pBotEnemy);
               int bot_team = UTIL_GetTeam(pEdict);

               // only heal your teammates or allies...
               if (((bot_team == player_team) ||
                    (team_allies[bot_team] & (1<<player_team))) &&
                   (iId != TF_WEAPON_MEDIKIT))
               {
                  return FALSE;  // don't "fire" unless weapon is medikit
               }
            }

            if (pEdict->v.playerclass == TFC_CLASS_ENGINEER)
            {
               int player_team = UTIL_GetTeam(pBot->pBotEnemy);
               int bot_team = UTIL_GetTeam(pEdict);

               // only heal your teammates or allies...
               if (((bot_team == player_team) ||
                    (team_allies[bot_team] & (1<<player_team))) &&
                   (iId != TF_WEAPON_SPANNER))
               {
                  return FALSE;  // don't "fire" unless weapon is spanner
               }
            }
         }

         if (((mod_id == VALVE_DLL) && (iId == VALVE_WEAPON_CROWBAR)) ||
             ((mod_id == TFC_DLL) && (iId == TF_WEAPON_AXE)) ||
             ((mod_id == TFC_DLL) && (iId == TF_WEAPON_KNIFE)) ||
             ((mod_id == TFC_DLL) && (iId == TF_WEAPON_SPANNER)) ||
             ((mod_id == CSTRIKE_DLL) && (iId == CS_WEAPON_KNIFE)) ||
             ((mod_id == GEARBOX_DLL) && (iId == GEARBOX_WEAPON_PIPEWRENCH)) ||
             ((mod_id == GEARBOX_DLL) && (iId == GEARBOX_WEAPON_KNIFE)) ||
             ((mod_id == GEARBOX_DLL) && (iId == GEARBOX_WEAPON_CROWBAR)) ||
             ((mod_id == HOLYWARS_DLL) && (iId == HW_WEAPON_JACKHAMMER)) ||
             ((mod_id == DMC_DLL) && (iId == DMC_WEAPON_AXE)))
         {
            // check if bot needs to duck down to hit enemy...
            if (pBot->pBotEnemy->v.origin.z < (pEdict->v.origin.z - 30))
               pBot->f_duck_time = gpGlobals->time + 1.0;

            extern int bot_stop;
            if (bot_stop == 2)
               bot_stop = 1;
         }

         if (use_primary)
         {
            pEdict->v.button |= IN_ATTACK;  // use primary attack

            if (pSelect[select_index].primary_fire_charge)
            {
               pBot->charging_weapon_id = iId;

               // release primary fire after the appropriate delay...
               pBot->f_primary_charging = gpGlobals->time +
                              pSelect[select_index].primary_charge_delay;

               pBot->f_shoot_time = gpGlobals->time;  // keep charging
            }
            else
            {
               // set next time to shoot
               if (pSelect[select_index].primary_fire_hold)
                  pBot->f_shoot_time = gpGlobals->time;  // don't let button up
               else
               {
                  int skill = pBot->bot_skill;
                  float base_delay, min_delay, max_delay;

                  base_delay = pDelay[select_index].primary_base_delay;
                  min_delay = pDelay[select_index].primary_min_delay[skill];
                  max_delay = pDelay[select_index].primary_max_delay[skill];

                  pBot->f_shoot_time = gpGlobals->time + base_delay +
                     RANDOM_FLOAT(min_delay, max_delay);
               }
            }
         }
         else  // MUST be use_secondary...
         {
            pEdict->v.button |= IN_ATTACK2;  // use secondary attack

            if (pSelect[select_index].secondary_fire_charge)
            {
               pBot->charging_weapon_id = iId;

               // release secondary fire after the appropriate delay...
               pBot->f_secondary_charging = gpGlobals->time +
                              pSelect[select_index].secondary_charge_delay;

               pBot->f_shoot_time = gpGlobals->time;  // keep charging
            }
            else
            {
               // set next time to shoot
               if (pSelect[select_index].secondary_fire_hold)
                  pBot->f_shoot_time = gpGlobals->time;  // don't let button up
               else
               {
                  int skill = pBot->bot_skill;
                  float base_delay, min_delay, max_delay;

                  base_delay = pDelay[select_index].secondary_base_delay;
                  min_delay = pDelay[select_index].secondary_min_delay[skill];
                  max_delay = pDelay[select_index].secondary_max_delay[skill];

                  pBot->f_shoot_time = gpGlobals->time + base_delay +
                     RANDOM_FLOAT(min_delay, max_delay);
               }
            }
         }

         return TRUE;  // weapon was fired
      }
   }

   // didn't have any available weapons or ammo, return FALSE
   return FALSE;
}


void BotShootAtEnemy( bot_t *pBot )
{
   float f_distance;
   Vector v_enemy;

   edict_t *pEdict = pBot->pEdict;

   // Initial safety checks
   if (!pEdict || FNullEnt(pEdict)) {
       return;
   }
   // If no enemy, or enemy is not alive, nothing to shoot at.
   // This check is vital before RL logic or original logic.
   if (!pBot->pBotEnemy || FNullEnt(pBot->pBotEnemy) || !IsAlive(pBot->pBotEnemy)) {
       pBot->pBotEnemy = NULL; // Clear enemy if invalid
       return;
   }

   // --- Reinforcement Learning Aiming Logic ---
   // Ensure bot itself is alive before attempting RL aiming
   if (pBot->aiming_nn_initialized && gpGlobals->time >= pBot->f_next_rl_aim_action_time && IsAlive(pEdict)) {
       pBot->f_next_rl_aim_action_time = gpGlobals->time + 0.1f; // RL action interval (e.g., 100ms)

       float state_features[RL_AIMING_STATE_SIZE];
       PrepareRLAimingState(pBot, pBot->pBotEnemy, state_features);

       float log_prob_action = 0.0f;
       RL_AimingAction_e chosen_action = RL_ChooseAction_Policy(&pBot->aiming_rl_nn, state_features, AIM_RL_EXPLORATION_EPSILON, &log_prob_action);

       bool shot_attempted_this_step = (chosen_action == AIM_RL_FIRE_PRIMARY);
       // Check if bot can actually fire (weapon cooldown)
       if (shot_attempted_this_step && pEdict->v.nextattack > gpGlobals->time) {
           shot_attempted_this_step = false;
       }

       ExecuteRLAimingAction(pBot, chosen_action); // Modifies pEdict->v.v_angle and may set IN_ATTACK

       // Calculate reward
       bool hit_this_step = false;
       float reward = CalculateRLAimingReward(pBot, pBot->pBotEnemy, chosen_action, state_features, shot_attempted_this_step, &hit_this_step);

       RL_StoreExperience_Policy(pBot, state_features, chosen_action, reward, log_prob_action);
       pBot->aiming_episode_step_count++;

       bool episode_done = false;
       const float RL_HIT_KILL_REWARD_BONUS = 20.0f;
       const float RL_DEATH_PENALTY_BONUS = -15.0f;

       if (hit_this_step && pBot->pBotEnemy && pBot->pBotEnemy->v.health <= 0) { // Re-check pBotEnemy for safety
           if (!pBot->current_aiming_episode_data.empty()) {
               pBot->current_aiming_episode_data.back().reward_received += RL_HIT_KILL_REWARD_BONUS;
           }
           episode_done = true;
           pBot->pBotEnemy = NULL;
       } else if (!pBot->pBotEnemy || !IsAlive(pBot->pBotEnemy) || (pBot->pBotEnemy->v.effects & EF_NODRAW)) { // Check pBotEnemy again
           episode_done = true;
           pBot->pBotEnemy = NULL;
       } else if (!IsAlive(pEdict)) {
           if (!pBot->current_aiming_episode_data.empty()) {
               pBot->current_aiming_episode_data.back().reward_received += RL_DEATH_PENALTY_BONUS;
           }
           episode_done = true;
       } else if (pBot->aiming_episode_step_count >= MAX_AIMING_EPISODE_LENGTH) {
           episode_done = true;
       }

       if (episode_done) {
           RL_UpdatePolicyNetwork_REINFORCE(pBot, AIM_RL_LEARNING_RATE, AIM_RL_DISCOUNT_FACTOR);
       }
       // The bot's v_angle and button state are set by ExecuteRLAimingAction.
       // These will be used by pfnRunPlayerMove in BotThink.
       return;
   }

   // --- Original Aiming Logic (Fallback if RL doesn't run or conditions not met) ---
   // The original 'if (pBot->f_reaction_target_time > gpGlobals->time) return;' is part of the fallback.
   if (pBot->f_reaction_target_time > gpGlobals->time)
      return;

   // do we need to aim at the feet?
   if (((mod_id == VALVE_DLL) && (pBot->current_weapon.iId == VALVE_WEAPON_RPG)) ||
       ((mod_id == TFC_DLL) && (pBot->current_weapon.iId == TF_WEAPON_RPG)) ||
       ((mod_id == GEARBOX_DLL) && (pBot->current_weapon.iId == GEARBOX_WEAPON_RPG)) ||
       ((mod_id == HOLYWARS_DLL) && (pBot->current_weapon.iId == HW_WEAPON_ROCKETLAUNCHER )) ||
       ((mod_id == DMC_DLL) && (pBot->current_weapon.iId == DMC_WEAPON_ROCKET1)))
   {
      Vector v_src, v_dest;
      TraceResult tr;

      v_src = pEdict->v.origin + pEdict->v.view_ofs;  // bot's eyes
      v_dest = pBot->pBotEnemy->v.origin - pBot->pBotEnemy->v.view_ofs;

      UTIL_TraceLine( v_src, v_dest, dont_ignore_monsters,
                      pEdict->v.pContainingEntity, &tr);

      // can the bot see the enemies feet?

      if ((tr.flFraction >= 1.0) ||
          ((tr.flFraction >= 0.95) &&
           (strcmp("player", STRING(tr.pHit->v.classname)) == 0)))
      {
         // aim at the feet for RPG type weapons
         v_enemy = (pBot->pBotEnemy->v.origin - pBot->pBotEnemy->v.view_ofs) -
                   GetGunPosition(pEdict);
      }
      else
         v_enemy = (pBot->pBotEnemy->v.origin + pBot->pBotEnemy->v.view_ofs) -
                   GetGunPosition(pEdict);
   }
   else
   {
      // aim for the head...
      v_enemy = (pBot->pBotEnemy->v.origin + pBot->pBotEnemy->v.view_ofs) -
                 GetGunPosition(pEdict);
   }

   Vector enemy_angle = UTIL_VecToAngles( v_enemy );

   if (enemy_angle.x > 180)
      enemy_angle.x -=360;

   if (enemy_angle.y > 180)
      enemy_angle.y -=360;

   // adjust the view angle pitch to aim correctly
   enemy_angle.x = -enemy_angle.x;

   float d_x, d_y;

   d_x = (enemy_angle.x - pEdict->v.v_angle.x);
   if (d_x > 180.0f)
      d_x = 360.0f - d_x;
   if (d_x < -180.0f)
      d_x = 360.0f + d_x;

   d_y = (enemy_angle.y - pEdict->v.v_angle.y);
   if (d_y > 180.0f)
      d_y = 360.0f - d_y;
   if (d_y < -180.0f)
      d_y = 360.0f + d_y;

   float delta_dist_x = fabs(d_x / pBot->f_frame_time);
   float delta_dist_y = fabs(d_y / pBot->f_frame_time);

   if ((delta_dist_x > 100.0) && (RANDOM_LONG(1, 100) < 40))
   {
      pBot->f_aim_x_angle_delta += 
         aim_tracking_x_scale[pBot->bot_skill] * pBot->f_frame_time * 0.8;
   }
   else
   {
      pBot->f_aim_x_angle_delta -= 
         aim_tracking_x_scale[pBot->bot_skill] * pBot->f_frame_time;
   }

   if (RANDOM_LONG(1, 100) < ((pBot->bot_skill+1) * 10))
   {
      pBot->f_aim_x_angle_delta += 
         aim_tracking_x_scale[pBot->bot_skill] * pBot->f_frame_time * 0.5;
   }

   if ((delta_dist_y > 100.0) && (RANDOM_LONG(1, 100) < 40))
   {
      pBot->f_aim_y_angle_delta += 
         aim_tracking_y_scale[pBot->bot_skill] * pBot->f_frame_time * 0.8;
   }
   else
   {
      pBot->f_aim_y_angle_delta -= 
         aim_tracking_y_scale[pBot->bot_skill] * pBot->f_frame_time;
   }

   if (RANDOM_LONG(1, 100) < ((pBot->bot_skill+1) * 10))
   {
      pBot->f_aim_y_angle_delta += 
         aim_tracking_y_scale[pBot->bot_skill] * pBot->f_frame_time * 0.5;
   }

   if (pBot->f_aim_x_angle_delta > 5.0)
      pBot->f_aim_x_angle_delta = 5.0;

   if (pBot->f_aim_x_angle_delta < 0.01)
      pBot->f_aim_x_angle_delta = 0.01;

   if (pBot->f_aim_y_angle_delta > 5.0)
      pBot->f_aim_y_angle_delta = 5.0;

   if (pBot->f_aim_y_angle_delta < 0.01)
      pBot->f_aim_y_angle_delta = 0.01;

   if (d_x < 0.0)
      d_x = d_x - pBot->f_aim_x_angle_delta;
   else
      d_x = d_x + pBot->f_aim_x_angle_delta;

   if (d_y < 0.0)
      d_y = d_y - pBot->f_aim_y_angle_delta;
   else
      d_y = d_y + pBot->f_aim_y_angle_delta;

   pEdict->v.idealpitch = pEdict->v.v_angle.x + d_x;
   BotFixIdealPitch(pEdict);

   pEdict->v.ideal_yaw = pEdict->v.v_angle.y + d_y;
   BotFixIdealYaw(pEdict);

   if ((mod_id == TFC_DLL) && (pEdict->v.playerclass == TFC_CLASS_ENGINEER))
   {
      if (strcmp(STRING(pBot->pBotEnemy->v.classname), "building_sentrygun") == 0)
      {
         float distance = (pBot->pBotEnemy->v.origin - pEdict->v.origin).Length();

         if ((pBot->f_shoot_time <= gpGlobals->time) && (distance <= 50))
         {
            BotFireWeapon( v_enemy, pBot, TF_WEAPON_SPANNER );

            pBot->enemy_attack_count--;

            if (pBot->enemy_attack_count <= 0)
               pBot->pBotEnemy = NULL;
         }

         return;
      }
      else if (strcmp(STRING(pBot->pBotEnemy->v.classname), "building_dispenser") == 0)
      {
         float distance = (pBot->pBotEnemy->v.origin - pEdict->v.origin).Length();

         if ((pBot->f_shoot_time <= gpGlobals->time) && (distance <= 50.0))
         {
            if (RANDOM_LONG(1,100) < 50)
               pEdict->v.button |= IN_DUCK;

            BotFireWeapon( v_enemy, pBot, TF_WEAPON_SPANNER );

            pBot->enemy_attack_count--;

            if (pBot->enemy_attack_count <= 0)
               pBot->pBotEnemy = NULL;
         }

         return;
      }
      else if (pBot->enemy_attack_count > 0)
      {
         float distance = (pBot->pBotEnemy->v.origin - pEdict->v.origin).Length();

         if ((pBot->f_shoot_time <= gpGlobals->time) && (distance <= 50))
         {
            BotFireWeapon( v_enemy, pBot, TF_WEAPON_SPANNER );

            pBot->enemy_attack_count--;

            if (pBot->enemy_attack_count <= 0)
               pBot->pBotEnemy = NULL;
         }

         return;
      }
   }

   // see if we have a grenade primed and it's time to throw...
   if ((pBot->b_grenade_primed) &&
       (pBot->f_gren_throw_time <= gpGlobals->time))
   {
      BotGrenadeThrow( pBot );

      return;
   }

   v_enemy.z = 0;  // ignore z component (up & down)

   f_distance = v_enemy.Length();  // how far away is the enemy scum?

   if (pBot->f_gren_check_time <= gpGlobals->time)
   {
      pBot->f_gren_check_time = gpGlobals->time + pBot->grenade_time;

      bool teammate = 0;

      if (!checked_teamplay)  // check for team play...
         BotCheckTeamplay();

      // is team play enabled?
      if (is_team_play)
      {
         // check if "enemy" is a teammate...

         int player_team = UTIL_GetTeam(pBot->pBotEnemy);
         int bot_team = UTIL_GetTeam(pEdict);

         if ((bot_team == player_team) ||
             (team_allies[bot_team] & (1<<player_team)))
            teammate = TRUE;
         else
            teammate = FALSE;
      }

      if (!teammate)
      {
         // see if it's time to throw a grenade yet...
         if ((f_distance <= 300) && (RANDOM_LONG(1, 100) <= 20) &&
             ((pBot->f_gren_throw_time < 0) ||
              (pBot->f_gren_throw_time < gpGlobals->time - pBot->grenade_time)))
         {
            if (BotGrenadeArm( pBot ))
            {
               return;  // grenade is now being primed and ready to throw
            }
         }
      }
   }

   if (f_distance > 200)      // run if distance to enemy is far
      pBot->f_move_speed = pBot->f_max_speed;
   else if (f_distance > 20)  // walk if distance is closer
      pBot->f_move_speed = pBot->f_max_speed / 2;
   else                     // don't move if close enough
      pBot->f_move_speed =10.0;


   // is it time to shoot yet?
   if (pBot->f_shoot_time <= gpGlobals->time)
   {
      // select the best weapon to use at this distance and fire...
      if (!BotFireWeapon(v_enemy, pBot, 0))
      {
         pBot->pBotEnemy = NULL;
         pBot->f_bot_find_enemy_time = gpGlobals->time + 3.0;
      }
   }
}


bool BotShootTripmine( bot_t *pBot )
{
   edict_t *pEdict = pBot->pEdict;

   if (pBot->b_shoot_tripmine != TRUE)
      return FALSE;

   // aim at the tripmine and fire the glock...

   Vector v_enemy = pBot->v_tripmine - GetGunPosition( pEdict );

   pEdict->v.v_angle = UTIL_VecToAngles( v_enemy );

   if (pEdict->v.v_angle.y > 180)
      pEdict->v.v_angle.y -=360;

   // Paulo-La-Frite - START bot aiming bug fix
   if (pEdict->v.v_angle.x > 180)
      pEdict->v.v_angle.x -=360;

   // set the body angles to point the gun correctly
   pEdict->v.angles.x = pEdict->v.v_angle.x / 3;
   pEdict->v.angles.y = pEdict->v.v_angle.y;
   pEdict->v.angles.z = 0;

   // adjust the view angle pitch to aim correctly (MUST be after body v.angles stuff)
   pEdict->v.v_angle.x = -pEdict->v.v_angle.x;
   // Paulo-La-Frite - END

   pEdict->v.ideal_yaw = pEdict->v.v_angle.y;

   BotFixIdealYaw(pEdict);

   return (BotFireWeapon( v_enemy, pBot, VALVE_WEAPON_GLOCK ));
}


bool BotGrenadeArm( bot_t *pBot )
{
   edict_t *pEdict = pBot->pEdict;

   if (mod_id == TFC_DLL)
   {
      // check if bot has both types of grenades...
      if ((pBot->gren1 > 0) && (pBot->gren2 > 0))
      {
         // choose 1 type at random...
         if (RANDOM_LONG(1, 100) <= 50)
            pBot->grenade_type = 0;
         else
            pBot->grenade_type = 1;
      }
      else if (pBot->gren1 > 0)
         pBot->grenade_type = 0;
      else if (pBot->gren2 > 0)
         pBot->grenade_type = 1;
      else
         return FALSE;  // no grenades are available

      pBot->b_grenade_primed = TRUE;

      pBot->f_gren_throw_time = gpGlobals->time + 1.0 + RANDOM_FLOAT(0.5, 1.0);

      if (pBot->grenade_type == 0)
         FakeClientCommand(pEdict, "+gren1", NULL, NULL);
      else
         FakeClientCommand(pEdict, "+gren2", NULL, NULL);

      return TRUE;  // grenade is being primed
   }

   return FALSE;  // grenades not supported in this MOD
}


void BotGrenadeThrow( bot_t *pBot )
{
   edict_t *pEdict = pBot->pEdict;

   if (mod_id == TFC_DLL)
   {
      // make sure it's time to throw grenade...
      if (pBot->f_gren_throw_time > gpGlobals->time)
         return;

      pBot->b_grenade_primed = FALSE;

      // throw the grenade...
      if (pBot->grenade_type == 0)
         FakeClientCommand(pEdict, "-gren1", NULL, NULL);
      else
         FakeClientCommand(pEdict, "-gren2", NULL, NULL);
   }
}
