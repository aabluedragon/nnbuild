//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers
Copyright (C) 2022, 2022 - Jordon Moss (Aka. StrikerTheHedgefox)

This file is part of EDuke32 / NetDuke32

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#include "duke3d.h"
#include "dukebot.h"
#include "namesdyn.h"

//#define BOT_DEBUG

static DukeBotAI_t botAI[MAXPLAYERS];

// Keep these 10 chars or under.
const char* bot_names[] = {
    "Bweebol",      "Chubbs",       "DickKikem",    "Balzac",
    "2Phat",        "Dookie",       "Manhandlr",    "MurderBot",
    "Lo Wang",      "Caleb",        "Bombshell",    "Scrotal",
    "Arnie",        "Termin8r",     "SteelBalz",    "BotMcBot",
    "Aimbot",       "DodgyMF",      "Devast8r",     "Overlord",
    "Cycloid",      "Lenoman",      "BotHausen",    "CrazyJoe",
    "BoSchitt",     "Duke4.bot",    "AliensSuk",    "Protozoid",
    "Sgt.Farts",    "LigmaBot",     "Zuckerbrg",    "BogosBint",
    "Dr.Proton",    "Rigelatin",    "Deez Nuts",    "GrsyMagic",
    "Aniki",        "Darkholme",    "Ricardo",      "Gachi",
    "CV-96",        "DEADBEEF",     "DukeBurgr",    "Mr. Ass",
    "SukItDown",    "FISTOSHIT",    "Trash Man",    "DethKlok",
    "Pickles",      "Skwisgaar",    "Wartooth",     "MurdrFace",
    "Explosion",    "X2P1158",      "HAL 9000",     "DNCASHMAN",
    "Zilla",        "Chernobog",    "Cerberus",     "PIZOFCHET",
    "Dreemurr",     "Homer",        "Liztroop",     "Enforcer",
    "Pigcop",       "MaknBacon",    "IcedPenis",    "Bazinga",
    "Tile 2200",    "Dylan",        "Taradino",     "Lorelei",
    "Thi",          "Doug",         "Ian",          "El Oscuro",
    "Yogi",         "Yuri",         "Vinny",        "Ringo",
    "Hank",         "Quintin",      "Orion",        "Phobos",
    "Electric6",    "Carnage",      "Marvin",       "P. Dood",
    "Covfefe",      "Gorefield",    "Zardoz",       "SEENINE",
    "OOZFILTER",    "Meat",         "Sponge",       "Pretzel",
    "Booti",        "Capussi",      "Scoot",        "Fromage",
    "DankyKang",    "Wicket",       "HeThicc",      "V-Dub",
    "Robotnik",     "WollSmoth",    "AAAAAAAAA",    "PlzHelp",
    "Dio",          "VargFren",     "Pantherk",     "Luigo",
    "JankOClok",    "CancrMaus",    "H4MMER",       "Gideon",
    "BOTYOT",       "DUKEYOT",      "ABADBABE",     "BEEFBABE",
    "DEADC0DE",     "DEFEC8ED",     "D0D0CACA",     "1337C0D3",
};

// This matrix is used to determine how far a dukebot will engage another player
// based on what weapon they're carrying, and what weapon their opponent is using.
static int32_t fdmatrix[MAX_WEAPONS][MAX_WEAPONS] =
{
    //KNEE PIST SHOT CHAIN RPG PIPE SHRI DEVI TRIP FREE HAND EXPA <- ENEMY v US
    {  128,  -1,  -1,  -1, 128,  -1,  -1,  -1, 128,  -1, 128,  -1 },   //KNEE
    { 1024,1024,1024,1024,2560, 128,2560,2560,1024,2560,2560,2560 },   //PIST
    {  512, 512, 512, 512,2560, 128,2560,2560,1024,2560,2560,2560 },   //SHOT
    {  512, 512, 512, 512,2560, 128,2560,2560,1024,2560,2560,2560 },   //CHAIN
    { 2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560 },   //RPG
    {  512, 512, 512, 512,2048, 512,2560,2560, 512,2560,2560,2560 },   //PIPE
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 },   //SHRI
    { 1536,1536,1536,1536,2560,1536,1536,1536,1536,1536,1536,1536 },   //DEVI
    {   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },   //TRIP
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 },   //FREE
    { 2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560 },   //HAND
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 }    //EXPA
};

static bool DukeBot_CanSee(vec3_t* pos1, int16_t sect1, vec3_t* pos2, int16_t sect2)
{
    return (cansee(pos1->x, pos1->y, pos1->z, sect1, pos2->x, pos2->y, pos2->z, sect2) ||
            cansee(pos1->x, pos1->y, pos1->z - (24 << 8), sect1, pos2->x, pos2->y, pos2->z - (24 << 8), sect2) ||
            cansee(pos1->x, pos1->y, pos1->z - (48 << 8), sect1, pos2->x, pos2->y, pos2->z - (48 << 8), sect2));
}

static bool DukeBot_CanSeeItem(vec3_t* pos1, int16_t sect1, int32_t spriteNum)
{
    auto spr = &sprite[spriteNum];
    return (cansee(pos1->x, pos1->y, pos1->z - (24 << 8), sect1, spr->x, spr->y, spr->z - (4 << 8), spr->sectnum) ||
            cansee(pos1->x, pos1->y, pos1->z - (48 << 8), sect1, spr->x, spr->y, spr->z - (4 << 8), spr->sectnum) ||
            cansee(pos1->x, pos1->y, pos1->z - (72 << 8), sect1, spr->x, spr->y, spr->z - (4 << 8), spr->sectnum));
}

static int32_t weapScore[MAX_WEAPONS][2] =
{
    // Weapon, Ammo
    { 0,    0 },
    { 20,   10 },    // PISTOL_WEAPON
    { 120,  25 },    // SHOTGUN_WEAPON
    { 50,   20 },    // CHAINGUN_WEAPON
    { 200,  50 },    // RPG_WEAPON
    { 60,   30 },    // HANDBOMB_WEAPON
    { 80,   10 },    // SHRINKER_WEAPON
    { 120,  25 },    // DEVISTATOR_WEAPON
    { 50,   10 },    // TRIPBOMB_WEAPON
    { 25,   10 },    // FREEZE_WEAPON
    { 0,    0  },    // HANDREMOTE_WEAPON
    { 200,  50 },    // GROW_WEAPON
};

static int32_t DukeBot_GetWeaponScore(int32_t pNum, int32_t spriteNum, int32_t weapon, bool ammo_only)
{
    auto const p = g_player[pNum].ps;
    
    if (ammo_only)
        return (p->ammo_amount[weapon] < p->max_ammo_amount[weapon]) ? weapScore[weapon][1] : 0;

    // Weapon stay on, we got this weapon, and it's map-spawned? Ignore.
    if (GTFLAGS(GAMETYPE_WEAPSTAY) && p->gotweapon[weapon] && (actor[spriteNum].picnum == sprite[spriteNum].picnum))
        return 0;

    if (p->gotweapon[weapon] && (p->ammo_amount[weapon] >= p->max_ammo_amount[weapon]))
        return 0;

    // If we have the weapon, treat it like ammo. If not, prioritize higher.
    return (p->gotweapon[weapon]) ? weapScore[weapon][1] : weapScore[weapon][0];
}

// Determines the priority of pickups for a bot.
static int DukeBot_GetSpriteScore(int pNum, int32_t spriteNum)
{
    auto const p = g_player[pNum].ps;

    switch (DynamicTileMap[sprite[spriteNum].picnum])
    {
        case FIRSTGUNSPRITE__STATIC:    return DukeBot_GetWeaponScore(pNum, spriteNum, PISTOL_WEAPON, false);
        case RPGSPRITE__STATIC:         return DukeBot_GetWeaponScore(pNum, spriteNum, RPG_WEAPON, false);
        case FREEZESPRITE__STATIC:      return DukeBot_GetWeaponScore(pNum, spriteNum, FREEZE_WEAPON, false);
        case SHRINKERSPRITE__STATIC:    return DukeBot_GetWeaponScore(pNum, spriteNum, SHRINKER_WEAPON, false);
        case HEAVYHBOMB__STATIC:        return DukeBot_GetWeaponScore(pNum, spriteNum, HANDBOMB_WEAPON, false);
        case CHAINGUNSPRITE__STATIC:    return DukeBot_GetWeaponScore(pNum, spriteNum, CHAINGUN_WEAPON, false);
        case TRIPBOMBSPRITE__STATIC:    return DukeBot_GetWeaponScore(pNum, spriteNum, TRIPBOMB_WEAPON, false);
        case SHOTGUNSPRITE__STATIC:     return DukeBot_GetWeaponScore(pNum, spriteNum, SHOTGUN_WEAPON, false);
        case DEVISTATORSPRITE__STATIC:  return DukeBot_GetWeaponScore(pNum, spriteNum, DEVISTATOR_WEAPON, false);

        case FREEZEAMMO__STATIC:        return DukeBot_GetWeaponScore(pNum, spriteNum, FREEZE_WEAPON, true);
        case AMMO__STATIC:              return DukeBot_GetWeaponScore(pNum, spriteNum, PISTOL_WEAPON, true);
        case BATTERYAMMO__STATIC:       return DukeBot_GetWeaponScore(pNum, spriteNum, CHAINGUN_WEAPON, true);
        case DEVISTATORAMMO__STATIC:    return DukeBot_GetWeaponScore(pNum, spriteNum, DEVISTATOR_WEAPON, true);
        case RPGAMMO__STATIC:           return DukeBot_GetWeaponScore(pNum, spriteNum, RPG_WEAPON, true);
        case CRYSTALAMMO__STATIC:       return DukeBot_GetWeaponScore(pNum, spriteNum, SHRINKER_WEAPON, true);
        case GROWAMMO__STATIC:          return DukeBot_GetWeaponScore(pNum, spriteNum, GROW_WEAPON, true);
        case HBOMBAMMO__STATIC:         return DukeBot_GetWeaponScore(pNum, spriteNum, HANDBOMB_WEAPON, true);
        case SHOTGUNAMMO__STATIC:       return DukeBot_GetWeaponScore(pNum, spriteNum, SHOTGUN_WEAPON, true);

        case COLA__STATIC:
            if (sprite[p->i].extra < 50) return 20;
            if (sprite[p->i].extra < p->max_player_health) return 10;
            return 0;
        case SIXPAK__STATIC:
            if (sprite[p->i].extra < 50) return 60;
            if (sprite[p->i].extra < p->max_player_health) return 30;
            return 0;
        case FIRSTAID__STATIC:
            if (p->firstaid_amount < p->max_player_health) return 100;
            return 0;
        case SHIELD__STATIC:
            if (p->shield_amount < p->max_shield_amount) return 50;
            return 0;
        case STEROIDS__STATIC:
            if (p->steroids_amount < 400) return 30;
            return 0;
        case AIRTANK__STATIC:
            if (p->scuba_amount < 6400) return 30;
            return 0;
        case JETPACK__STATIC:
            if (p->jetpack_amount < 1600) return 100;
            return 0;
        case HEATSENSOR__STATIC:
            if (p->heat_amount < 1200) return 5;
            return 0;
        case ACCESSCARD__STATIC:
            return 1;
        case BOOTS__STATIC:
            if (p->boot_amount < 200) return 15;
            return 0;
        case ATOMICHEALTH__STATIC:
            if (sprite[p->i].extra < 50) return 100;
            if (sprite[p->i].extra < p->max_player_health << 1) return 50;
            return 0;
        case HOLODUKE__STATIC:
            if (p->holoduke_amount < 2400) return 5;
            return 0;
        case RESPAWNMARKERGREEN__STATIC:
            return 1;
    }

    return(0);
}

static inline bool DukeBot_IsSpriteValid(int32_t spriteNum)
{
    auto const spr = &sprite[spriteNum];
    if ((uint32_t)spriteNum >= MAXSPRITES)
        return false;

    return !((spr->xrepeat <= 0) || (spr->yrepeat <= 0) || ((spr->cstat & 32768) == 32768) || (spr->statnum == MAXSTATUS));
}

static inline int32_t DukeBot_GetAngleForWall(int16_t wNum)
{
    if ((uint16_t)wNum >= MAXWALLS)
        return 0;

    auto wall1 = &wall[wNum];
    auto wall2 = &wall[wall1->point2];

    return (getangle(wall2->x - wall1->x, wall2->y - wall1->y) + 1536) & 2047;
}

static inline bool DukeBot_CheckUsableSector(int16_t sectNum)
{
    if ((uint16_t)sectNum >= MAXSECTORS)
        return false;

    if (sector[sectNum].lotag & 16384) // Sector's locked.
        return false;

    switch (sector[sectNum].lotag)
    {
        case ST_15_WARP_ELEVATOR:
        case ST_16_PLATFORM_DOWN:
        case ST_17_PLATFORM_UP:
        case ST_18_ELEVATOR_DOWN:
        case ST_19_ELEVATOR_UP:
        case ST_21_FLOOR_DOOR:
        case ST_28_DROP_FLOOR:
        case ST_30_ROTATE_RISE_BRIDGE:
        {
            if (GetAnimationGoal(sectNum, SECTANIM_FLOOR) == -1)
                return true;

            break;
        }
        case ST_20_CEILING_DOOR:
        case ST_22_SPLITTING_DOOR:
        case ST_29_TEETH_DOOR:
        {
            if (GetAnimationGoal(sectNum, SECTANIM_CEILING) == -1)
                return true;

            break;
        }
        case ST_23_SWINGING_DOOR:
        {
            return true;
        }
    }

    return false;
}

// [JM] I think this needs better documentation.
// Still having a hard time figuring out everything this does.
int16_t searchsect[MAXSECTORS], searchparent[MAXSECTORS];
uint8_t dashow2dsector[(MAXSECTORS + 7) >> 3];
static void DukeBot_GetNextWallForPath(int32_t playerNum, int32_t endsect)
{
    auto const bot = &botAI[playerNum];
    auto const p = g_player[playerNum].ps;
    auto const pSpr = &sprite[p->i];

    int32_t startsect = pSpr->sectnum; // Player's current sector.
    if (startsect == -1)
        return;

    // Optimization: If we have a wall, and our start/end sects haven't changed, no point in updating.
    if ((bot->goalWall != -1) && (startsect == bot->lastStartSect) && (endsect == bot->lastEndSect))
        return;

    bot->lastStartSect = startsect;
    bot->lastEndSect = endsect;

    clearbufbyte(dashow2dsector, sizeof(dashow2dsector), 0);
    searchsect[0] = startsect;
    searchparent[0] = -1;
    bitmap_set(dashow2dsector, startsect);

    for (int32_t splc = 0, send = 1; splc < send; splc++)
    {
        int16_t curSectNum = searchsect[splc];
        auto curSect = &sector[curSectNum];

        int32_t startwall = curSect->wallptr;
        int32_t endwall = startwall + curSect->wallnum;

        int32_t wNum = 0;
        walltype* wal;

        for (wNum = startwall, wal = &wall[startwall]; wNum < endwall; wNum++, wal++)
        {
            int16_t nextSectNum = wal->nextsector;
            if (nextSectNum < 0)
                continue;

            auto nSect = &sector[nextSectNum];

            vec2_t d = { ((wall[wal->point2].x + wal->x) >> 1),
                         ((wall[wal->point2].y + wal->y) >> 1) };

            int32_t curFloorZ = yax_getflorzofslope(curSectNum, d);
            int32_t nextFloorZ = nSect->floorz;
            int32_t nextCeilZ = nSect->ceilingz;

            if ((nSect->floorstat | nSect->ceilingstat) & FLOOR_STAT_SLOPE)
                yax_getzsofslope(nextSectNum, d.x, d.y, &nextCeilZ, &nextFloorZ);

            // Ceiling too low, and not a usable sector.
            if ((nextCeilZ > nextFloorZ - (28 << 8)) && ((nSect->lotag < ST_15_WARP_ELEVATOR) || (nSect->lotag > ST_22_SPLITTING_DOOR)))
                continue;

            // Floor too high for sector we're searching from?
            if (nextFloorZ < curFloorZ - (72 << 8) &&
                !((nSect->lotag >= ST_15_WARP_ELEVATOR && nSect->lotag <= ST_19_ELEVATOR_UP) || nSect->lotag == ST_21_FLOOR_DOOR) &&
                !(curSect->lotag >= ST_15_WARP_ELEVATOR && curSect->lotag <= ST_19_ELEVATOR_UP))
                continue;

            if(!bitmap_test(dashow2dsector, nextSectNum))
            {
                bitmap_set(dashow2dsector, nextSectNum);
                searchsect[send] = (int16_t)nextSectNum;
                searchparent[send] = (int16_t)splc;
                send++;
                if (nextSectNum == endsect)
                {
                    clearbufbyte(dashow2dsector, sizeof(dashow2dsector), 0);

                    int16_t k; // Need a good name for this and description of WTF thes two loops do.
                    for (k = send - 1; k >= 0; k = searchparent[k])
                        bitmap_set(dashow2dsector, searchsect[k]);

                    for (k = send - 1; k >= 0; k = searchparent[k])
                        if (!searchparent[k]) break;

                    int32_t desiredSect = searchsect[k];
                    startwall = sector[desiredSect].wallptr;
                    endwall = startwall + sector[desiredSect].wallnum;

                    vec2_t wPos = { 0, 0 };
                    // CHECKME: Does this break the parent loop?
                    for (wNum = startwall; wNum < endwall; wNum++)
                    {
                        wPos.x += wall[wNum].x;
                        wPos.y += wall[wNum].y;
                    }
                    wPos.x /= (endwall - startwall);
                    wPos.y /= (endwall - startwall);

                    startwall = sector[startsect].wallptr;
                    endwall = startwall + sector[startsect].wallnum;
                    int32_t lastDist = 0;
                    int32_t targetWall = startwall;

                    // CHECKME: Does this break the parent loop?
                    for (wNum = startwall; wNum < endwall; wNum++)
                    {
                        if (wall[wNum].nextsector != desiredSect)
                            continue;

                        vec2_t d = { wall[wall[wNum].point2].x - wall[wNum].x,
                                     wall[wall[wNum].point2].y - wall[wNum].y };

                        if ((wPos.x - pSpr->x) * (wall[wNum].y - pSpr->y) <= (wPos.y - pSpr->y) * (wall[wNum].x - pSpr->x))
                        {
                            if ((wPos.x - pSpr->x) * (wall[wall[wNum].point2].y - pSpr->y) >= (wPos.y - pSpr->y) * (wall[wall[wNum].point2].x - pSpr->x))
                            {
                                targetWall = wNum;
                                break;
                            }
                        }

                        int32_t wallDist = ksqrt(d.x * d.x + d.y * d.y);
                        if (wallDist > lastDist)
                        {
                            lastDist = wallDist;
                            targetWall = wNum;
                        }
                    }

                    int32_t wallAng = DukeBot_GetAngleForWall(targetWall);
                    bot->goalPos.x = ((wall[targetWall].x + wall[wall[targetWall].point2].x) >> 1) + (sintable[(wallAng + 512) & 2047] >> 8);
                    bot->goalPos.y = ((wall[targetWall].y + wall[wall[targetWall].point2].y) >> 1) + (sintable[wallAng & 2047] >> 8);
                    bot->goalPos.z = sector[desiredSect].floorz - (32 << 8);
                    bot->goalWall = targetWall;
                    return;
                }
            }
        }

        // Propagate search through teleporters? I think?
        for (int32_t spr = headspritesect[searchsect[splc]]; spr >= 0; spr = nextspritesect[spr])
        {
            if (sprite[spr].lotag == SE_7_TELEPORT)
            {
                int16_t sect = sprite[sprite[spr].owner].sectnum;
                
                if(!bitmap_test(dashow2dsector, sect))
                {
                    bitmap_set(dashow2dsector, sect);
                    searchsect[send] = (int16_t)sect;
                    searchparent[send] = (int16_t)splc;
                    send++;
                    if (sect == endsect)
                    {
                        clearbufbyte(dashow2dsector, sizeof(dashow2dsector), 0);

                        int16_t k; // Need a good name for this and description of WTF thes two loops do.
                        for (k = send - 1; k >= 0; k = searchparent[k])
                            bitmap_set(dashow2dsector, searchsect[k]);

                        for (k = send - 1; k >= 0; k = searchparent[k])
                            if (!searchparent[k]) break;

                        int32_t desiredSect = searchsect[k];
                        startwall = sector[startsect].wallptr;
                        endwall = startwall + sector[startsect].wallnum;

                        int32_t lastDist = 0;
                        int32_t targetWall = startwall;
                        for (int32_t wNum = startwall; wNum < endwall; wNum++)
                        {
                            vec2_t d = { wall[wall[wNum].point2].x - wall[wNum].x,
                                         wall[wall[wNum].point2].y - wall[wNum].y };

                            int32_t wallDist = ksqrt(d.x * d.x + d.y * d.y);
                            if ((wall[wNum].nextsector == desiredSect) && (wallDist > lastDist))
                            {
                                lastDist = wallDist;
                                targetWall = wNum;
                            }

                        }

                        int32_t wallAng = DukeBot_GetAngleForWall(targetWall);
                        bot->goalPos.x = ((wall[targetWall].x + wall[wall[targetWall].point2].x) >> 1) + (sintable[(wallAng + 512) & 2047] >> 8);
                        bot->goalPos.y = ((wall[targetWall].y + wall[wall[targetWall].point2].y) >> 1) + (sintable[wallAng & 2047] >> 8);
                        bot->goalPos.z = sector[desiredSect].floorz - (32 << 8);
                        bot->goalWall = targetWall;
                        return;
                    }
                }
            }
        }
    }
}

static void DukeBot_Init(int32_t playerNum)
{
    auto const bot = &botAI[playerNum];

    bot->goalWall = -1;
    bot->goalSprite = -1;
    bot->goalSpriteScore = 0;
    bot->lastGoalSprite = -1;
    bot->lastEndSect = -1;

    bot->goalPlayer = playerNum;

    bot->moveCount = 0;
    bot->cycleCount = 0;
    
    bot->seekSpriteCooldown = 0;
    bot->seekSpriteFailCount = 0;

    for (int32_t i = 0; i < MAXPLAYERS; i++)
    {
        if (g_player[i].ps == NULL || !g_player[i].connected)
            continue;

        bot->lastSeenPlayerPos[i] = g_player[i].ps->pos;
    }
}

void DukeBot_Activate(int32_t playerNum)
{
    auto const bot = &botAI[playerNum];
    if (bot->active || !g_player[playerNum].isBot)
        return;

    DukeBot_Init(playerNum);

    int32_t botRand = time(0) + playerNum;
    snprintf(g_player[playerNum].user_name, sizeof(g_player[0].user_name), "*%s", bot_names[seed_krand(&botRand) % ARRAY_SIZE(bot_names)]);

    bot->active = 1;
}

void DukeBot_ResetAll()
{
    for (int32_t playerNum = 1; playerNum < ud.multimode; playerNum++)
    {
        DukeBotAI_t* bot = &botAI[playerNum];

        if (bot->active)
            DukeBot_Init(playerNum);
        else if (ud.playerai)
            DukeBot_Activate(playerNum);
    }
}

void DukeBot_GetInput(int snum, input_t* syn)
{
    auto const bot = &botAI[snum];
    auto const p = g_player[snum].ps;
    auto const pSpr = &sprite[p->i];
    auto goalp = g_player[bot->goalPlayer].ps;

    if (!bot->active || ud.limit_hit || ud.pause_on)
        return; 

    syn->fvel = 0;
    syn->svel = 0;
    syn->q16avel = 0;
    syn->q16horz = 0;
    syn->bits = 0;
    syn->extbits = 0;

    if ((bot->cycleCount & 7) == 0)
    {
        vec3_t testPos = sprite[goalp->i].xyz;
        if(!DukeBot_CanSee(&pSpr->xyz, pSpr->sectnum, &testPos, sprite[goalp->i].sectnum))
        {
            bot->goalPlayer = snum;
            goalp = g_player[snum].ps;
        }
    }

    if ((bot->goalPlayer == snum) || goalp->dead_flag)
    {
        int32_t lastDist = INT32_MAX;

        int32_t otherPlayer;
        TRAVERSE_CONNECT(otherPlayer)
        {
            auto pOther = g_player[otherPlayer].ps;
            if (otherPlayer != snum && !(GTFLAGS(GAMETYPE_TDM) && p->team == pOther->team) && !pOther->dead_flag)
            {
                int32_t curDist = dist(&pSpr->xyz, &sprite[pOther->i].xyz);

                if (!DukeBot_CanSee(&pSpr->xyz, pSpr->sectnum, &sprite[pOther->i].xyz, sprite[pOther->i].sectnum))
                    curDist <<= 1;
                else
                    bot->lastSeenPlayerPos[otherPlayer] = sprite[pOther->i].xyz;

                if (curDist < lastDist)
                {
                    lastDist = curDist;
                    bot->goalPlayer = otherPlayer;
                    goalp = g_player[otherPlayer].ps;
                }
            }
        }
    }

    // Dodge incoming projectiles. Dukebots are a real bastard when it comes to this.
    for (int spr = headspritestat[STAT_PROJECTILE]; spr >= 0; spr = nextspritestat[spr])
    {
        int32_t dodgeSpeed = 0;
        switch (DynamicTileMap[sprite[spr].picnum])
        {
        case TONGUE__STATIC:
            dodgeSpeed = 4;
            break;
        case FREEZEBLAST__STATIC:
            dodgeSpeed = 4;
            break;
        case SHRINKSPARK__STATIC:
            dodgeSpeed = 16;
            break;
        case RPG__STATIC:
            dodgeSpeed = 16;
            break;
        default:
            dodgeSpeed = 0;
            break;
        }

        if (dodgeSpeed)
        {
            vec3_t projPos = sprite[spr].xyz;
            for (int32_t dodgeCnt = 0; dodgeCnt <= 8; dodgeCnt++)
            {
                if (tmulscale11(projPos.x - pSpr->x, projPos.x - pSpr->x, projPos.y - pSpr->y, projPos.y - pSpr->y, (projPos.z - pSpr->z) >> 4, (projPos.z - pSpr->z) >> 4) < 3072)
                {
                    vec2_t d = { sintable[(sprite[spr].ang + 512) & 2047],
                                 sintable[sprite[spr].ang & 2047] };

                    int32_t dodgeDir;
                    if ((pSpr->x - projPos.x) * d.y > (pSpr->y - projPos.y) * d.x)
                        dodgeDir = -dodgeSpeed * 512;
                    else
                        dodgeDir = dodgeSpeed * 512;

                    syn->fvel -= mulscale17(d.y, dodgeDir);
                    syn->svel += mulscale17(d.x, dodgeDir);
                }

                if (dodgeCnt < 7)
                {
                    projPos.x += (mulscale14(sprite[spr].xvel, sintable[(sprite[spr].ang + 512) & 2047]) << 2);
                    projPos.y += (mulscale14(sprite[spr].xvel, sintable[sprite[spr].ang & 2047]) << 2);
                    projPos.z += (sprite[spr].zvel << 2);
                }
                else
                {
                    // [JM] Doesn't do anything... hit var isn't used anywhere. Leftover crud?
#if 0
                    hitscan(&sprite[spr].pos, sprite[spr].sectnum,
                        mulscale14(sprite[spr].xvel, sintable[(sprite[spr].ang + 512) & 2047]),
                        mulscale14(sprite[spr].xvel, sintable[sprite[spr].ang & 2047]),
                        (int)sprite[spr].zvel,
                        &hit, CLIPMASK1);
#endif
                }
            }
        }
    }

    if (bot->seekSpriteCooldown > 0)
        bot->seekSpriteCooldown--;

    if (bot->goalSprite != -1)
    {
        if (bot->seekSpriteFailCount > BOT_SEEK_GIVEUP || !DukeBot_IsSpriteValid(bot->goalSprite)) // Can't see or reach, abandon.
        {
#ifdef BOT_DEBUG
            OSD_Printf("^06DukeBot %d (%s)^15: Goal sprite unreachable for too long, or is gone. Abandoning.\n", snum, g_player[snum].user_name);
#endif
            bot->lastGoalSprite = bot->goalSprite;
            bot->goalSprite = -1;
            bot->goalSpriteScore = 0;
            bot->seekSpriteFailCount = 0;
            bot->seekSpriteCooldown = 30;
        }      
    }

    //if ((bot->goalSect < 0) || (bot->goalWall < 0))
    if (!bot->seekSpriteCooldown)
    {
        // No goal sprite, or haven't seen existing goal sprite for a while.
        if (bot->goalSprite == -1 || (bot->seekSpriteFailCount > BOT_SEEK_LOOKFORBETTER))
        {
#ifdef BOT_DEBUG
            if (bot->goalSprite != -1)
                OSD_Printf("^06DukeBot %d (%s)^15: Looking for higher scoring sprite.\n", snum, g_player[snum].user_name);
            else
                OSD_Printf("^06DukeBot %d (%s)^15: Looking for new goal sprite.\n", snum, g_player[snum].user_name);
#endif

            static int32_t stat_list[] = { STAT_ACTOR, STAT_ZOMBIEACTOR };
            int32_t bestSprite = -1, bestScore = 0;
            int32_t closestSprite = -1, closestScore = 0, closestDist = INT32_MAX;

            for (int32_t i = 0; i < (int32_t)ARRAY_SIZE(stat_list); i++)
            {
                int32_t spriteNum;
                for (SPRITES_OF(stat_list[i], spriteNum))
                {
                    auto spr = &sprite[spriteNum];
                    if ((spriteNum == bot->lastGoalSprite) || !DukeBot_IsSpriteValid(spriteNum))
                        continue;

                    int32_t curScore = DukeBot_GetSpriteScore(snum, spriteNum);
                    if (curScore <= 0)
                        continue;

                    int32_t curDist = ldist(&pSpr->xyz, &spr->xyz);
                    if (curDist < closestDist) // Which is closer?
                    {
                        closestSprite = spriteNum;
                        closestScore = curScore;
                        closestDist = curDist;
                    }

                    if (!DukeBot_CanSeeItem(&pSpr->xyz, pSpr->sectnum, spriteNum))
                        continue;
                    
                    if (curScore > bestScore)
                    {
                        bestScore = curScore;
                        bestSprite = spriteNum;
                    }
                }
            }

            if ((bot->goalSprite != -1 || bestSprite == -1) && (closestSprite != -1) && (closestScore >= bot->goalSpriteScore))
            {
#ifdef BOT_DEBUG
                OSD_Printf("^06DukeBot %d (%s)^15: Getting closest sprite.\n", snum, g_player[snum].user_name);
#endif
                bot->goalSprite = closestSprite;
                bot->goalSpriteScore = closestScore;
                //bot->seekSpriteFailCount = 0;
                bot->seekSpriteCooldown = 30;
            }
            else if (bestSprite != -1 && (bot->goalSprite == -1 || (bestScore > bot->goalSpriteScore)))
            {
#ifdef BOT_DEBUG
                OSD_Printf("^06DukeBot %d (%s)^15: New best sprite acquired.\n", snum, g_player[snum].user_name);
#endif
                bot->goalSprite = bestSprite;
                bot->goalSpriteScore = bestScore;
                bot->seekSpriteFailCount = 0;
                bot->seekSpriteCooldown = 30;
            }
            else
            {
#ifdef BOT_DEBUG
                OSD_Printf("^06DukeBot %d (%s)^15: No sprite found.\n", snum, g_player[snum].user_name);
#endif
                bot->seekSpriteCooldown = 30;
            }
        }
    }

    auto const gpSpr = &sprite[goalp->i];
    bool const seePlayer = (!goalp->dead_flag && DukeBot_CanSee(&pSpr->xyz, pSpr->sectnum, &gpSpr->xyz, gpSpr->sectnum));
    bool const seeSprite = (bot->goalSprite != -1) ? DukeBot_CanSeeItem(&pSpr->xyz, pSpr->sectnum, bot->goalSprite) : false;
    bool const prioritizeItems = ((pSpr->extra <= 50) || (p->curr_weapon == KNEE_WEAPON) || (p->ammo_amount[p->curr_weapon] <= (p->max_ammo_amount[p->curr_weapon] >> 3)));
    int32_t moveFlags = 0;
    int32_t combatFlags = 0;

    int32_t const gpDist = max(ldist(&pSpr->xyz, &gpSpr->xyz), 1);
    int32_t const sprDist = (bot->goalSprite != -1) ? max(ldist(&pSpr->xyz, &sprite[bot->goalSprite].xyz), 1) : INT32_MAX;

    // Search sectors.
    int32_t endsect = sprite[goalp->i].sectnum;
    if (bot->goalSprite != -1)
    {
        if (prioritizeItems || (seeSprite && (sprDist < gpDist)) || (!seeSprite && (sprDist<<4 < gpDist)))
            endsect = sprite[bot->goalSprite].sectnum;
    }
    
    DukeBot_GetNextWallForPath(snum, endsect);

    // Get Angle
    int32_t faceAngle = getangle(bot->goalPos.x - pSpr->x, bot->goalPos.y - pSpr->y);

    if (seePlayer)
        faceAngle = getangle(gpSpr->x - pSpr->x, gpSpr->y - pSpr->y);
    else if (seeSprite)
    {
        auto gSpr = &sprite[bot->goalSprite];
        faceAngle = getangle(gSpr->x - pSpr->x, gSpr->y - pSpr->y);
    }

    // Move, do stuff. (or override angle if need be).
    if (seePlayer)
    {
        int32_t fightDist = max(fdmatrix[p->curr_weapon][goalp->curr_weapon], 128);
        fightDist = max(fightDist, (klabs(gpSpr->z - pSpr->z) >> 4));
        int32_t zAngle = 100 - ((gpSpr->z - pSpr->z) * 8) / gpDist;

        hitdata_t hit;
        hitscan(&p->pos, p->cursectnum, sintable[(pSpr->ang + 512) & 2047], sintable[pSpr->ang & 2047],
            fix16_to_int(F16(100) - p->q16horiz - p->q16horizoff) * 32, &hit, CLIPMASK1);

        bot->lastSeenPlayerPos[bot->goalPlayer] = gpSpr->xyz;

        syn->bits |= BIT(SK_FIRE);
        if (gpDist < 256) // We're close, give 'em the boot.
            syn->bits |= BIT(SK_QUICK_KICK);

        // Handle special weapon cases.
        if (p->curr_weapon == RPG_WEAPON)
        {
            if (ldist(&pSpr->xyz, &hit.xyz) < (g_rpgBlastRadius + 512))
                syn->bits &= ~BIT(SK_FIRE);
        }

        if ((p->curr_weapon == HANDBOMB_WEAPON) && (!(rand() & 7)))
            syn->bits &= ~BIT(SK_FIRE);

        if (p->curr_weapon == TRIPBOMB_WEAPON)
            syn->bits |= ((rand() % MAX_WEAPONS) << SK_WEAPON_BITS);

        // Handle Inventory
        if ((p->steroids_amount >= 400) && !(rand() % 100))
            syn->bits |= BIT(SK_STEROIDS);

        if ((gpSpr->yrepeat < 32) || (gpSpr->pal == 1)) // Target shrunk or frozen?
        {
            combatFlags |= BOT_COMBAT_STOMP;
            syn->bits &= ~BIT(SK_FIRE);
            fightDist = 0;
        }

        if (fightDist > 0)
            moveFlags |= BOT_MOVE_STRAFE;

        if (fightDist <= 128)
            combatFlags |= BOT_COMBAT_MELEE;

        if (!prioritizeItems || (combatFlags & (BOT_COMBAT_MELEE | BOT_COMBAT_STOMP)))
        {
            vec2_t fightPos = { (gpSpr->x + ((pSpr->x - gpSpr->x) * fightDist / gpDist)),
                                (gpSpr->y + ((pSpr->y - gpSpr->y) * fightDist / gpDist)) };

            syn->fvel += (fightPos.x - pSpr->x) * 2047 / gpDist;
            syn->svel += (fightPos.y - pSpr->y) * 2047 / gpDist;
        }

#if 0
        syn->bits &= ~BIT(SK_FIRE); // DEBUG: STOP SHOOTING, AND HOLD STILL, DAMNIT.
        moveFlags &= ~BOT_MOVE_STRAFE;
#endif

        syn->q16horz = min(max(fix16_from_int(zAngle) - p->q16horiz, F16(-MAXHORIZ)), F16(MAXHORIZ));
        syn->bits |= BIT(SK_AIMMODE);
    }

    if (seeSprite)
    {
        if (sprDist < 96) // Pretty much on top of it, so let it go.
            bot->seekSpriteFailCount += 30;

        if (!((combatFlags & BOT_COMBAT_MELEE && (gpDist < sprDist)) || combatFlags & BOT_COMBAT_STOMP))
        {
            auto gSpr = &sprite[bot->goalSprite];
            syn->fvel += ((gSpr->x - pSpr->x) * 2047) / sprDist;
            syn->svel += ((gSpr->y - pSpr->y) * 2047) / sprDist;
        }
#ifdef BOT_DEBUG
        //OSD_Printf("DukeBot #%d (%s): Sprite seen. Pursuing. (Dist: %d)\n", snum, g_player[snum].user_name, sprDist);
#endif
    }
    else bot->seekSpriteFailCount++;

    if (bot->goalWall >= 0)
    {
        int32_t goalDist = max(ldist(&pSpr->xyz, &bot->goalPos), 1);

        if (!seePlayer && (bot->goalWall >= 0) && (goalDist < 4096)) // Face wall if we're close and we're targeting one.
            faceAngle = DukeBot_GetAngleForWall(bot->goalWall);

#if  0
        // [JM]: Close to our goal wall, start a new search.
        // NOTE: I want to change this to an intersection check with the player's radius, so it's a bit more... accurate? I guess?
        //       Someone will have to teach me how to do this, because I'm dumb as shit.
        if (goalDist < 1024)
            bot->goalWall = -1;
#endif

        if (!seeSprite && !(combatFlags & BOT_COMBAT_MELEE))
        {
            syn->fvel += ((bot->goalPos.x - pSpr->x) * 2047) / goalDist;
            syn->svel += ((bot->goalPos.y - pSpr->y) * 2047) / goalDist;
        }
    }
    else if (!seeSprite && !seePlayer)
    {
        auto lsPos = &bot->lastSeenPlayerPos[bot->goalPlayer];

        faceAngle = getangle(lsPos->x - pSpr->x, lsPos->y - pSpr->y);

        int32_t lastSeenDist = max(ldist(&pSpr->xyz, lsPos), 1);
        syn->fvel += ((lsPos->x - pSpr->x) * 2047) / lastSeenDist;
        syn->svel += ((lsPos->y - pSpr->y) * 2047) / lastSeenDist;

#ifdef BOT_DEBUG
        OSD_Printf("DukeBot #%d (%s): ^02Cannot see player %d, heading to XYZ: %d, %d, %d (Dist: %d)\n", snum, g_player[snum].user_name, bot->goalPlayer, lsPos->x, lsPos->y, lsPos->z, lastSeenDist);
#endif
    }

    vec3_t curPos = p->pos;
    int16_t curSect = p->cursectnum;

    int32_t moveResult = clipmove(&curPos, &curSect, p->vel.x, p->vel.y, 164L, 4L << 8, 4L << 8, CLIPMASK0);

    if (!moveResult)
    {
        curPos = p->pos;
        curPos.z += (24 << 8);
        curSect = p->cursectnum;
        moveResult = clipmove(&curPos, &curSect, p->vel.x, p->vel.y, 164L, 4L << 8, 4L << 8, CLIPMASK0);
    }

    if (moveResult)
    {
        bot->moveCount++;
        moveFlags |= BOT_MOVE_STRAFE;

        // Avoiding obstructions. (Jumping/crouching, etc) 
        if ((moveResult & 49152) == 32768)  //Hit a wall (49152 for sprite)
        {
            int32_t wallNum = moveResult & (MAXWALLS - 1);
            int16_t nextSect = wall[wallNum].nextsector;
            if (nextSect == -1) // No sector, so let's try seeking through TROR.
            {
                wallNum = yax_getnextwall(wallNum, YAX_CEILING);
                if (wallNum != -1)
                    nextSect = wall[wallNum].nextsector;
            }

            if (nextSect >= 0)
            {
                int32_t floorZ = yax_getflorzofslope(nextSect, p->pos.xy);
                int32_t ceilZ = yax_getceilzofslope(nextSect, p->pos.xy);

                if (floorZ <= p->pos.z + (24 << 8))
                    moveFlags |= BOT_MOVE_JUMP;

                if (ceilZ >= p->pos.z - (24 << 8))
                    moveFlags |= BOT_MOVE_CROUCH;

                if (DukeBot_CheckUsableSector(nextSect) || DukeBot_CheckUsableSector(pSpr->sectnum))
                {
                    moveFlags |= BOT_MOVE_USE;
                    faceAngle = DukeBot_GetAngleForWall(wallNum); // Face the wall.
                }
                else if (floorZ == ceilZ) // Sector's closed up. Potentially a door? Try it.
                    moveFlags |= BOT_MOVE_USE;
            }
        }

        if ((moveResult & 49152) == 49152) // Bumped into a sprite.
        {
            int32_t spriteNum = moveResult & (MAXSPRITES - 1);

            if(!((combatFlags & BOT_COMBAT_MELEE) && (spriteNum == goalp->i))) // Not touching target in melee mode.
                moveFlags |= BOT_MOVE_JUMP;
        }

        // Process move types
        if (moveFlags & BOT_MOVE_JUMP)
        {
            if (((bot->moveCount & 31) == 4) || ((bot->moveCount & 31) == 8))
                syn->bits |= BIT(SK_JUMP);

            if (bot->goalSprite != -1) // Give up faster if we have to attempt jumping at a sprite.
                bot->seekSpriteFailCount++;
        }

        if (moveFlags & BOT_MOVE_CROUCH)
            syn->bits |= BIT(SK_CROUCH);

        if (moveFlags & BOT_MOVE_USE && !(bot->moveCount % 2))
            syn->bits |= BIT(SK_OPEN);

        if ((bot->moveCount & 31) == 17)
            syn->bits |= BIT(SK_QUICK_KICK);

        if (bot->moveCount > 32)
        {
            bot->goalWall = -1;
            bot->moveCount = 0;
        }
    }
    else
        bot->moveCount = 0;

    if (moveFlags & BOT_MOVE_STRAFE)
    {
        //Strafe attack
        int32_t strafeClock = (int32_t)totalclock + snum * 13468;
        int32_t strafeVal = sintable[(strafeClock << 6) & 2047];

        strafeVal += sintable[((strafeClock + 4245) << 5) & 2047];
        strafeVal += sintable[((strafeClock + 6745) << 4) & 2047];
        strafeVal += sintable[((strafeClock + 15685) << 3) & 2047];

        if (seePlayer && !prioritizeItems && !(combatFlags & BOT_COMBAT_MELEE))
        {
            int32_t dx = sintable[(sprite[goalp->i].ang + 512) & 2047];
            int32_t dy = sintable[sprite[goalp->i].ang & 2047];

            if ((pSpr->x - gpSpr->x) * dy > (pSpr->y - gpSpr->y) * dx)
                strafeVal += 8192;
            else
                strafeVal -= 8192;

            if ((gpDist > 256) && !(rand() % 200)) // Randomly jump around a bit in battle.
                syn->bits |= BIT(SK_JUMP);
        }

        syn->fvel += ((sintable[(faceAngle + 1024) & 2047] * strafeVal) >> 17);
        syn->svel += ((sintable[(faceAngle + 512) & 2047] * strafeVal) >> 17);
    }

    if (p->dead_flag)
        syn->bits |= BIT(SK_OPEN);

    if ((p->firstaid_amount > 0) && (p->last_extra < 100))
        syn->bits |= BIT(SK_MEDKIT);

    if((klabs(syn->fvel) > BOT_MAXSPEED>>2) || (klabs(syn->svel) > BOT_MAXSPEED>>2))
        syn->bits |= BIT(SK_RUN);

    // Instigate a bit of randomness, which may help to get unstuck in some situations.
    syn->fvel += (rand() % 512)-256;
    syn->svel += (rand() % 512)-256;

    // Cap velocity, so we're not inhumanly fast.
    syn->fvel = clamp(syn->fvel, -BOT_MAXSPEED, BOT_MAXSPEED);
    syn->svel = clamp(syn->svel, -BOT_MAXSPEED, BOT_MAXSPEED);

    syn->q16avel = min(max(fix16_from_int((((faceAngle + 1024 - pSpr->ang) & 2047) - 1024) >> 1), F16(-127)), F16(127));
    bot->cycleCount++;
}