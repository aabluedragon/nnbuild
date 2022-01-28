#include "duke3d.h"
#include "dukebot.h"
#include "namesdyn.h"

static DukeBotAI_t botAI[MAXPLAYERS];

int16_t searchsect[MAXSECTORS], searchparent[MAXSECTORS];
uint8_t dashow2dsector[(MAXSECTORS + 7) >> 3];

// This matrix is used to determine how far a dukebot will engage another player
// based on what weapon they're carrying, and what weapon their opponent is using.
static int32_t fdmatrix[MAX_WEAPONS][MAX_WEAPONS] =
{
    //KNEE PIST SHOT CHAIN RPG PIPE SHRI DEVI WALL FREE HAND EXPA
    {  128,  -1,  -1,  -1, 128,  -1,  -1,  -1, 128,  -1, 128,  -1 },   //KNEE
    { 1024,1024,1024,1024,2560, 128,2560,2560,1024,2560,2560,2560 },   //PIST
    {  512, 512, 512, 512,2560, 128,2560,2560,1024,2560,2560,2560 },   //SHOT
    {  512, 512, 512, 512,2560, 128,2560,2560,1024,2560,2560,2560 },   //CHAIN
    { 2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560 },   //RPG
    {  512, 512, 512, 512,2048, 512,2560,2560, 512,2560,2560,2560 },   //PIPE
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 },   //SHRI
    { 1536,1536,1536,1536,2560,1536,1536,1536,1536,1536,1536,1536 },   //DEVI
    {   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },   //WALL
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 },   //FREE
    { 2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560,2560 },   //HAND
    {  128, 128, 128, 128,2560, 128,2560,2560, 128, 128, 128, 128 }    //EXPA
};

static bool DukeBot_CanSee(vec3_t pos1, int16_t sect1, vec3_t pos2, int16_t sect2)
{
    return ((cansee(pos1.x, pos1.y, pos1.z, sect1, pos2.x, pos2.y, pos2.z, sect2)) ||
            (cansee(pos1.x, pos1.y, pos1.z - (24 << 8), sect1, pos2.x, pos2.y, pos2.z - (24 << 8), sect2)) ||
            (cansee(pos1.x, pos1.y, pos1.z - (48 << 8), sect1, pos2.x, pos2.y, pos2.z - (48 << 8), sect2)));
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
    DukePlayer_t* p = g_player[pNum].ps;
    
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
    DukePlayer_t* p = g_player[pNum].ps;

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
            if (sprite[p->i].extra < p->max_player_health) return 10;
            return 1;
        case SIXPAK__STATIC:
            if (sprite[p->i].extra < p->max_player_health) return 30;
            return 1;
        case FIRSTAID__STATIC:
            if (p->firstaid_amount < p->max_player_health) return 100;
            return 1;
        case SHIELD__STATIC:
            if (p->shield_amount < p->max_shield_amount) return 50;
            return 1;
        case STEROIDS__STATIC:
            if (p->steroids_amount < 400) return 30;
            return 1;
        case AIRTANK__STATIC:
            if (p->scuba_amount < 6400) return 30;
            return 1;
        case JETPACK__STATIC:
            if (p->jetpack_amount < 1600) return 100;
            return 1;
        case HEATSENSOR__STATIC:
            if (p->heat_amount < 1200) return 5;
            return 1;
        case ACCESSCARD__STATIC:
            return 1;
        case BOOTS__STATIC:
            if (p->boot_amount < 200) return 15;
            return 1;
        case ATOMICHEALTH__STATIC:
            if (sprite[p->i].extra < p->max_player_health << 1) return 50;
            return 1;
        case HOLODUKE__STATIC:
            if (p->holoduke_amount < 2400) return 5;
            return 1;
        case MUSICANDSFX__STATIC: // Why?
            return 1;
    }
    return(0);
}

void DukeBot_GetInput(int snum, input_t* syn)
{
    int dx, dy;
    int dist, daang, zang, fightdist, damyang, damysect;
    int startsect, endsect, splc, send, startwall, endwall;

    DukeBotAI_t* bot = &botAI[snum];
    DukePlayer_t* p = g_player[snum].ps;
    DukePlayer_t* goalp = g_player[bot->goalPlayer].ps;
    walltype* wal;

    auto pSpr = &sprite[p->i];

    syn->fvel = 0;
    syn->svel = 0;
    syn->q16avel = 0;
    syn->q16horz = 0;
    syn->bits = 0;
    syn->extbits = 0;

    if (ud.limit_hit)
        return;

    damyang = sprite[p->i].ang;
    damysect = sprite[p->i].sectnum;
    if ((numplayers >= 2) && (snum == myconnectindex))
    {
        //pos1 = predictedPlayer.pos;
        //pos1.z += PHEIGHT;
        damyang = fix16_to_int(predictedPlayer.q16ang);
        damysect = predictedPlayer.cursectnum;
    }

    if (!(numframes & 7))
    {
        vec3_t testPos = sprite[goalp->i].xyz;
        if(!DukeBot_CanSee(pSpr->xyz, damysect, testPos, sprite[goalp->i].sectnum))
        {
            bot->goalPlayer = snum;
            goalp = g_player[snum].ps;
        }
    }

    if ((bot->goalPlayer == snum) || goalp->dead_flag)
    {
        int32_t lastDist = 0x7fffffff;

        int otherPlayer;
        TRAVERSE_CONNECT(otherPlayer)
        {
            auto pOther = g_player[otherPlayer].ps;
            if (otherPlayer != snum && !(GTFLAGS(GAMETYPE_TDM) && p->team == pOther->team) && !pOther->dead_flag)
            {
                dist = ksqrt((sprite[pOther->i].x - pSpr->x) * (sprite[pOther->i].x - pSpr->x) + (sprite[pOther->i].y - pSpr->y) * (sprite[pOther->i].y - pSpr->y));

                vec3_t testPos = sprite[pOther->i].xyz;
                if (!DukeBot_CanSee(pSpr->xyz, damysect, testPos, sprite[pOther->i].sectnum))
                    dist <<= 1;

                if (dist < lastDist)
                {
                    lastDist = dist;
                    bot->goalPlayer = otherPlayer;
                    goalp = g_player[otherPlayer].ps;
                }
            }
        }
    }

    if (p->dead_flag) syn->bits |= BIT(SK_OPEN);
    if ((p->firstaid_amount > 0) && (p->last_extra < 100))
        syn->bits |= BIT(SK_MEDKIT);

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
                    dx = sintable[(sprite[spr].ang + 512) & 2047];
                    dy = sintable[sprite[spr].ang & 2047];

                    int32_t dodgeDir;
                    if ((pSpr->x - projPos.x) * dy > (pSpr->y - projPos.y) * dx)
                        dodgeDir = -dodgeSpeed * 512;
                    else
                        dodgeDir = dodgeSpeed * 512;

                    syn->fvel -= mulscale17(dy, dodgeDir);
                    syn->svel += mulscale17(dx, dodgeDir);
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

    vec3_t gpPos = sprite[goalp->i].xyz; // Goal Player's Position

    if (bot->goalPlayer != snum && !goalp->dead_flag && DukeBot_CanSee(pSpr->xyz, damysect, gpPos, sprite[goalp->i].sectnum))
    {
        syn->bits |= BIT(SK_FIRE);

        if ((p->curr_weapon == HANDBOMB_WEAPON) && (!(rand() & 7)))
            syn->bits &= ~BIT(SK_FIRE);

        if (p->curr_weapon == TRIPBOMB_WEAPON)
            syn->bits |= ((rand() % MAX_WEAPONS) << SK_WEAPON_BITS);

        if ((p->steroids_amount >= 400) && !(rand() % 100))
            syn->bits |= BIT(SK_STEROIDS);

        if (p->curr_weapon == RPG_WEAPON)
        {
            hitdata_t hit;
            vec3_t testPos = pSpr->xyz;

            // Let's not commit seppuku with rockets.
            testPos.z -= PHEIGHT;
            hitscan(&testPos, damysect, sintable[(damyang + 512) & 2047], sintable[damyang & 2047],
                fix16_to_int(F16(100) - p->q16horiz - p->q16horizoff) * 32, &hit, CLIPMASK1);

            if ((hit.x - pSpr->x) * (hit.x - pSpr->x) + (hit.y - pSpr->y) * (hit.y - pSpr->y) < 2560 * 2560)
                syn->bits &= ~BIT(SK_FIRE);
        }

        fightdist = fdmatrix[p->curr_weapon][goalp->curr_weapon];
        if (fightdist < 128)
            fightdist = 128;

        dist = ksqrt((gpPos.x - pSpr->x) * (gpPos.x - pSpr->x) + (gpPos.y - pSpr->y) * (gpPos.y - pSpr->y));
        if (dist == 0)
            dist = 1;

        daang = getangle(gpPos.x + (goalp->vel.x >> 14) - pSpr->x, gpPos.y + (goalp->vel.y >> 14) - pSpr->y);
        zang = 100 - ((gpPos.z - pSpr->z) * 8) / dist;
        fightdist = max(fightdist, (klabs(gpPos.z - pSpr->z) >> 4));

        if (sprite[goalp->i].yrepeat < 32) // Target shrunk?
        {
            fightdist = 0;
            syn->bits &= ~BIT(SK_FIRE);
        }
        if (sprite[goalp->i].pal == 1) // Target frozen?
        {
            fightdist = 0;
            syn->bits &= ~BIT(SK_FIRE);
        }

        if (dist < 256) // We're close, give 'em the boot.
            syn->bits |= BIT(SK_QUICK_KICK);

        vec2_t fightPos = { (gpPos.x + ((pSpr->x - gpPos.x) * fightdist / dist)),
                            (gpPos.y + ((pSpr->y - gpPos.y) * fightdist / dist)) };
        syn->fvel += (fightPos.x - pSpr->x) * 2047 / dist;
        syn->svel += (fightPos.y - pSpr->y) * 2047 / dist;

        //Strafe attack
        if (fightdist)
        {
            int32_t strafeClock = (int32_t)totalclock + snum * 13468;
            int32_t strafeVal = sintable[(strafeClock << 6) & 2047];

            strafeVal += sintable[((strafeClock + 4245) << 5) & 2047];
            strafeVal += sintable[((strafeClock + 6745) << 4) & 2047];
            strafeVal += sintable[((strafeClock + 15685) << 3) & 2047];

            dx = sintable[(sprite[goalp->i].ang + 512) & 2047];
            dy = sintable[sprite[goalp->i].ang & 2047];

            if ((pSpr->x - gpPos.x) * dy > (pSpr->y - gpPos.y) * dx)
                strafeVal += 8192;
            else
                strafeVal -= 8192;

            syn->fvel += ((sintable[(daang + 1024) & 2047] * strafeVal) >> 17);
            syn->svel += ((sintable[(daang + 512) & 2047] * strafeVal) >> 17);

            if (!(rand() % 200)) // Randomly jump around a bit in battle.
                syn->bits |= BIT(SK_JUMP);
        }

        syn->q16avel = min(max(fix16_from_int((((daang + 1024 - damyang) & 2047) - 1024) >> 1), F16(-127)), F16(127));
        syn->q16horz = min(max(fix16_from_int(zang) - p->q16horiz, F16(-MAXHORIZ)), F16(MAXHORIZ));
        syn->bits |= BIT(SK_AIMMODE);
        return;
    }

    bot->goalSect = -1;
    if (bot->goalSect < 0)
    {
        bot->goalWall = -1;
        startsect = sprite[p->i].sectnum;
        endsect = sprite[goalp->i].sectnum;

        clearbufbyte(dashow2dsector, (MAXSECTORS + 7) >> 3, 0L);
        searchsect[0] = startsect;
        searchparent[0] = -1;
        dashow2dsector[startsect >> 3] |= (1 << (startsect & 7));
        for (splc = 0, send = 1; splc < send; splc++)
        {
            startwall = sector[searchsect[splc]].wallptr;
            endwall = startwall + sector[searchsect[splc]].wallnum;

            int32_t wNum = 0;
            for (wNum = startwall, wal = &wall[startwall]; wNum < endwall; wNum++, wal++)
            {
                int16_t sect = wal->nextsector;
                if (sect < 0)
                    continue;

                dx = ((wall[wal->point2].x + wal->x) >> 1);
                dy = ((wall[wal->point2].y + wal->y) >> 1);

                if ((getceilzofslope(sect, dx, dy) > getflorzofslope(sect, dx, dy) - (28 << 8)) && ((sector[sect].lotag < 15) || (sector[sect].lotag > 22)))
                    continue;

                if (getflorzofslope(sect, dx, dy) < getflorzofslope(searchsect[splc], dx, dy) - (72 << 8))
                    continue;

                if ((dashow2dsector[sect >> 3] & (1 << (sect & 7))) == 0)
                {
                    dashow2dsector[sect >> 3] |= (1 << (sect & 7));
                    searchsect[send] = (short)sect;
                    searchparent[send] = (short)splc;
                    send++;
                    if (sect == endsect)
                    {
                        clearbufbyte(dashow2dsector, (MAXSECTORS + 7) >> 3, 0L);

                        int16_t targetSect;
                        for (targetSect = send - 1; targetSect >= 0; targetSect = searchparent[targetSect])
                            dashow2dsector[searchsect[targetSect] >> 3] |= (1 << (searchsect[targetSect] & 7));

                        for (targetSect = send - 1; targetSect >= 0; targetSect = searchparent[targetSect])
                            if (!searchparent[targetSect]) break;

                        bot->goalSect = searchsect[targetSect];
                        startwall = sector[bot->goalSect].wallptr;
                        endwall = startwall + sector[bot->goalSect].wallnum;

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
                            if (wall[wNum].nextsector != bot->goalSect)
                                continue;

                            dx = wall[wall[wNum].point2].x - wall[wNum].x;
                            dy = wall[wall[wNum].point2].y - wall[wNum].y;

                            //if (dx*(y1-wall[i].y) <= dy*(x1-wall[i].x))
                            //   if (dx*(y2-wall[i].y) >= dy*(x2-wall[i].x))
                            if ((wPos.x - pSpr->x) * (wall[wNum].y - pSpr->y) <= (wPos.y - pSpr->y) * (wall[wNum].x - pSpr->x))
                            {
                                if ((wPos.x - pSpr->x) * (wall[wall[wNum].point2].y - pSpr->y) >= (wPos.y - pSpr->y) * (wall[wall[wNum].point2].x - pSpr->x))
                                {
                                    targetWall = wNum;
                                    break;
                                }
                            }

                            dist = ksqrt(dx * dx + dy * dy);
                            if (dist > lastDist)
                            {
                                lastDist = dist;
                                targetWall = wNum;
                            }
                        }
                        bot->goalWall = targetWall;
                        daang = ((getangle(wall[wall[targetWall].point2].x - wall[targetWall].x, wall[wall[targetWall].point2].y - wall[targetWall].y) + 1536) & 2047);
                        bot->goalPos.x = ((wall[targetWall].x + wall[wall[targetWall].point2].x) >> 1) + (sintable[(daang + 512) & 2047] >> 8);
                        bot->goalPos.y = ((wall[targetWall].y + wall[wall[targetWall].point2].y) >> 1) + (sintable[daang & 2047] >> 8);
                        bot->goalPos.z = sector[bot->goalSect].floorz - (32 << 8);
                        break;
                    }
                }
            }

            for (int32_t spr = headspritesect[searchsect[splc]]; spr >= 0; spr = nextspritesect[spr])
            {
                if (sprite[spr].lotag == SE_7_TELEPORT)
                {
                    int16_t sect = sprite[sprite[spr].owner].sectnum;
                    if ((dashow2dsector[sect >> 3] & (1 << (sect & 7))) == 0)
                    {
                        dashow2dsector[sect >> 3] |= (1 << (sect & 7));
                        searchsect[send] = (short)sect;
                        searchparent[send] = (short)splc;
                        send++;
                        if (sect == endsect)
                        {
                            int16_t targetSect;
                            clearbufbyte(dashow2dsector, (MAXSECTORS + 7) >> 3, 0L);
                            for (targetSect = send - 1; targetSect >= 0; targetSect = searchparent[targetSect])
                                dashow2dsector[searchsect[targetSect] >> 3] |= (1 << (searchsect[targetSect] & 7));

                            for (targetSect = send - 1; targetSect >= 0; targetSect = searchparent[targetSect])
                                if (!searchparent[targetSect]) break;

                            bot->goalSect = searchsect[targetSect];
                            startwall = sector[startsect].wallptr;
                            endwall = startwall + sector[startsect].wallnum;
                            int32_t lastDist = 0;
                            int32_t targetWall = startwall;
                            for (int32_t wNum = startwall; wNum < endwall; wNum++)
                            {
                                dx = wall[wall[wNum].point2].x - wall[wNum].x;
                                dy = wall[wall[wNum].point2].y - wall[wNum].y;
                                dist = ksqrt(dx * dx + dy * dy);
                                if ((wall[wNum].nextsector == bot->goalSect) && (dist > lastDist))
                                {
                                    lastDist = dist;
                                    targetWall = wNum;
                                }

                            }
                            bot->goalWall = targetWall;
                            daang = ((getangle(wall[wall[targetWall].point2].x - wall[targetWall].x, wall[wall[targetWall].point2].y - wall[targetWall].y) + 1536) & 2047);
                            bot->goalPos.x = ((wall[targetWall].x + wall[wall[targetWall].point2].x) >> 1) + (sintable[(daang + 512) & 2047] >> 8);
                            bot->goalPos.y = ((wall[targetWall].y + wall[wall[targetWall].point2].y) >> 1) + (sintable[daang & 2047] >> 8);
                            bot->goalPos.z = sector[bot->goalSect].floorz - (32 << 8);
                            break;
                        }
                    }
                }
            }
            if (bot->goalWall >= 0)
                break;
        }
    }

    if ((bot->goalSect < 0) || (bot->goalWall < 0))
    {
        if (bot->goalSprite < 0 || !cansee(pSpr->x, pSpr->y, pSpr->z - (32 << 8), damysect, sprite[bot->goalSprite].x, sprite[bot->goalSprite].y, sprite[bot->goalSprite].z - (4 << 8), sprite[bot->goalSprite].sectnum))
        {
            int bestSprite = -1, bestScore = 0;

            for (int searchCnt = 0; searchCnt < 16; searchCnt++)
            {
                int16_t sect = (rand() % numsectors);
                for (int32_t spr = headspritesect[sect]; spr >= 0; spr = nextspritesect[spr])
                {
                    if ((sprite[spr].xrepeat <= 0) || (sprite[spr].yrepeat <= 0) || ((sprite[spr].cstat & 32768) == 32768))
                        continue;

                    int32_t curScore = DukeBot_GetSpriteScore(snum, spr);
                    if (curScore <= 0)
                        continue;

                    if (cansee(pSpr->x, pSpr->y, pSpr->z - (32 << 8), damysect, sprite[spr].x, sprite[spr].y, sprite[spr].z - (4 << 8), sect))
                    {
                        if (curScore > bestScore)
                        {
                            bestScore = curScore;
                            bestSprite = spr;
                        }
                    }
                }
            }

            if (bestSprite != -1 && (bot->goalSprite < 0 || bestScore > bot->goalSpriteScore))
            {
                bot->goalPos.x = sprite[bestSprite].x;
                bot->goalPos.y = sprite[bestSprite].y;
                bot->goalPos.z = sprite[bestSprite].z;
                bot->goalSprite = bestSprite;
                bot->goalSpriteScore = bestScore;
            }
        }

        dist = ksqrt((bot->goalPos.x - pSpr->x) * (bot->goalPos.x - pSpr->x) + (bot->goalPos.y - pSpr->y) * (bot->goalPos.y - pSpr->y));
        if (!dist)
            return;

        daang = getangle(bot->goalPos.x - pSpr->x, bot->goalPos.y - pSpr->y);
        syn->fvel += (bot->goalPos.x - pSpr->x) * 2047 / dist;
        syn->svel += (bot->goalPos.y - pSpr->y) * 2047 / dist;
        syn->q16avel = min(max(fix16_from_int((((daang + 1024 - damyang) & 2047) - 1024) >> 3), F16(-127)), F16(127));
    }
    else if (bot->goalSprite != -1)
    {
        auto const gSpr = &sprite[bot->goalSprite];
        if (((gSpr->cstat & 32768) == 32768) || !cansee(pSpr->x, pSpr->y, pSpr->z - (32 << 8), damysect, gSpr->x, gSpr->y, gSpr->z - (4 << 8), gSpr->sectnum))
        {
            bot->goalSpriteScore = 0;
            bot->goalSprite = -1;
        }
    }

    vec3_t curPos = p->pos;
    int16_t dasect = p->cursectnum;

    int32_t moveResult = clipmove(&curPos, &dasect, p->vel.x, p->vel.y, 164L, 4L << 8, 4L << 8, CLIPMASK0);

    if (!moveResult)
    {
        curPos = p->pos;
        curPos.z += (24 << 8);
        dasect = p->cursectnum;
        moveResult = clipmove(&curPos, &dasect, p->vel.x, p->vel.y, 164L, 4L << 8, 4L << 8, CLIPMASK0);
    }

    if (moveResult)
    {
        bot->moveCount++;

        // Avoiding obstructions. (Jumping/crouching)
        int32_t moveType = 0;
        if ((moveResult & 0xc000) == 32768)  //Hit a wall (49152 for sprite)
        {
            if (wall[moveResult & (MAXWALLS - 1)].nextsector >= 0)
            {
                if (getflorzofslope(wall[moveResult & (MAXWALLS - 1)].nextsector, p->pos.x, p->pos.y) <= p->pos.z + (24 << 8))
                    moveType |= 1;

                if (getceilzofslope(wall[moveResult & (MAXWALLS - 1)].nextsector, p->pos.x, p->pos.y) >= p->pos.z - (24 << 8))
                    moveType |= 2;
            }
        }

        if ((moveResult & 0xc000) == 49152)
            moveType |= 1;

        if (moveType & 1)
        {
            if (bot->moveCount == 4)
                syn->bits |= BIT(SK_JUMP);
        }
        if (moveType & 2)
            syn->bits |= BIT(SK_CROUCH);

        //Strafe attack
        daang = getangle(gpPos.x - pSpr->x, gpPos.y - pSpr->y);
        if ((moveResult & 0xc000) == 32768)
            daang = getangle(wall[wall[moveResult & (MAXWALLS - 1)].point2].x - wall[moveResult & (MAXWALLS - 1)].x, wall[wall[moveResult & (MAXWALLS - 1)].point2].y - wall[moveResult & (MAXWALLS - 1)].y);
        
        int32_t strafeClock = (int32_t)totalclock + snum * 13468;
        int32_t strafeVal = sintable[(strafeClock << 6) & 2047];
        strafeVal += sintable[((strafeClock + 4245) << 5) & 2047];
        strafeVal += sintable[((strafeClock + 6745) << 4) & 2047];
        strafeVal += sintable[((strafeClock + 15685) << 3) & 2047];
        syn->fvel += ((sintable[(daang + 1024) & 2047] * strafeVal) >> 17);
        syn->svel += ((sintable[(daang + 512) & 2047] * strafeVal) >> 17);

        if ((bot->moveCount & 31) == 2)
            syn->bits |= BIT(SK_OPEN);
        if ((bot->moveCount & 31) == 17)
            syn->bits |= BIT(SK_QUICK_KICK);

        if (bot->moveCount > 32)
        {
            bot->goalSect = -1;
            bot->goalWall = -1;
            bot->moveCount = 0;
        }

        if (bot->goalSprite != -1)
        {
            auto const gSpr = &sprite[bot->goalSprite];
            if (((gSpr->cstat & 32768) == 32768) || !cansee(pSpr->x, pSpr->y, pSpr->z - (32 << 8), damysect, gSpr->x, gSpr->y, gSpr->z - (4 << 8), gSpr->sectnum))
            {
                bot->goalSpriteScore = 0;
                bot->goalSprite = -1;
            }
        }
    }
    else
        bot->moveCount = 0;

    if ((bot->goalSect >= 0) && (bot->goalWall >= 0))
    {
        dist = ksqrt((bot->goalPos.x - pSpr->x) * (bot->goalPos.x - pSpr->x) + (bot->goalPos.y - pSpr->y) * (bot->goalPos.y - pSpr->y));
        if (!dist)
            return;

        daang = getangle(bot->goalPos.x - pSpr->x, bot->goalPos.y - pSpr->y);
        if ((bot->goalWall >= 0) && (dist < 4096))
            daang = ((getangle(wall[wall[bot->goalWall].point2].x - wall[bot->goalWall].x, wall[wall[bot->goalWall].point2].y - wall[bot->goalWall].y) + 1536) & 2047);
        
        syn->fvel += (bot->goalPos.x - pSpr->x) * 2047 / dist;
        syn->svel += (bot->goalPos.y - pSpr->y) * 2047 / dist;
        syn->q16avel = min(max(fix16_from_int((((daang + 1024 - damyang) & 2047) - 1024) >> 3), -127), 127);
    }
}