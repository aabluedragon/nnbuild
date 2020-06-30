//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment
Copyright (C) 2000, 2003 - Matt Saettler (EDuke Enhancements)
Copyright (C) 2004, 2007 - EDuke32 developers

This file is part of EDuke32

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

// Savage Baggage Masters

#include "duke3d.h"
#include "osd.h"
#include "gamedef.h"

int g_currentweapon;
int g_gun_pos;
int g_looking_arc;
int g_weapon_offset;
int g_gs;
int g_kb;
int g_looking_angSR1;
int g_weapon_xoffset;

int32_t turnheldtime; //MED
int32_t lastcontroltime; //MED

int numTeams = 4;
// No sense having more teams than you can have players.
Team_t teams[MAXPLAYERS] = {
    { "Blue", 9, 0, 0, 0 },
    { "Red", 10, 0, 0, 0 },
    { "Green", 11, 0, 0, 0 },
    { "Gray", 12, 0, 0, 0 },
};

uint8_t allowedColors[(MAXPALOOKUPS + CHAR_BIT - 1) / CHAR_BIT];
char pColorNames[MAXPALOOKUPS][32] = { "Auto" };

extern int g_levelTextTime;

void playerColor_init(void)
{
    initprintf("Player Color INIT:\n");
    playerColor_add("Blue", 9);
    playerColor_add("Red", 10);
    playerColor_add("Green", 11);
    playerColor_add("Gray", 12);
    playerColor_add("Dark gray", 13);
    playerColor_add("Dark green", 14);
    playerColor_add("Brown", 15);
    playerColor_add("Dark blue", 16);
    playerColor_add("Bright red", 21);
    playerColor_add("Yellow", 23);
}

uint8_t playerColor_getNextColor(uint8_t palnum, uint8_t useauto)
{
    int seek = palnum;
    while (1)
    {
        seek++;
        if (seek >= MAXPALOOKUPS)
        {
            seek = 0;
        }

        if (TEST_ALLOWEDCOLOR(seek) || (useauto && seek == 0))
        {
            break;
        }
    }

    return (uint8_t)seek;
}

uint8_t playerColor_getPreviousColor(uint8_t palnum, uint8_t useauto)
{
    int seek = palnum;
    while (1)
    {
        seek--;
        if (seek < 0)
        {
            seek = MAXPALOOKUPS - 1;
        }

        if (TEST_ALLOWEDCOLOR(seek) || (useauto && seek == 0))
        {
            break;
        }
    }

    return (uint8_t)seek;
}

void playerColor_add(const char *name, int palnum)
{
    if (palnum <= 0 || palnum >= MAXPALOOKUPS)
    {
        initprintf("Tried to add an invalid player pal: %d\n", palnum);
        return;
    }

    Bsprintf(pColorNames[palnum], "%s", name);
    SET_ALLOWEDCOLOR(palnum);

    initprintf("Added color %s (%d)\n", name, palnum);
}

void playerColor_delete(int palnum)
{
    int i;

    if (palnum == 0 || palnum >= MAXPALOOKUPS)
    {
        initprintf("Tried to delete an invalid player pal: %d\n", palnum);
        return;
    }

    if (palnum > 0)
    {
        RESET_ALLOWEDCOLOR(palnum);
    }
    else if (palnum < 0) // Negative number, delete all.
    {
        for (i = 1; i < MAXPALOOKUPS; i++)
        {
            RESET_ALLOWEDCOLOR(i);
            memset(pColorNames[i], 0, sizeof(pColorNames[0]));
        }
    }
}

void P_UpdateScreenPal(DukePlayer_t *p)
{
    if (p->heat_on) p->palette = SLIMEPAL;
    else if (p->cursectnum < 0) p->palette = BASEPAL;
    else if ((sector[p->cursectnum].ceilingpicnum >= FLOORSLIME) && (sector[p->cursectnum].ceilingpicnum <= FLOORSLIME + 2))
    {
        p->palette = SLIMEPAL;
    }
    else
    {
        if (sector[p->cursectnum].lotag == ST_2_UNDERWATER) p->palette = WATERPAL;
        else p->palette = BASEPAL;
    }
    g_restorePalette = 1;
}

static void P_IncurDamage(DukePlayer_t *p)
{
    int damage = 0L, shield_damage = 0L;

    Gv_SetVar(g_iReturnVarID,0,p->i,sprite[p->i].yvel);
    X_OnEvent(EVENT_INCURDAMAGE, p->i, sprite[p->i].yvel, -1);

    if (Gv_GetVar(g_iReturnVarID,p->i,sprite[p->i].yvel) == 0)

    {
        sprite[p->i].extra -= p->extra_extra8>>8;

        damage = sprite[p->i].extra - p->last_extra;

        if (damage < 0)
        {
            p->extra_extra8 = 0;

            if (p->shield_amount > 0)
            {
                shield_damage =  damage * (20 + (krand()%30)) / 100;
                damage -= shield_damage;

                p->shield_amount += shield_damage;

                if (p->shield_amount < 0)
                {
                    damage += p->shield_amount;
                    p->shield_amount = 0;
                }
            }

            sprite[p->i].extra = p->last_extra + damage;
        }

    }

}

void P_QuickKill(DukePlayer_t *p)
{
    p->pals[0] = 48;
    p->pals[1] = 48;
    p->pals[2] = 48;
    p->pals_time = 48;

    sprite[p->i].extra = 0;
    sprite[p->i].cstat |= 32768;
    if (ud.god == 0) A_DoGuts(p->i,JIBS6,8);
    return;
}

static void A_DoWaterTracers(int x1,int y1,int z1,int x2,int y2,int z2,int n)
{
    int i, xv, yv, zv;
    short sect = -1;

    i = n+1;
    xv = (x2-x1)/i;
    yv = (y2-y1)/i;
    zv = (z2-z1)/i;

    if ((klabs(x1-x2)+klabs(y1-y2)) < 3084)
        return;

    for (i=n;i>0;i--)
    {
        x1 += xv;
        y1 += yv;
        z1 += zv;
        updatesector(x1,y1,&sect);
        if (sect >= 0)
        {
            if (sector[sect].lotag == ST_2_UNDERWATER)
                A_InsertSprite(sect,x1,y1,z1,WATERBUBBLE,-32,4+(krand()&3),4+(krand()&3),krand()&2047,0,0,g_player[0].ps->i,5);
            else
                A_InsertSprite(sect,x1,y1,z1,SMALLSMOKE,-32,14,14,0,0,0,g_player[0].ps->i,5);
        }
    }
}

static void A_HitscanProjTrail(int x1, int y1, int z1, int x2, int y2, int z2, int ang, int atwith)
{
    int n, j, i;
    short sect = -1;

    x1 += (sintable[(348+ang+512)&2047]/ProjectileData[atwith].offset);
    y1 += (sintable[(ang+348)&2047]/ProjectileData[atwith].offset);
    z1 += 1024+(ProjectileData[atwith].toffset<<8);

    n = ((FindDistance2D(x1-x2,y1-y2))>>8)+1;

    x2 = ((x2-x1)/n);
    y2 = ((y2-y1)/n);
    z2 = ((z2-z1)/n);

    x1 += x2>>2;
    y1 += y2>>2;
    z1 += (z2>>2);

    for (i=ProjectileData[atwith].tnum;i>0;i--)
    {
        x1 += x2;
        y1 += y2;
        z1 += z2;
        updatesector(x1,y1,&sect);

        if (sect < 0)
            break;
        
        getzsofslope(sect,x1,y1,&n,&j);

        if (z1 > j || z1 < n)
            break;

        j = A_InsertSprite(sect,x1,y1,z1,ProjectileData[atwith].trail,-32,ProjectileData[atwith].txrepeat,ProjectileData[atwith].tyrepeat,ang,0,0,g_player[0].ps->i,0);
        changespritestat(j,1);
    }
}

int A_GetHitscanRange(int i)
{
    hitdata_t hit;
    int zoff = 0;

    if (PN == APLAYER) zoff = (40<<8);

    vec3_t origin;
    origin = sprite[i].pos;
    origin.z -= zoff;

    hitscan(&origin,SECT,
            sintable[(SA+512)&2047],
            sintable[SA&2047],
            0,&hit,CLIPMASK1);

    return (FindDistance2D(hit.pos.x-origin.x,hit.pos.y-origin.y));
}

static int A_FindTargetSprite(spritetype *s,int aang,int atwith)
{
    int gotshrinker,gotfreezer;
    int i, j, a, k, cans;
    static int aimstats[] = {10,13,1,2};
    int dx1, dy1, dx2, dy2, dx3, dy3, smax, sdist;
    int xv, yv;

    if (s->picnum == APLAYER)
    {
        if (!g_player[s->yvel].ps->auto_aim)
            return -1;
        if (g_player[s->yvel].ps->auto_aim == 2)
        {
            if (A_CheckSpriteTileFlags(atwith,SPRITE_PROJECTILE) && (ProjectileData[atwith].workslike & PROJECTILE_RPG))
                return -1;
            else switch (DynamicTileMap[atwith])
                {
                case TONGUE__STATIC:
                case FREEZEBLAST__STATIC:
                case SHRINKSPARK__STATIC:
                case SHRINKER__STATIC:
                case RPG__STATIC:
                case FIRELASER__STATIC:
                case SPIT__STATIC:
                case COOLEXPLOSION1__STATIC:
                    return -1;
                default:
                    break;
                }
        }
    }

    a = s->ang;

    j = -1;

    gotshrinker = (s->picnum == APLAYER && *aplWeaponWorksLike[g_player[s->yvel].ps->curr_weapon] == SHRINKER_WEAPON);
    gotfreezer = (s->picnum == APLAYER && *aplWeaponWorksLike[g_player[s->yvel].ps->curr_weapon] == FREEZE_WEAPON);

    smax = 0x7fffffff;

    dx1 = sintable[(a+512-aang)&2047];
    dy1 = sintable[(a-aang)&2047];
    dx2 = sintable[(a+512+aang)&2047];
    dy2 = sintable[(a+aang)&2047];

    dx3 = sintable[(a+512)&2047];
    dy3 = sintable[a&2047];

    for (k=0;k<4;k++)
    {
        if (j >= 0)
            break;
        for (i=headspritestat[aimstats[k]];i >= 0;i=nextspritestat[i])
            if (sprite[i].xrepeat > 0 && sprite[i].extra >= 0 && (sprite[i].cstat&(257+32768)) == 257)
                if (A_CheckEnemySprite(&sprite[i]) || k < 2)
                {
                    if (A_CheckEnemySprite(&sprite[i]) || PN == APLAYER || PN == SHARK)
                    {
                        if (PN == APLAYER &&
                                // ud.ffire == 0 &&
                                (GTFLAGS(GAMETYPE_PLAYERSFRIENDLY) || (GTFLAGS(GAMETYPE_TDM) && g_player[sprite[i].yvel].ps->team == g_player[s->yvel].ps->team)) &&
                                s->picnum == APLAYER &&
                                s != &sprite[i])
                            continue;

                        if (gotshrinker && sprite[i].xrepeat < 30)
                        {
                            if (PN == SHARK)
                            {
                                if (sprite[i].xrepeat < 20) continue;
                                continue;
                            }
                            else if (!(PN >= GREENSLIME && PN <= GREENSLIME+7))
                                continue;
                        }
                        if (gotfreezer && sprite[i].pal == 1) continue;
                    }

                    xv = (SX-s->x);
                    yv = (SY-s->y);

                    if ((dy1*xv) <= (dx1*yv))
                        if ((dy2*xv) >= (dx2*yv))
                        {
                            sdist = mulscale(dx3,xv,14) + mulscale(dy3,yv,14);
                            if (sdist > 512 && sdist < smax)
                            {
                                if (s->picnum == APLAYER)
                                    a = (klabs(scale(SZ-s->z,10,sdist)-fix16_to_int(g_player[s->yvel].ps->q16horiz+g_player[s->yvel].ps->q16horizoff-F16(100))) < 100);
                                else a = 1;

                                if (PN == ORGANTIC || PN == ROTATEGUN)
                                    cans = cansee(SX,SY,SZ,SECT,s->x,s->y,s->z-(32<<8),s->sectnum);
                                else cans = cansee(SX,SY,SZ-(32<<8),SECT,s->x,s->y,s->z-(32<<8),s->sectnum);

                                if (a && cans)
                                {
                                    smax = sdist;
                                    j = i;
                                }
                            }
                        }
                }
    }

    return j;
}

int A_Shoot(int i,int atwith)
{
    short l, sa, p, j, k=-1, wh, scount;
    int vel, zvel = 0, x, oldzvel, dal;
    unsigned char sizx,sizy;
    spritetype *s = &sprite[i];
    short sect = s->sectnum;

    vec3_t spos;
    hitdata_t hit;

    if (s->picnum == APLAYER)
    {
        p = s->yvel;

        spos.x = g_player[p].ps->pos.x;
        spos.y = g_player[p].ps->pos.y;
        spos.z = g_player[p].ps->pos.z+g_player[p].ps->pyoff+(4<<8);
        sa = fix16_to_int(g_player[p].ps->q16ang);

        g_player[p].ps->crack_time = 777;
    }
    else
    {
        p = -1;
        sa = s->ang;
        spos.x = s->x;
        spos.y = s->y;
        spos.z = s->z-((s->yrepeat*tilesiz[s->picnum].y)<<1)+(4<<8);
        if (s->picnum != ROTATEGUN)
        {
            spos.z -= (7<<8);
            if (A_CheckEnemySprite(s) && PN != COMMANDER)
            {
                spos.x += (sintable[(sa+1024+96)&2047]>>7);
                spos.y += (sintable[(sa+512+96)&2047]>>7);
            }
        }
    }

    if (A_CheckSpriteTileFlags(atwith,SPRITE_PROJECTILE))
    {
        /* Custom projectiles.  This is a big hack. */

        if (ProjectileData[atwith].offset == 0) ProjectileData[atwith].offset = 1;

        if (ProjectileData[atwith].workslike & PROJECTILE_BLOOD || ProjectileData[atwith].workslike & PROJECTILE_KNEE)
        {

            if (ProjectileData[atwith].workslike & PROJECTILE_BLOOD)
            {
                if (p >= 0)
                    sa += 64 - (krand()&127);
                else sa += 1024 + 64 - (krand()&127);
                zvel = 1024-(krand()&2047);
            }

            if (ProjectileData[atwith].workslike & PROJECTILE_KNEE)
            {
                if (p >= 0)
                {
                    zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)<<5;
                    spos.z += (6<<8);
                    sa += 15;
                }
                else if (!(ProjectileData[atwith].workslike & PROJECTILE_NOAIM))
                {
                    j = g_player[A_FindPlayer(s,&x)].ps->i;
                    zvel = ((sprite[j].z - spos.z)<<8) / (x+1);
                    sa = getangle(sprite[j].x- spos.x,sprite[j].y - spos.y);
                }
            }

            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],zvel<<6,
                    &hit,CLIPMASK1);

            if (ProjectileData[atwith].workslike & PROJECTILE_BLOOD)
            {
                if (ProjectileData[atwith].range == 0)
                    ProjectileData[atwith].range = 1024;

                if (FindDistance2D(spos.x-hit.pos.x, spos.y-hit.pos.y) < ProjectileData[atwith].range)
                    if (FindDistance2D(wall[hit.wall].x-wall[wall[hit.wall].point2].x,wall[hit.wall].y-wall[wall[hit.wall].point2].y) > (mulscale(ProjectileData[atwith].xrepeat+8,tilesiz[ProjectileData[atwith].decal].x,3)))
                        if (hit.wall >= 0 && wall[hit.wall].overpicnum != BIGFORCE)
                            if ((wall[hit.wall].nextsector >= 0 && hit.sect >= 0 &&
                                    sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT &&
                                    sector[hit.sect].lotag == ST_0_NO_EFFECT &&
                                    sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT &&
                                    (sector[hit.sect].floorz-sector[wall[hit.wall].nextsector].floorz) > (mulscale(ProjectileData[atwith].yrepeat,tilesiz[ProjectileData[atwith].decal].y,3)<<8)) ||
                                    (wall[hit.wall].nextsector == -1 && sector[hit.sect].lotag == ST_0_NO_EFFECT))
                                if ((wall[hit.wall].cstat&16) == 0)
                                {
                                    if (wall[hit.wall].nextsector >= 0)
                                    {
                                        k = headspritesect[wall[hit.wall].nextsector];
                                        while (k >= 0)
                                        {
                                            if (sprite[k].statnum == STAT_EFFECTOR && sprite[k].lotag == SE_13_EXPLOSIVE)
                                                return -1;
                                            k = nextspritesect[k];
                                        }
                                    }

                                    if (wall[hit.wall].nextwall >= 0 &&
                                            wall[wall[hit.wall].nextwall].hitag != 0)
                                        return -1;

                                    if (wall[hit.wall].hitag == 0)
                                    {
                                        if (ProjectileData[atwith].decal >= 0)
                                        {
                                            k = A_Spawn(i,ProjectileData[atwith].decal);

                                            if (!(SpriteFlags[ProjectileData[atwith].decal] & SPRITE_DECAL))
                                                SpriteFlags[ProjectileData[atwith].decal] |= SPRITE_DECAL;

                                            k = A_Spawn(i,ProjectileData[atwith].decal);
                                            sprite[k].xvel = -1;
                                            sprite[k].ang = getangle(wall[hit.wall].x-wall[wall[hit.wall].point2].x,
                                                                     wall[hit.wall].y-wall[wall[hit.wall].point2].y)+512;
                                            sprite[k].x = hit.pos.x;
                                            sprite[k].y = hit.pos.y;
                                            sprite[k].z = hit.pos.z;
                                            if (ProjectileData[atwith].workslike & PROJECTILE_RANDDECALSIZE)
                                            {
                                                wh = (krand()&ProjectileData[atwith].xrepeat);
                                                if (wh < ProjectileData[atwith].yrepeat)
                                                    wh = ProjectileData[atwith].yrepeat;
                                                sprite[k].xrepeat = wh;
                                                sprite[k].yrepeat = wh;
                                            }
                                            else
                                            {
                                                sprite[k].xrepeat = ProjectileData[atwith].xrepeat;
                                                sprite[k].yrepeat = ProjectileData[atwith].yrepeat;
                                            }
                                            sprite[k].z += sprite[k].yrepeat<<8;
                                            //                                        sprite[k].cstat = 16+(krand()&12);
                                            sprite[k].cstat = 16;

                                            wh = (krand()&1);
                                            if (wh == 1)
                                                sprite[k].cstat |= 4;

                                            wh = (krand()&1);
                                            if (wh == 1)
                                                sprite[k].cstat |= 8;

                                            wh = sprite[k].sectnum;
                                            sprite[k].shade = sector[wh].floorshade;
                                            sprite[k].x -= mulscale13(1,sintable[(sprite[k].ang+2560)&2047]);
                                            sprite[k].y -= mulscale13(1,sintable[(sprite[k].ang+2048)&2047]);

                                            A_SetSprite(k,CLIPMASK0);
                                            A_AddToDeleteQueue(k);
                                            changespritestat(k,5);

                                        }
                                        //                                    if( PN == OOZFILTER || PN == NEWBEAST )
                                        //                                        sprite[k].pal = 6;
                                    }
                                }
                return -1;
            }

            if (hit.sect < 0) return -1;

            if ((ProjectileData[atwith].range == 0) && (ProjectileData[atwith].workslike & PROJECTILE_KNEE))
                ProjectileData[atwith].range = 1024;

            if ((ProjectileData[atwith].range > 0) && ((klabs(spos.x-hit.pos.x)+klabs(spos.y-hit.pos.y)) > ProjectileData[atwith].range))
                return -1;
            else
            {
                if (hit.wall >= 0 || hit.sprite >= 0)
                {
                    j = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,atwith,-15,0,0,sa,32,0,i,4);
                    ActorExtra[j].projectile.workslike = ProjectileData[sprite[j].picnum].workslike;
                    sprite[j].extra = ProjectileData[atwith].extra;
                    if (ProjectileData[atwith].extra_rand > 0)
                        sprite[j].extra += (krand()&ProjectileData[atwith].extra_rand);
                    if (p >= 0)
                    {
                        if (ProjectileData[atwith].spawns >= 0)
                        {
                            k = A_Spawn(j,ProjectileData[atwith].spawns);
                            sprite[k].z -= (8<<8);
                            ActorExtra[k].temp_data[6] = hit.wall;
                            ActorExtra[k].temp_data[7] = hit.sect;
                            ActorExtra[k].temp_data[8] = hit.sprite;
                        }
                        if (ProjectileData[atwith].sound >= 0) A_PlaySound(ProjectileData[atwith].sound,j);
                    }

                    if (p >= 0 && g_player[p].ps->steroids_amount > 0 && g_player[p].ps->steroids_amount < 400)
                        sprite[j].extra += (g_player[p].ps->max_player_health>>2);

                    if (hit.sprite >= 0 && sprite[hit.sprite].picnum != ACCESSSWITCH && sprite[hit.sprite].picnum != ACCESSSWITCH2)
                    {
                        A_DamageObject(hit.sprite,j);
                        if (p >= 0) P_ActivateSwitch(p,hit.sprite,1);
                    }

                    else if (hit.wall >= 0)
                    {
                        if (wall[hit.wall].cstat&2)
                            if (wall[hit.wall].nextsector >= 0)
                                if (hit.pos.z >= (sector[wall[hit.wall].nextsector].floorz))
                                    hit.wall = wall[hit.wall].nextwall;

                        if (hit.wall >= 0 && wall[hit.wall].picnum != ACCESSSWITCH && wall[hit.wall].picnum != ACCESSSWITCH2)
                        {
                            A_DamageWall(j,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,atwith);
                            if (p >= 0) P_ActivateSwitch(p,hit.wall,0);
                        }
                    }
                }
                else if (p >= 0 && zvel > 0 && sector[hit.sect].lotag == ST_1_ABOVE_WATER)
                {
                    j = A_Spawn(g_player[p].ps->i,WATERSPLASH2);
                    sprite[j].x = hit.pos.x;
                    sprite[j].y = hit.pos.y;
                    sprite[j].ang = fix16_to_int(g_player[p].ps->q16ang); // Total tweek
                    sprite[j].xvel = 32;
                    A_SetSprite(i,CLIPMASK0);
                    sprite[j].xvel = 0;

                }
            }
            return -1;
        }

        if (ProjectileData[atwith].workslike & PROJECTILE_HITSCAN)
        {
            if (s->extra >= 0) s->shade = ProjectileData[atwith].shade;

            if (p >= 0)
            {
                int angRange=32;
                int zRange=256;

                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }
                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)+(5<<8);
                    if (((sprite[j].picnum >= GREENSLIME)&&(sprite[j].picnum <= GREENSLIME+7))||(sprite[j].picnum ==ROTATEGUN))
                    {
                        dal -= (8<<8);
//                        return -1;
                    }
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0) hit.pos.x++;
                    zvel = ((sprite[j].z- spos.z-dal)<<8) / hit.pos.x;
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }

                Gv_SetVar(g_iAngRangeVarID,angRange, i,p);
                Gv_SetVar(g_iZRangeVarID,zRange,i,p);
                X_OnEvent(EVENT_GETSHOTRANGE, i,p, -1);
                angRange=Gv_GetVar(g_iAngRangeVarID,i,p);
                zRange=Gv_GetVar(g_iZRangeVarID,i,p);

                if (ProjectileData[atwith].workslike & PROJECTILE_ACCURATE_AUTOAIM)
                {
                    if (!g_player[p].ps->auto_aim)
                    {
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                        if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
                        hitscan(&spos,sect,sintable[(sa+512)&2047],sintable[sa&2047],
                                zvel<<6,&hit,CLIPMASK1);
                        if (hit.sprite != -1)
                        {
                            if (sprite[hit.sprite].statnum == 1 || sprite[hit.sprite].statnum == 2 || sprite[hit.sprite].statnum == 10 || sprite[hit.sprite].statnum == 13)
                                j = hit.sprite;
                        }
                    }

                    if (j == -1)
                    {
                        sa += (angRange / 2) - (krand() % angRange);
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                        zvel += (zRange / 2) - (krand() % zRange);
                    }
                }
                else
                {
                    sa += (angRange / 2) - (krand() % angRange);
                    if (j == -1)
                    {
                        // no target
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                    }
                    zvel += (zRange / 2) - (krand() % zRange);
                }
                spos.z -= (2<<8);
            }
            else
            {
                j = A_FindPlayer(s,&x);
                spos.z -= (4<<8);
                hit.pos.x = ldist(&sprite[g_player[j].ps->i], s);
                if (hit.pos.x == 0)
                    hit.pos.x++;
                zvel = ((g_player[j].ps->pos.z-spos.z) <<8) / hit.pos.x;
                if (s->picnum != BOSS1)
                {
                    zvel += 128-(krand()&255);
                    sa += 32-(krand()&63);
                }
                else
                {
                    zvel += 128-(krand()&255);
                    sa = getangle(g_player[j].ps->pos.x-spos.x,g_player[j].ps->pos.y-spos.y)+64-(krand()&127);
                }
            }

            if (ProjectileData[atwith].cstat >= 0) s->cstat &= ~ProjectileData[atwith].cstat;
            else s->cstat &= ~257;

            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],
                    zvel<<6,&hit,CLIPMASK1);


            if (ProjectileData[atwith].cstat >= 0) s->cstat |= ProjectileData[atwith].cstat;
            else s->cstat |= 257;

            if (hit.sect < 0) return -1;

            if ((ProjectileData[atwith].range > 0) && ((klabs(spos.x-hit.pos.x)+klabs(spos.y-hit.pos.y)) > ProjectileData[atwith].range)) return -1;

            if (ProjectileData[atwith].trail >= 0)
                A_HitscanProjTrail(spos.x, spos.y, spos.z,hit.pos.x,hit.pos.y,hit.pos.z,sa,atwith);

            if (ProjectileData[atwith].workslike & PROJECTILE_WATERBUBBLES)
            {
                if ((krand()&15) == 0 && sector[hit.sect].lotag == ST_2_UNDERWATER)
                    A_DoWaterTracers(hit.pos.x,hit.pos.y,hit.pos.z, spos.x, spos.y, spos.z,8-(ud.multimode>>1));
            }

            if (p >= 0)
            {
                k = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1,-15,10,10,sa,0,0,i,4);
                sprite[k].extra = ProjectileData[atwith].extra;
                if (ProjectileData[atwith].extra_rand > 0)
                    sprite[k].extra += (krand()%ProjectileData[atwith].extra_rand);
                sprite[k].yvel = atwith; // this is a hack to allow you to detect which weapon spawned a SHOTSPARK1
                ActorExtra[k].temp_data[6] = hit.wall;
                ActorExtra[k].temp_data[7] = hit.sect;
                ActorExtra[k].temp_data[8] = hit.sprite;

                if (hit.wall == -1 && hit.sprite == -1)
                {
                    if (zvel < 0)
                    {
                        if (sector[hit.sect].ceilingstat&1)
                        {
                            sprite[k].xrepeat = 0;
                            sprite[k].yrepeat = 0;
                            return -1;
                        }
                        else
                            Sect_DamageCeiling(hit.sect);
                    }
                    if (ProjectileData[atwith].spawns >= 0)
                    {
                        wh=A_Spawn(k,ProjectileData[atwith].spawns);
                        if (ProjectileData[atwith].sxrepeat > 4) sprite[wh].xrepeat=ProjectileData[atwith].sxrepeat;
                        if (ProjectileData[atwith].syrepeat > 4) sprite[wh].yrepeat=ProjectileData[atwith].syrepeat;
                        ActorExtra[wh].temp_data[6] = hit.wall;
                        ActorExtra[wh].temp_data[7] = hit.sect;
                        ActorExtra[wh].temp_data[8] = hit.sprite;
                    }
                }

                if (hit.sprite >= 0)
                {
                    A_DamageObject(hit.sprite,k);
                    if (sprite[hit.sprite].picnum == APLAYER && !FFIRE_CHECK(sprite[hit.sprite].yvel, sprite[i].yvel))
                    {
                        l = A_Spawn(k,JIBS6);
                        sprite[k].xrepeat = sprite[k].yrepeat = 0;
                        sprite[l].z += (4<<8);
                        sprite[l].xvel = 16;
                        sprite[l].xrepeat = sprite[l].yrepeat = 24;
                        sprite[l].ang += 64-(krand()&127);
                    }
                    else
                    {
                        if (ProjectileData[atwith].spawns >= 0)
                        {
                            wh=A_Spawn(k,ProjectileData[atwith].spawns);
                            if (ProjectileData[atwith].sxrepeat > 4) sprite[wh].xrepeat=ProjectileData[atwith].sxrepeat;
                            if (ProjectileData[atwith].syrepeat > 4) sprite[wh].yrepeat=ProjectileData[atwith].syrepeat;
                            ActorExtra[wh].temp_data[6] = hit.wall;
                            ActorExtra[wh].temp_data[7] = hit.sect;
                            ActorExtra[wh].temp_data[8] = hit.sprite;
                        }
                    }
                    if (p >= 0 && (
                                sprite[hit.sprite].picnum == DIPSWITCH ||
                                sprite[hit.sprite].picnum == DIPSWITCH+1 ||
                                sprite[hit.sprite].picnum == DIPSWITCH2 ||
                                sprite[hit.sprite].picnum == DIPSWITCH2+1 ||
                                sprite[hit.sprite].picnum == DIPSWITCH3 ||
                                sprite[hit.sprite].picnum == DIPSWITCH3+1 ||
                                sprite[hit.sprite].picnum == HANDSWITCH ||
                                sprite[hit.sprite].picnum == HANDSWITCH+1))
                    {
                        P_ActivateSwitch(p,hit.sprite,1);
                        return -1;
                    }
                }
                else if (hit.wall >= 0)
                {
                    if (ProjectileData[atwith].spawns >= 0)
                    {
                        wh=A_Spawn(k,ProjectileData[atwith].spawns);
                        if (ProjectileData[atwith].sxrepeat > 4) sprite[wh].xrepeat=ProjectileData[atwith].sxrepeat;
                        if (ProjectileData[atwith].syrepeat > 4) sprite[wh].yrepeat=ProjectileData[atwith].syrepeat;
                        ActorExtra[wh].temp_data[6] = hit.wall;
                        ActorExtra[wh].temp_data[7] = hit.sect;
                        ActorExtra[wh].temp_data[8] = hit.sprite;
                    }
                    if (CheckDoorTile(wall[hit.wall].picnum) == 1)
                        goto DOSKIPBULLETHOLE;
                    if (p >= 0 && (
                                wall[hit.wall].picnum == DIPSWITCH ||
                                wall[hit.wall].picnum == DIPSWITCH+1 ||
                                wall[hit.wall].picnum == DIPSWITCH2 ||
                                wall[hit.wall].picnum == DIPSWITCH2+1 ||
                                wall[hit.wall].picnum == DIPSWITCH3 ||
                                wall[hit.wall].picnum == DIPSWITCH3+1 ||
                                wall[hit.wall].picnum == HANDSWITCH ||
                                wall[hit.wall].picnum == HANDSWITCH+1))
                    {
                        P_ActivateSwitch(p,hit.wall,0);
                        return -1;
                    }

                    if (wall[hit.wall].hitag != 0 || (wall[hit.wall].nextwall >= 0 && wall[wall[hit.wall].nextwall].hitag != 0))
                        goto DOSKIPBULLETHOLE;

                    if (hit.sect >= 0 && sector[hit.sect].lotag == ST_0_NO_EFFECT)
                        if (wall[hit.wall].overpicnum != BIGFORCE)
                            if ((wall[hit.wall].nextsector >= 0 && sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT) ||
                                    (wall[hit.wall].nextsector == -1 && sector[hit.sect].lotag == ST_0_NO_EFFECT))
                                if ((wall[hit.wall].cstat&16) == 0)
                                {
                                    if (wall[hit.wall].nextsector >= 0)
                                    {
                                        l = headspritesect[wall[hit.wall].nextsector];
                                        while (l >= 0)
                                        {
                                            if (sprite[l].statnum == STAT_EFFECTOR && sprite[l].lotag == SE_13_EXPLOSIVE)
                                                goto DOSKIPBULLETHOLE;
                                            l = nextspritesect[l];
                                        }
                                    }

                                    l = headspritestat[STAT_MISC];
                                    while (l >= 0)
                                    {
                                        if (sprite[l].picnum == ProjectileData[atwith].decal)
                                            if (dist(&sprite[l],&sprite[k]) < (12+(krand()&7)))
                                                goto DOSKIPBULLETHOLE;
                                        l = nextspritestat[l];
                                    }
                                    if (ProjectileData[atwith].decal >= 0)
                                    {
                                        l = A_Spawn(k,ProjectileData[atwith].decal);
                                        sprite[l].xvel = -1;
                                        sprite[l].ang = getangle(wall[hit.wall].x-wall[wall[hit.wall].point2].x,
                                                                 wall[hit.wall].y-wall[wall[hit.wall].point2].y)+512;
                                        if (ProjectileData[atwith].workslike & PROJECTILE_RANDDECALSIZE)
                                        {
                                            wh = (krand()&ProjectileData[atwith].xrepeat);
                                            if (wh < ProjectileData[atwith].yrepeat)
                                                wh = ProjectileData[atwith].yrepeat;
                                            sprite[l].xrepeat = wh;
                                            sprite[l].yrepeat = wh;
                                        }
                                        else
                                        {
                                            sprite[l].xrepeat = ProjectileData[atwith].xrepeat;
                                            sprite[l].yrepeat = ProjectileData[atwith].yrepeat;
                                        }
                                        sprite[l].cstat = 16+(krand()&12);
                                        sprite[l].x -= mulscale13(1,sintable[(sprite[l].ang+2560)&2047]);
                                        sprite[l].y -= mulscale13(1,sintable[(sprite[l].ang+2048)&2047]);

                                        A_SetSprite(l,CLIPMASK0);
                                        A_AddToDeleteQueue(l);
                                    }
                                }

DOSKIPBULLETHOLE:

                    if (wall[hit.wall].cstat&2)
                        if (wall[hit.wall].nextsector >= 0)
                            if (hit.pos.z >= (sector[wall[hit.wall].nextsector].floorz))
                                hit.wall = wall[hit.wall].nextwall;

                    A_DamageWall(k,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,atwith);
                }
            }
            else
            {
                k = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1,-15,24,24,sa,0,0,i,4);
                sprite[k].extra = ProjectileData[atwith].extra;
                if (ProjectileData[atwith].extra_rand > 0)
                    sprite[k].extra += (krand()%ProjectileData[atwith].extra_rand);
                sprite[k].yvel = atwith; // this is a hack to allow you to detect which weapon spawned a SHOTSPARK1
                ActorExtra[k].temp_data[6] = hit.wall;
                ActorExtra[k].temp_data[7] = hit.sect;
                ActorExtra[k].temp_data[8] = hit.sprite;

                if (hit.sprite >= 0)
                {
                    A_DamageObject(hit.sprite,k);
                    if (sprite[hit.sprite].picnum != APLAYER)
                    {
                        if (ProjectileData[atwith].spawns >= 0)
                        {
                            wh=A_Spawn(k,ProjectileData[atwith].spawns);
                            if (ProjectileData[atwith].sxrepeat > 4) sprite[wh].xrepeat=ProjectileData[atwith].sxrepeat;
                            if (ProjectileData[atwith].syrepeat > 4) sprite[wh].yrepeat=ProjectileData[atwith].syrepeat;
                            ActorExtra[wh].temp_data[6] = hit.wall;
                            ActorExtra[wh].temp_data[7] = hit.sect;
                            ActorExtra[wh].temp_data[8] = hit.sprite;
                        }
                    }
                    else sprite[k].xrepeat = sprite[k].yrepeat = 0;
                }
                else if (hit.wall >= 0)
                    A_DamageWall(k,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,atwith);
            }

            if ((krand()&255) < 4)
                if (ProjectileData[atwith].isound >= 0)
                    S_PlaySoundXYZ(ProjectileData[atwith].isound,k,hit.pos.x,hit.pos.y,hit.pos.z);

            return -1;
        }

        if (ProjectileData[atwith].workslike & PROJECTILE_RPG)
        {

            /*            if(ProjectileData[atwith].workslike & PROJECTILE_FREEZEBLAST)
                            sz += (3<<8);*/

            if (s->extra >= 0) s->shade = ProjectileData[atwith].shade;

            scount = 1;
            vel = ProjectileData[atwith].vel;

            j = -1;

            if (p >= 0)
            {
                //            j = A_FindTargetSprite( s, AUTO_AIM_ANGLE ); // 48
                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }
                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)+(8<<8);
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);

                    if (hit.pos.x == 0)
                        hit.pos.x++;

                    zvel = ((sprite[j].z-spos.z-dal)*vel) / hit.pos.x;

                    if (sprite[j].picnum != RECON)
                        sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
                //                else zvel = (100-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*81;
                else zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*(ProjectileData[atwith].vel/8);

                if (ProjectileData[atwith].sound >= 0)
                    A_PlaySound(ProjectileData[atwith].sound,i);
            }
            else
            {
                if (!(ProjectileData[atwith].workslike & PROJECTILE_NOAIM))
                {
                    j = A_FindPlayer(s,&x);
                    sa = getangle(g_player[j].ps->opos.x-spos.x,g_player[j].ps->opos.y-spos.y);

                    l = ldist(&sprite[g_player[j].ps->i],s);
                    if (l == 0)
                        l++;
                    zvel = ((g_player[j].ps->opos.z-spos.z)*vel) / l;

                    if (A_CheckEnemySprite(s) && (s->hitag&face_player_smart))
                        sa = s->ang+(krand()&31)-16;
                }
            }



            if (p >= 0 && j >= 0)
                l = j;
            else l = -1;

            /*                        j = A_InsertSprite(sect,
                                            sx+(sintable[(348+sa+512)&2047]/448),
                                            sy+(sintable[(sa+348)&2047]/448),
                                            sz-(1<<8),atwith,0,14,14,sa,vel,zvel,i,4);*/
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            j = A_InsertSprite(sect,
                               spos.x+(sintable[(348+sa+512)&2047]/ProjectileData[atwith].offset),
                               spos.y+(sintable[(sa+348)&2047]/ProjectileData[atwith].offset),
                               spos.z-(1<<8),atwith,0,14,14,sa,vel,zvel,i,4);

            sprite[j].xrepeat=ProjectileData[atwith].xrepeat;
            sprite[j].yrepeat=ProjectileData[atwith].yrepeat;


            if (ProjectileData[atwith].extra_rand > 0)
                sprite[j].extra += (krand()&ProjectileData[atwith].extra_rand);
            if (!(ProjectileData[atwith].workslike & PROJECTILE_BOUNCESOFFWALLS))
                sprite[j].yvel = l;
            else
            {
                if (ProjectileData[atwith].bounces >= 1) sprite[j].yvel = ProjectileData[atwith].bounces;
                else sprite[j].yvel = g_numFreezeBounces;
                //                sprite[j].xrepeat >>= 1;
                //                sprite[j].yrepeat >>= 1;
                sprite[j].zvel -= (2<<4);
            }
            /*
                        if(p == -1)
                        {
                            if(!(ProjectileData[atwith].workslike & PROJECTILE_BOUNCESOFFWALLS))
                            {
                                sprite[j].xrepeat = ProjectileData[atwith].xrepeat; // 30
                                sprite[j].yrepeat = ProjectileData[atwith].yrepeat;
                                sprite[j].extra >>= 2;
                            }
                        }
            */
            if (ProjectileData[atwith].cstat >= 0) sprite[j].cstat = ProjectileData[atwith].cstat;
            else sprite[j].cstat = 128;
            if (ProjectileData[atwith].clipdist >= 0) sprite[j].clipdist = ProjectileData[atwith].clipdist;
            else sprite[j].clipdist = 40;

            Bmemcpy(&ActorExtra[j].projectile, &ProjectileData[sprite[j].picnum], sizeof(ProjectileData[sprite[j].picnum]));

            //            sa = s->ang+32-(krand()&63);
            //            zvel = oldzvel+512-(krand()&1023);

            return j;
        }

    }
    else
    {
        switch (DynamicTileMap[atwith])
        {
        case BLOODSPLAT1__STATIC:
        case BLOODSPLAT2__STATIC:
        case BLOODSPLAT3__STATIC:
        case BLOODSPLAT4__STATIC:

            if (p >= 0)
                sa += 64 - (krand()&127);
            else sa += 1024 + 64 - (krand()&127);
            zvel = 1024-(krand()&2047);
            fallthrough__;

        case KNEE__STATIC:
            if (atwith == KNEE)
            {
                if (p >= 0)
                {
                    zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                    spos.z += (6<<8);
                    sa += 15;
                }
                else
                {
                    j = g_player[A_FindPlayer(s,&x)].ps->i;
                    zvel = ((sprite[j].z-spos.z)<<8) / (x+1);
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
            }

            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],zvel<<6,
                    &hit,CLIPMASK1);

            if (atwith == BLOODSPLAT1 || atwith == BLOODSPLAT2 || atwith == BLOODSPLAT3 || atwith == BLOODSPLAT4)
            {
                if (FindDistance2D(spos.x-hit.pos.x,spos.y-hit.pos.y) < 1024)
                    if (hit.wall >= 0 && wall[hit.wall].overpicnum != BIGFORCE)
                        if ((wall[hit.wall].nextsector >= 0 && hit.sect >= 0 &&
                                sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT &&
                                sector[hit.sect].lotag == ST_0_NO_EFFECT &&
                                sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT &&
                                (sector[hit.sect].floorz-sector[wall[hit.wall].nextsector].floorz) > (16<<8)) ||
                                (wall[hit.wall].nextsector == -1 && sector[hit.sect].lotag == ST_0_NO_EFFECT))
                            if ((wall[hit.wall].cstat&16) == 0)
                            {
                                if (wall[hit.wall].nextsector >= 0)
                                {
                                    k = headspritesect[wall[hit.wall].nextsector];
                                    while (k >= 0)
                                    {
                                        if (sprite[k].statnum == STAT_EFFECTOR && sprite[k].lotag == SE_13_EXPLOSIVE)
                                            return -1;
                                        k = nextspritesect[k];
                                    }
                                }

                                if (wall[hit.wall].nextwall >= 0 &&
                                        wall[wall[hit.wall].nextwall].hitag != 0)
                                    return -1;

                                if (wall[hit.wall].hitag == 0)
                                {
                                    k = A_Spawn(i,atwith);
                                    sprite[k].xvel = -12;
                                    sprite[k].ang = getangle(wall[hit.wall].x-wall[wall[hit.wall].point2].x,
                                                             wall[hit.wall].y-wall[wall[hit.wall].point2].y)+512;
                                    sprite[k].x = hit.pos.x;
                                    sprite[k].y = hit.pos.y;
                                    sprite[k].z = hit.pos.z;
                                    sprite[k].cstat |= (krand()&4);
                                    A_SetSprite(k,CLIPMASK0);
                                    setspritez_old(k,sprite[k].x,sprite[k].y,sprite[k].z);
                                    if (PN == OOZFILTER || PN == NEWBEAST)
                                        sprite[k].pal = 6;
                                }
                            }
                return -1;
            }

            if (hit.sect < 0) break;

            if ((klabs(spos.x-hit.pos.x)+klabs(spos.y-hit.pos.y)) < 1024)
            {
                if (hit.wall >= 0 || hit.sprite >= 0)
                {
                    j = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,KNEE,-15,0,0,sa,32,0,i,4);
                    sprite[j].extra += (krand()&7);
                    if (p >= 0)
                    {
                        k = A_Spawn(j,SMALLSMOKE);
                        sprite[k].z -= (8<<8);
                        A_PlaySound(KICK_HIT,j);
                        ActorExtra[k].temp_data[6] = hit.wall;
                        ActorExtra[k].temp_data[7] = hit.sect;
                        ActorExtra[k].temp_data[8] = hit.sprite;
                    }

                    if (p >= 0 && g_player[p].ps->steroids_amount > 0 && g_player[p].ps->steroids_amount < 400)
                        sprite[j].extra += (g_player[p].ps->max_player_health>>2);

                    if (hit.sprite >= 0 && sprite[hit.sprite].picnum != ACCESSSWITCH && sprite[hit.sprite].picnum != ACCESSSWITCH2)
                    {
                        A_DamageObject(hit.sprite,j);
                        if (p >= 0) P_ActivateSwitch(p,hit.sprite,1);
                    }

                    else if (hit.wall >= 0)
                    {
                        if (wall[hit.wall].cstat&2)
                            if (wall[hit.wall].nextsector >= 0)
                                if (hit.pos.z >= (sector[wall[hit.wall].nextsector].floorz))
                                    hit.wall = wall[hit.wall].nextwall;

                        if (hit.wall >= 0 && wall[hit.wall].picnum != ACCESSSWITCH && wall[hit.wall].picnum != ACCESSSWITCH2)
                        {
                            A_DamageWall(j,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,atwith);
                            if (p >= 0) P_ActivateSwitch(p,hit.wall,0);
                        }
                    }
                }
                else if (p >= 0 && zvel > 0 && sector[hit.sect].lotag == ST_1_ABOVE_WATER)
                {
                    j = A_Spawn(g_player[p].ps->i,WATERSPLASH2);
                    sprite[j].x = hit.pos.x;
                    sprite[j].y = hit.pos.y;
                    sprite[j].ang = fix16_to_int(g_player[p].ps->q16ang); // Total tweek
                    sprite[j].xvel = 32;
                    A_SetSprite(i,CLIPMASK0);
                    sprite[j].xvel = 0;

                }
            }

            break;

        case SHOTSPARK1__STATIC:
        case SHOTGUN__STATIC:
        case CHAINGUN__STATIC:

            if (s->extra >= 0) s->shade = -96;

            if (p >= 0)
            {
                int angRange=32;
                int zRange=256;

                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }
                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)+(5<<8);
                    if (((sprite[j].picnum>=GREENSLIME)&&(sprite[j].picnum<=GREENSLIME+7))||(sprite[j].picnum==ROTATEGUN))
                    {

                        dal -= (8<<8);

                    }
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0) hit.pos.x++;
                    zvel = ((sprite[j].z-spos.z-dal)<<8) / hit.pos.x;
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }

                Gv_SetVar(g_iAngRangeVarID,angRange, i,p);
                Gv_SetVar(g_iZRangeVarID,zRange,i,p);

                X_OnEvent(EVENT_GETSHOTRANGE, i,p, -1);

                angRange=Gv_GetVar(g_iAngRangeVarID,i,p);
                zRange=Gv_GetVar(g_iZRangeVarID,i,p);

                if (atwith == SHOTSPARK1__STATIC && !WW2GI && !NAM)
                {
                    if (!g_player[p].ps->auto_aim)
                    {
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                        if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
                        hitscan(&spos,sect,sintable[(sa+512)&2047],sintable[sa&2047],
                                zvel<<6,&hit,CLIPMASK1);
                        if (hit.sprite != -1)
                        {
                            if (sprite[hit.sprite].statnum == 1 || sprite[hit.sprite].statnum == 2 || sprite[hit.sprite].statnum == 10 || sprite[hit.sprite].statnum == 13)
                                j = hit.sprite;
                        }
                    }

                    if (j == -1)
                    {
                        sa += (angRange / 2) - (krand() % angRange);
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                        zvel += (zRange / 2) - (krand() % zRange);
                    }
                }
                else
                {
                    sa += (angRange / 2) - (krand() % angRange);
                    if (j == -1)
                    {
                        // no target
                        zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                    }
                    zvel += (zRange / 2) - (krand() % zRange);
                }

                spos.z -= (2<<8);
            }
            else
            {
                j = A_FindPlayer(s,&x);
                spos.z -= (4<<8);
                hit.pos.x = ldist(&sprite[g_player[j].ps->i], s);
                if (hit.pos.x == 0)
                    hit.pos.x++;
                zvel = ((g_player[j].ps->pos.z-spos.z) <<8) / hit.pos.x;
                if (s->picnum != BOSS1)
                {
                    zvel += 128-(krand()&255);
                    sa += 32-(krand()&63);
                }
                else
                {
                    zvel += 128-(krand()&255);
                    sa = getangle(g_player[j].ps->pos.x-spos.x,g_player[j].ps->pos.y-spos.y)+64-(krand()&127);
                }
            }

            s->cstat &= ~257;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],
                    zvel<<6,&hit,CLIPMASK1);
            s->cstat |= 257;

            if (hit.sect < 0) return -1;

            if ((krand()&15) == 0 && sector[hit.sect].lotag == ST_2_UNDERWATER)
                A_DoWaterTracers(hit.pos.x,hit.pos.y,hit.pos.z, spos.x, spos.y, spos.z,8-(ud.multimode>>1));

            if (p >= 0)
            {
                k = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1,-15,10,10,sa,0,0,i,4);
                sprite[k].extra = *actorscrptr[atwith];
                sprite[k].extra += (krand()%6);
                sprite[k].yvel = atwith; // this is a hack to allow you to detect which weapon spawned a SHOTSPARK1
                ActorExtra[k].temp_data[6] = hit.wall;
                ActorExtra[k].temp_data[7] = hit.sect;
                ActorExtra[k].temp_data[8] = hit.sprite;


                if (hit.wall == -1 && hit.sprite == -1)
                {
                    if (zvel < 0)
                    {
                        if (sector[hit.sect].ceilingstat&1)
                        {
                            sprite[k].xrepeat = 0;
                            sprite[k].yrepeat = 0;
                            return -1;
                        }
                        else
                            Sect_DamageCeiling(hit.sect);
                    }
                    l = A_Spawn(k,SMALLSMOKE);
                    ActorExtra[l].temp_data[6] = hit.wall;
                    ActorExtra[l].temp_data[7] = hit.sect;
                    ActorExtra[l].temp_data[8] = hit.sprite;
                }

                if (hit.sprite >= 0)
                {
                    A_DamageObject(hit.sprite,k);
                    if (sprite[hit.sprite].picnum == APLAYER && !FFIRE_CHECK(sprite[hit.sprite].yvel, sprite[i].yvel))
                    {
                        l = A_Spawn(k,JIBS6);
                        sprite[k].xrepeat = sprite[k].yrepeat = 0;
                        sprite[l].z += (4<<8);
                        sprite[l].xvel = 16;
                        sprite[l].xrepeat = sprite[l].yrepeat = 24;
                        sprite[l].ang += 64-(krand()&127);
                    }
                    else
                    {
                        l = A_Spawn(k,SMALLSMOKE);
                        ActorExtra[l].temp_data[6] = hit.wall;
                        ActorExtra[l].temp_data[7] = hit.sect;
                        ActorExtra[l].temp_data[8] = hit.sprite;
                    }

                    if (p >= 0 && (
                                sprite[hit.sprite].picnum == DIPSWITCH ||
                                sprite[hit.sprite].picnum == DIPSWITCH+1 ||
                                sprite[hit.sprite].picnum == DIPSWITCH2 ||
                                sprite[hit.sprite].picnum == DIPSWITCH2+1 ||
                                sprite[hit.sprite].picnum == DIPSWITCH3 ||
                                sprite[hit.sprite].picnum == DIPSWITCH3+1 ||
                                sprite[hit.sprite].picnum == HANDSWITCH ||
                                sprite[hit.sprite].picnum == HANDSWITCH+1))
                    {
                        P_ActivateSwitch(p,hit.sprite,1);
                        return -1;
                    }
                }
                else if (hit.wall >= 0)
                {
                    l = A_Spawn(k,SMALLSMOKE);
                    ActorExtra[l].temp_data[6] = hit.wall;
                    ActorExtra[l].temp_data[7] = hit.sect;
                    ActorExtra[l].temp_data[8] = hit.sprite;

                    if (CheckDoorTile(wall[hit.wall].picnum) == 1)
                        goto SKIPBULLETHOLE;
                    if (p >= 0 && (
                                wall[hit.wall].picnum == DIPSWITCH ||
                                wall[hit.wall].picnum == DIPSWITCH+1 ||
                                wall[hit.wall].picnum == DIPSWITCH2 ||
                                wall[hit.wall].picnum == DIPSWITCH2+1 ||
                                wall[hit.wall].picnum == DIPSWITCH3 ||
                                wall[hit.wall].picnum == DIPSWITCH3+1 ||
                                wall[hit.wall].picnum == HANDSWITCH ||
                                wall[hit.wall].picnum == HANDSWITCH+1))
                    {
                        P_ActivateSwitch(p,hit.wall,0);
                        return -1;
                    }

                    if (wall[hit.wall].hitag != 0 || (wall[hit.wall].nextwall >= 0 && wall[wall[hit.wall].nextwall].hitag != 0))
                        goto SKIPBULLETHOLE;

                    if (hit.sect >= 0 && sector[hit.sect].lotag == ST_0_NO_EFFECT)
                        if (wall[hit.wall].overpicnum != BIGFORCE)
                            if ((wall[hit.wall].nextsector >= 0 && sector[wall[hit.wall].nextsector].lotag == ST_0_NO_EFFECT) ||
                                    (wall[hit.wall].nextsector == -1 && sector[hit.sect].lotag == ST_0_NO_EFFECT))
                                if ((wall[hit.wall].cstat&16) == 0)
                                {
                                    if (wall[hit.wall].nextsector >= 0)
                                    {
                                        l = headspritesect[wall[hit.wall].nextsector];
                                        while (l >= 0)
                                        {
                                            if (sprite[l].statnum == STAT_EFFECTOR && sprite[l].lotag == SE_13_EXPLOSIVE)
                                                goto SKIPBULLETHOLE;
                                            l = nextspritesect[l];
                                        }
                                    }

                                    l = headspritestat[STAT_MISC];
                                    while (l >= 0)
                                    {
                                        if (sprite[l].picnum == BULLETHOLE)
                                            if (dist(&sprite[l],&sprite[k]) < (12+(krand()&7)))
                                                goto SKIPBULLETHOLE;
                                        l = nextspritestat[l];
                                    }
                                    l = A_Spawn(k,BULLETHOLE);
                                    sprite[l].xvel = -1;
                                    sprite[l].x = hit.pos.x;
                                    sprite[l].y = hit.pos.y;
                                    sprite[l].z = hit.pos.z;

                                    sprite[l].ang = getangle(wall[hit.wall].x-wall[wall[hit.wall].point2].x,
                                                             wall[hit.wall].y-wall[wall[hit.wall].point2].y)+512;

                                    sprite[l].x -= mulscale13(1,sintable[(sprite[l].ang+2560)&2047]);
                                    sprite[l].y -= mulscale13(1,sintable[(sprite[l].ang+2048)&2047]);
                                    A_SetSprite(l,CLIPMASK0);
                                }

SKIPBULLETHOLE:

                    if (wall[hit.wall].cstat&2)
                        if (wall[hit.wall].nextsector >= 0)
                            if (hit.pos.z >= (sector[wall[hit.wall].nextsector].floorz))
                                hit.wall = wall[hit.wall].nextwall;

                    A_DamageWall(k,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1);
                }
            }
            else
            {
                k = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1,-15,24,24,sa,0,0,i,4);
                sprite[k].extra = *actorscrptr[atwith];
                sprite[k].yvel = atwith; // this is a hack to allow you to detect which weapon spawned a SHOTSPARK1
                ActorExtra[k].temp_data[6] = hit.wall;
                ActorExtra[k].temp_data[7] = hit.sect;
                ActorExtra[k].temp_data[8] = hit.sprite;

                if (hit.sprite >= 0)
                {
                    A_DamageObject(hit.sprite,k);
                    if (sprite[hit.sprite].picnum != APLAYER)
                    {
                        l = A_Spawn(k,SMALLSMOKE);
                        ActorExtra[l].temp_data[6] = hit.wall;
                        ActorExtra[l].temp_data[7] = hit.sect;
                        ActorExtra[l].temp_data[8] = hit.sprite;
                    }
                    else sprite[k].xrepeat = sprite[k].yrepeat = 0;
                }
                else if (hit.wall >= 0)
                    A_DamageWall(k,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,SHOTSPARK1);
            }

            if ((krand()&255) < 4)
                S_PlaySoundXYZ(PISTOL_RICOCHET,k,hit.pos.x,hit.pos.y,hit.pos.z);

            return -1;

        case FIRELASER__STATIC:
        case SPIT__STATIC:
        case COOLEXPLOSION1__STATIC:

            if (s->extra >= 0) s->shade = -96;

            scount = 1;
            if (atwith == SPIT) vel = 292;
            else
            {
                if (atwith == COOLEXPLOSION1)
                {
                    if (s->picnum == BOSS2) vel = 644;
                    else vel = 348;
                    spos.z -= (4<<7);
                }
                else
                {
                    vel = 840;
                    spos.z -= (4<<7);
                }
            }

            if (p >= 0)
            {
                //            j = A_FindTargetSprite( s, AUTO_AIM_ANGLE );
                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }

                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)-(12<<8);
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0) hit.pos.x++;
                    zvel = ((sprite[j].z-spos.z-dal)*vel) / hit.pos.x;
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
                else
                    zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*98;
            }
            else
            {
                j = A_FindPlayer(s,&x);
                //                sa = getangle(g_player[j].ps->opos.x-sx,g_player[j].ps->opos.y-sy);
                sa += 16-(krand()&31);
                hit.pos.x = ldist(&sprite[g_player[j].ps->i],s);
                if (hit.pos.x == 0) hit.pos.x++;
                zvel = ((g_player[j].ps->opos.z - spos.z + (3<<8))*vel) / hit.pos.x;
            }
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            oldzvel = zvel;

            if (atwith == SPIT)
            {
                sizx = 18;
                sizy = 18, spos.z -= (10<<8);
            }
            else
            {
                if (atwith == FIRELASER)
                {
                    if (p >= 0)
                    {

                        sizx = 34;
                        sizy = 34;
                    }
                    else
                    {
                        sizx = 18;
                        sizy = 18;
                    }
                }
                else
                {
                    sizx = 18;
                    sizy = 18;
                }
            }

            if (p >= 0) sizx = 7,sizy = 7;

            while (scount > 0)
            {
                j = A_InsertSprite(sect, spos.x, spos.y, spos.z,atwith,-127,sizx,sizy,sa,vel,zvel,i,4);
                sprite[j].extra += (krand()&7);

                if (atwith == COOLEXPLOSION1)
                {
                    sprite[j].shade = 0;
                    if (PN == BOSS2)
                    {
                        l = sprite[j].xvel;
                        sprite[j].xvel = 1024;
                        A_SetSprite(j,CLIPMASK0);
                        sprite[j].xvel = l;
                        sprite[j].ang += 128-(krand()&255);
                    }
                }

                sprite[j].cstat = 128;
                sprite[j].clipdist = 4;

                sa = s->ang+32-(krand()&63);
                zvel = oldzvel+512-(krand()&1023);

                scount--;
            }

            return j;

        case FREEZEBLAST__STATIC:
            spos.z += (3<<8);
            fallthrough__;

        case RPG__STATIC:

            if (s->extra >= 0) s->shade = -96;

            scount = 1;
            vel = 644;

            j = -1;

            if (p >= 0)
            {
                //            j = A_FindTargetSprite( s, AUTO_AIM_ANGLE ); // 48
                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }

                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)+(8<<8);
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0) hit.pos.x++;
                    zvel = ((sprite[j].z-spos.z-dal)*vel) / hit.pos.x;
                    if (sprite[j].picnum != RECON)
                        sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
                else zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*81;
                if (atwith == RPG)
                    A_PlaySound(RPG_SHOOT,i);
            }
            else
            {
                j = A_FindPlayer(s,&x);
                sa = getangle(g_player[j].ps->opos.x-spos.x,g_player[j].ps->opos.y-spos.y);
                if (PN == BOSS3)
                    spos.z -= (32<<8);
                else if (PN == BOSS2)
                {
                    vel += 128;
                    spos.z += 24<<8;
                }

                l = ldist(&sprite[g_player[j].ps->i],s);
                if (l == 0)
                    l++;
                zvel = ((g_player[j].ps->opos.z-spos.z)*vel) / l;

                if (A_CheckEnemySprite(s) && (s->hitag&face_player_smart))
                    sa = s->ang+(krand()&31)-16;
            }

            if (p >= 0 && j >= 0)
                l = j;
            else l = -1;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            j = A_InsertSprite(sect,
                               spos.x+(sintable[(348+sa+512)&2047]/448),
                               spos.y+(sintable[(sa+348)&2047]/448),
                               spos.z-(1<<8),atwith,0,14,14,sa,vel,zvel,i,4);

            sprite[j].extra += (krand()&7);
            if (atwith != FREEZEBLAST)
                sprite[j].yvel = l;
            else
            {
                sprite[j].yvel = g_numFreezeBounces;
                sprite[j].xrepeat >>= 1;
                sprite[j].yrepeat >>= 1;
                sprite[j].zvel -= (2<<4);
            }

            if (p == -1)
            {
                if (PN == BOSS3)
                {
                    if (krand()&1)
                    {
                        sprite[j].x -= sintable[sa&2047]>>6;
                        sprite[j].y -= sintable[(sa+1024+512)&2047]>>6;
                        sprite[j].ang -= 8;
                    }
                    else
                    {
                        sprite[j].x += sintable[sa&2047]>>6;
                        sprite[j].y += sintable[(sa+1024+512)&2047]>>6;
                        sprite[j].ang += 4;
                    }
                    sprite[j].xrepeat = 42;
                    sprite[j].yrepeat = 42;
                }
                else if (PN == BOSS2)
                {
                    sprite[j].x -= sintable[sa&2047]/56;
                    sprite[j].y -= sintable[(sa+1024+512)&2047]/56;
                    sprite[j].ang -= 8+(krand()&255)-128;
                    sprite[j].xrepeat = 24;
                    sprite[j].yrepeat = 24;
                }
                else if (atwith != FREEZEBLAST)
                {
                    sprite[j].xrepeat = 30;
                    sprite[j].yrepeat = 30;
                    sprite[j].extra >>= 2;
                }
            }

            else if (*aplWeaponWorksLike[g_player[p].ps->curr_weapon] == DEVISTATOR_WEAPON)
            {
                sprite[j].extra >>= 2;
                sprite[j].ang += 16-(krand()&31);
                sprite[j].zvel += 256-(krand()&511);

                if (g_player[p].ps->hbomb_hold_delay)
                {
                    sprite[j].x -= sintable[sa&2047]/644;
                    sprite[j].y -= sintable[(sa+1024+512)&2047]/644;
                }
                else
                {
                    sprite[j].x += sintable[sa&2047]>>8;
                    sprite[j].y += sintable[(sa+1024+512)&2047]>>8;
                }
                sprite[j].xrepeat >>= 1;
                sprite[j].yrepeat >>= 1;
            }

            sprite[j].cstat = 128;
            if (atwith == RPG)
                sprite[j].clipdist = 4;
            else
                sprite[j].clipdist = 40;

            return j;

        case HANDHOLDINGLASER__STATIC:

            if (p >= 0)
                zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*32;
            else zvel = 0;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];

            spos.z -= g_player[p].ps->pyoff;

            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],
                    zvel<<6,&hit,CLIPMASK1);

            spos.z += g_player[p].ps->pyoff;

            j = 0;
            if (hit.sprite >= 0) break;

            if (hit.wall >= 0 && hit.sect >= 0)
                if (((hit.pos.x-spos.x)*(hit.pos.x-spos.x)+(hit.pos.y-spos.y)*(hit.pos.y-spos.y)) < (290*290))
                {
                    if (wall[hit.wall].nextsector >= 0)
                    {
                        if (sector[wall[hit.wall].nextsector].lotag <= 2 && sector[hit.sect].lotag <= 2)
                            j = 1;
                    }
                    else if (sector[hit.sect].lotag <= 2)
                        j = 1;
                }

            if (j == 1)
            {
                int lTripBombControl=Gv_GetVarByLabel("TRIPBOMB_CONTROL", TRIPBOMB_TRIPWIRE, g_player[p].ps->i, p);
                k = A_InsertSprite(hit.sect,hit.pos.x,hit.pos.y,hit.pos.z,TRIPBOMB,-16,4,5,sa,0,0,i,6);
                if (lTripBombControl & TRIPBOMB_TIMER)
                {
                    int lLifetime=Gv_GetVarByLabel("STICKYBOMB_LIFETIME", NAM_GRENADE_LIFETIME, g_player[p].ps->i, p);
                    int lLifetimeVar=Gv_GetVarByLabel("STICKYBOMB_LIFETIME_VAR", NAM_GRENADE_LIFETIME_VAR, g_player[p].ps->i, p);
                    // set timer.  blows up when at zero....
                    ActorExtra[k].temp_data[7]=lLifetime
                                               + mulscale(krand(),lLifetimeVar, 14)
                                               - lLifetimeVar;
                    ActorExtra[k].temp_data[6]=1;
                }
                else

                    sprite[k].hitag = k;
                A_PlaySound(LASERTRIP_ONWALL,k);
                sprite[k].xvel = -20;
                A_SetSprite(k,CLIPMASK0);
                sprite[k].cstat = 16;
                ActorExtra[k].temp_data[5] = sprite[k].ang = getangle(wall[hit.wall].x-wall[wall[hit.wall].point2].x,wall[hit.wall].y-wall[wall[hit.wall].point2].y)-512;


            }
            return j?k:-1;

        case BOUNCEMINE__STATIC:
        case MORTER__STATIC:

            if (s->extra >= 0) s->shade = -96;

            j = g_player[A_FindPlayer(s,&x)].ps->i;
            x = ldist(&sprite[j],s);

            zvel = -x>>1;

            if (zvel < -4096)
                zvel = -2048;
            vel = x>>4;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            A_InsertSprite(sect,
                           spos.x+(sintable[(512+sa+512)&2047]>>8),
                           spos.y+(sintable[(sa+512)&2047]>>8),
                           spos.z+(6<<8),atwith,-64,32,32,sa,vel,zvel,i,1);
            break;

        case GROWSPARK__STATIC:

            if (p >= 0)
            {
                //            j = A_FindTargetSprite( s, AUTO_AIM_ANGLE );
                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }

                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1)+(5<<8);
                    if (((sprite[j].picnum >= GREENSLIME)&&(sprite[j].picnum <= GREENSLIME+7))||(sprite[j].picnum ==ROTATEGUN))
                    {
                        dal -= (8<<8);

                    }
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0)
                        hit.pos.x++;
                    zvel = ((sprite[j].z-spos.z-dal)<<8) / hit.pos.x;
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
                else
                {
                    sa += 16-(krand()&31);
                    zvel = fix16_to_int(F16(100) - g_player[p].ps->q16horiz - g_player[p].ps->q16horizoff) << 5;
                    zvel += 128-(krand()&255);
                }

                spos.z -= (2<<8);
            }
            else
            {
                j = A_FindPlayer(s,&x);
                spos.z -= (4<<8);
                hit.pos.x = ldist(&sprite[g_player[j].ps->i], s);
                if (hit.pos.x == 0)
                    hit.pos.x++;
                zvel = ((g_player[j].ps->pos.z-spos.z) <<8) / hit.pos.x;
                zvel += 128-(krand()&255);
                sa += 32-(krand()&63);
            }

            k = 0;

            //            RESHOOTGROW:
            if (sect < 0) break;

            s->cstat &= ~257;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            hitscan(&spos,sect,
                    sintable[(sa+512)&2047],
                    sintable[sa&2047],
                    zvel<<6,&hit,CLIPMASK1);

            s->cstat |= 257;

            j = A_InsertSprite(sect,hit.pos.x,hit.pos.y,hit.pos.z,GROWSPARK,-16,28,28,sa,0,0,i,1);

            sprite[j].pal = 2;
            sprite[j].cstat |= 130;
            sprite[j].xrepeat = sprite[j].yrepeat = 1;

            if (hit.wall == -1 && hit.sprite == -1 && hit.sect >= 0)
            {
                if (zvel < 0 && (sector[hit.sect].ceilingstat&1) == 0)
                    Sect_DamageCeiling(hit.sect);
            }
            else if (hit.sprite >= 0) A_DamageObject(hit.sprite,j);
            else if (hit.wall >= 0 && wall[hit.wall].picnum != ACCESSSWITCH && wall[hit.wall].picnum != ACCESSSWITCH2)
            {
                /*    if(wall[hit.wall].overpicnum == MIRROR && k == 0)
                    {
                        l = getangle(
                            wall[wall[hit.wall].point2].x-wall[hit.wall].x,
                            wall[wall[hit.wall].point2].y-wall[hit.wall].y);

                        sx = hitx;
                        sy = hity;
                        sz = hitz;
                        sect = hitsect;
                        sa = ((l<<1) - sa)&2047;
                        sx += sintable[(sa+512)&2047]>>12;
                        sy += sintable[sa&2047]>>12;

                        k++;
                        goto RESHOOTGROW;
                    }
                    else */
                A_DamageWall(j,hit.wall,hit.pos.x,hit.pos.y,hit.pos.z,atwith);
            }

            break;

        case SHRINKER__STATIC:
            if (s->extra >= 0) s->shade = -96;
            if (p >= 0)
            {
                //            j = A_FindTargetSprite( s, AUTO_AIM_ANGLE );
                Gv_SetVar(g_iAimAngleVarID,AUTO_AIM_ANGLE,i,p);
                X_OnEvent(EVENT_GETAUTOAIMANGLE, i, p, -1);
                j=-1;
                if (Gv_GetVar(g_iAimAngleVarID,i,p) > 0)
                {
                    j = A_FindTargetSprite(s, Gv_GetVar(g_iAimAngleVarID,i,p),atwith);
                }

                if (j >= 0)
                {
                    dal = ((sprite[j].yrepeat*tilesiz[sprite[j].picnum].y)<<1);
                    hit.pos.x = ldist(&sprite[g_player[p].ps->i], &sprite[j]);
                    if (hit.pos.x == 0)
                        hit.pos.x++;
                    zvel = ((sprite[j].z-spos.z-dal-(4<<8))*768) / hit.pos.x;
                    sa = getangle(sprite[j].x-spos.x,sprite[j].y-spos.y);
                }
                else zvel = fix16_to_int(F16(100)-g_player[p].ps->q16horiz-g_player[p].ps->q16horizoff)*98;
            }
            else if (s->statnum != 3)
            {
                j = A_FindPlayer(s,&x);
                l = ldist(&sprite[g_player[j].ps->i],s);
                if (l == 0)
                    l++;
                zvel = ((g_player[j].ps->opos.z-spos.z)*512) / l ;
            }
            else zvel = 0;
            if (ActorExtra[i].temp_data[9]) zvel = ActorExtra[i].temp_data[9];
            j = A_InsertSprite(sect,
                               spos.x+(sintable[(512+sa+512)&2047]>>12),
                               spos.y+(sintable[(sa+512)&2047]>>12),
                               spos.z+(2<<8),SHRINKSPARK,-16,28,28,sa,768,zvel,i,4);

            sprite[j].cstat = 128;
            sprite[j].clipdist = 32;


            return j;
        }
    }
    return -1;
}

static void P_DisplaySpitAnim(int snum)
{
    int i, a, x, y, z;

    if (g_player[snum].ps->loogcnt == 0) return;

    y = (g_player[snum].ps->loogcnt<<2);
    for (i=0;i<g_player[snum].ps->numloogs;i++)
    {
        a = klabs(sintable[((g_player[snum].ps->loogcnt+i)<<5)&2047])>>5;
        z = 4096+((g_player[snum].ps->loogcnt+i)<<9);
        x = (-fix16_to_int(g_player[snum].inputBits->q16avel)) + (sintable[((g_player[snum].ps->loogcnt+i)<<6)&2047]>>10);

        rotatesprite(
            (g_player[snum].ps->loogiex[i]+x)<<16,(200+g_player[snum].ps->loogiey[i]-y)<<16,z-(i<<8),256-a,
            LOOGIE,0,0,2,0,0,xdim-1,ydim-1);
    }
}

static int P_DisplayFistAnim(int gs,int snum)
{
    int looking_arc,fisti,fistpal;
    int fistzoom, fistz;

    fisti = g_player[snum].ps->fist_incs;
    if (fisti > 32) fisti = 32;
    if (fisti <= 0) return 0;

    looking_arc = klabs(g_player[snum].ps->look_ang)/9;

    fistzoom = 65536L - (sintable[(512+(fisti<<6))&2047]<<2);
    if (fistzoom > 90612L)
        fistzoom = 90612L;
    if (fistzoom < 40920)
        fistzoom = 40290;
    fistz = 194 + (sintable[((6+fisti)<<7)&2047]>>9);

    if (sprite[g_player[snum].ps->i].pal == 1)
        fistpal = 1;
    else if (g_player[snum].ps->cursectnum >= 0)
        fistpal = sector[g_player[snum].ps->cursectnum].floorpal;
    else fistpal = 0;

    rotatesprite(
        (-fisti+222+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4))<<16,
        (looking_arc+fistz)<<16,
        fistzoom,0,FIST,gs,fistpal,2,0,0,xdim-1,ydim-1);

    return 1;
}

#define weapsc(sc) scale(sc,ud.weaponscale,100)

static void G_DrawTileScaled(int x, int y, int tilenum, int shade, int orientation, int p)
{
    int a = 0;
    int xoff = 192;

    switch (g_currentweapon)
    {
    case DEVISTATOR_WEAPON:
    case TRIPBOMB_WEAPON:
        xoff = 160;
        break;
    default:
        if (orientation & 262144)
        {
            xoff = 160;
            orientation &= ~262144;
        }
        break;
    }

    if (orientation&4)
        a = 1024;

#if defined(USE_OPENGL)
    if (videoGetRenderMode() >= 3 && usemodels && md_tilehasmodel(tilenum,p) > 0)
        y += (224-weapsc(224));
#endif
    rotatesprite(weapsc((orientation & 1024) ? x : (x << 16)) + ((xoff - weapsc(xoff)) << 16),
        weapsc((orientation & 1024) ? y : (y << 16)) + ((200 - weapsc(200)) << 16),
        weapsc(65536L), a, tilenum, shade, p, (2 | orientation), windowxy1.x, windowxy1.y, windowxy2.x, windowxy2.y);
}

static void G_DrawWeaponTile(int x, int y, int tilenum, int shade, int orientation, int p)
{
    if (!ud.drawweapon)
        return;
    else if (ud.drawweapon == 1)
        G_DrawTileScaled(x,y,tilenum,shade,orientation,p);
    else if (ud.drawweapon == 2)
    {
        switch (g_currentweapon)
        {
        case PISTOL_WEAPON:
        case CHAINGUN_WEAPON:
        case RPG_WEAPON:
        case FREEZE_WEAPON:
        case SHRINKER_WEAPON:
        case GROW_WEAPON:
        case DEVISTATOR_WEAPON:
        case TRIPBOMB_WEAPON:
        case HANDREMOTE_WEAPON:
        case HANDBOMB_WEAPON:
        case SHOTGUN_WEAPON:
            rotatesprite(160<<16,(180+(g_player[screenpeek].ps->weapon_pos*g_player[screenpeek].ps->weapon_pos))<<16,scale(65536,ud.statusbarscale,100),0,g_currentweapon==GROW_WEAPON?GROWSPRITEICON:WeaponPickupSprites[g_currentweapon],0,0,2,windowxy1.x,windowxy1.y,windowxy2.x,windowxy2.y);
            break;
        }
    }
}

static int P_DisplayKneeAnim(int gs,int snum)
{
    static signed char knee_y[] = {0,-8,-16,-32,-64,-84,-108,-108,-108,-72,-32,-8};
    int looking_arc, pal = g_player[snum].ps->palookup;

    if (g_player[snum].ps->knee_incs > 11 || g_player[snum].ps->knee_incs == 0 || sprite[g_player[snum].ps->i].extra <= 0) return 0;

    looking_arc = knee_y[g_player[snum].ps->knee_incs] + klabs(g_player[snum].ps->look_ang)/9;

    looking_arc -= (g_player[snum].ps->hard_landing<<3);

    if (sprite[g_player[snum].ps->i].pal == 1)
        pal = 1;
    else if (g_player[snum].ps->cursectnum >= 0)
    {
        pal = sector[g_player[snum].ps->cursectnum].floorpal;
        if (pal == 0)
            pal = g_player[snum].ps->palookup;
    }

    G_DrawTileScaled(105+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4)-(g_player[snum].ps->look_ang>>1)+(knee_y[g_player[snum].ps->knee_incs]>>2),looking_arc+280-(fix16_to_int(g_player[snum].ps->q16horiz-g_player[snum].ps->q16horizoff)>>4),KNEE,gs,4+262144,pal);

    return 1;
}

static int P_DisplayKnuckleAnim(int gs,int snum)
{
    static char knuckle_frames[] = {0,1,2,2,3,3,3,2,2,1,0};
    int looking_arc, pal = 0;

    if (g_player[snum].ps->knuckle_incs == 0 || sprite[g_player[snum].ps->i].extra <= 0) return 0;

    looking_arc = klabs(g_player[snum].ps->look_ang)/9;

    looking_arc -= (g_player[snum].ps->hard_landing<<3);

    if (sprite[g_player[snum].ps->i].pal == 1)
        pal = 1;
    else if (g_player[snum].ps->cursectnum >= 0)
        pal = sector[g_player[snum].ps->cursectnum].floorpal;

    G_DrawTileScaled(160+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4)-(g_player[snum].ps->look_ang>>1),looking_arc+180-(fix16_to_int(g_player[snum].ps->q16horiz-g_player[snum].ps->q16horizoff)>>4),CRACKKNUCKLES+knuckle_frames[g_player[snum].ps->knuckle_incs>>1],gs,4+262144,pal);

    return 1;
}

int lastvisinc;

void P_SetWeaponGamevars(int playerNum, const DukePlayer_t* const pPlayer)
{
    Gv_SetVar(g_iWeaponVarID, pPlayer->curr_weapon, pPlayer->i, playerNum);
    Gv_SetVar(g_iWorksLikeVarID,
        ((unsigned)pPlayer->curr_weapon < MAX_WEAPONS) ? PWEAPON(playerNum, pPlayer->curr_weapon, WorksLike) : -1,
        pPlayer->i, playerNum);
}

void P_FireWeapon(DukePlayer_t *p)
{
    int i, snum = sprite[p->i].yvel;

    Gv_SetVar(g_iReturnVarID,0,p->i,snum);
    X_OnEvent(EVENT_DOFIRE, p->i, snum, -1);

    if (Gv_GetVar(g_iReturnVarID,p->i,snum) == 0)
    {
        if (p->weapon_pos != 0) return;

        if (aplWeaponWorksLike[p->curr_weapon][snum]!=KNEE_WEAPON)
            p->ammo_amount[p->curr_weapon]--;

        if (aplWeaponFireSound[p->curr_weapon][snum] > 0)
        {
            A_PlaySound(aplWeaponFireSound[p->curr_weapon][snum],p->i);
        }

        P_SetWeaponGamevars(snum, p);
//        OSD_Printf("doing %d %d %d\n",aplWeaponShoots[p->curr_weapon][snum],p->curr_weapon,snum);
        A_Shoot(p->i,aplWeaponShoots[p->curr_weapon][snum]);
        for (i=1;i<aplWeaponShotsPerBurst[p->curr_weapon][snum];i++)
        {
            if (aplWeaponFlags[p->curr_weapon][snum] & WEAPON_FIREEVERYOTHER)
            {
                // this makes the projectiles fire on a delay from player code
                ActorExtra[p->i].temp_data[7] = (aplWeaponShotsPerBurst[p->curr_weapon][snum])<<1;
            }
            else
            {
                if (aplWeaponFlags[p->curr_weapon][snum] & WEAPON_AMMOPERSHOT)
                {
                    if (p->ammo_amount[p->curr_weapon] > 0)
                        p->ammo_amount[p->curr_weapon]--;
                    else break;
                }
                A_Shoot(p->i,aplWeaponShoots[p->curr_weapon][snum]);
            }
        }

        if (!(aplWeaponFlags[p->curr_weapon][snum] & WEAPON_NOVISIBLE))
        {
            lastvisinc = (int32_t)totalclock+32;
            p->visibility = 0;
        }

        /*        if( //!(aplWeaponFlags[p->curr_weapon][snum] & WEAPON_CHECKATRELOAD) &&
                    aplWeaponReload[p->curr_weapon][snum] > aplWeaponTotalTime[p->curr_weapon][snum]
                    && p->ammo_amount[p->curr_weapon] > 0
                    && (aplWeaponClip[p->curr_weapon][snum])
                    && ((p->ammo_amount[p->curr_weapon]%(aplWeaponClip[p->curr_weapon][snum]))==0))
                {
                    p->kickback_pic=aplWeaponTotalTime[p->curr_weapon][snum];
                } */
    }
}

void P_DoWeaponSpawn(DukePlayer_t *p)
{
    int j, snum = sprite[p->i].yvel;

    if (!aplWeaponSpawn[p->curr_weapon][snum])
        return;

    j = A_Spawn(p->i, aplWeaponSpawn[p->curr_weapon][snum]);

    if ((aplWeaponFlags[p->curr_weapon][snum] & WEAPON_SPAWNTYPE3))
    {
        // like chaingun shells
        sprite[j].ang += 1024;
        sprite[j].ang &= 2047;
        sprite[j].xvel += 32;
        sprite[j].z += (3<<8);
    }

    A_SetSprite(j,CLIPMASK0);

}

void P_DisplayScubaMask(int snum)
{
    int p;

    if (sprite[g_player[snum].ps->i].pal == 1)
        p = 1;
    else if (g_player[snum].ps->cursectnum >= 0)
        p = sector[g_player[snum].ps->cursectnum].floorpal;
    else p = 0;

    if (g_player[snum].ps->scuba_on)
    {
#ifdef USE_OPENGL
        if (videoGetRenderMode() >= 3)
            G_DrawTileScaled(44, (200 - tilesiz[SCUBAMASK].y), SCUBAMASK, 0, 2 + 16 + 262144, p);
#endif

        G_DrawTileScaled(43, (200 - tilesiz[SCUBAMASK].y), SCUBAMASK, 0, 2 + 16 + 262144, p);
        G_DrawTileScaled(320 - 43, (200 - tilesiz[SCUBAMASK].y), SCUBAMASK, 0, 2 + 4 + 16 + 262144, p);
    }
}

static int P_DisplayTipAnim(int gs,int snum)
{
    int p,looking_arc;
    static short tip_y[] = {0,-8,-16,-32,-64,-84,-108,-108,-108,-108,-108,-108,-108,-108,-108,-108,-96,-72,-64,-32,-16};

    if (g_player[snum].ps->tipincs == 0) return 0;

    looking_arc = klabs(g_player[snum].ps->look_ang)/9;
    looking_arc -= (g_player[snum].ps->hard_landing<<3);

    if (sprite[g_player[snum].ps->i].pal == 1)
        p = 1;
    else
        p = sector[g_player[snum].ps->cursectnum].floorpal;

    /*    if(g_player[snum].ps->access_spritenum >= 0)
            p = sprite[g_player[snum].ps->access_spritenum].pal;
        else
            p = wall[g_player[snum].ps->access_wallnum].pal;
      */
    G_DrawTileScaled(170+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4)-(g_player[snum].ps->look_ang>>1),
                     (tip_y[g_player[snum].ps->tipincs]>>1)+looking_arc+240-(fix16_to_int(g_player[snum].ps->q16horiz-g_player[snum].ps->q16horizoff)>>4),TIP+((26-g_player[snum].ps->tipincs)>>4),gs,262144,p);

    return 1;
}

static int P_DisplayAccessAnim(int gs,int snum)
{
    static short access_y[] = {0,-8,-16,-32,-64,-84,-108,-108,-108,-108,-108,-108,-108,-108,-108,-108,-96,-72,-64,-32,-16};
    int looking_arc;
    int p;

    if (g_player[snum].ps->access_incs == 0 || sprite[g_player[snum].ps->i].extra <= 0) return 0;

    looking_arc = access_y[g_player[snum].ps->access_incs] + klabs(g_player[snum].ps->look_ang)/9;
    looking_arc -= (g_player[snum].ps->hard_landing<<3);

    if (g_player[snum].ps->access_spritenum >= 0)
        p = sprite[g_player[snum].ps->access_spritenum].pal;
    else p = 0;
    //    else
    //        p = wall[g_player[snum].ps->access_wallnum].pal;

    if ((g_player[snum].ps->access_incs-3) > 0 && (g_player[snum].ps->access_incs-3)>>3)
        G_DrawTileScaled(170+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4)-(g_player[snum].ps->look_ang>>1)+(access_y[g_player[snum].ps->access_incs]>>2),looking_arc+266-(fix16_to_int(g_player[snum].ps->q16horiz-g_player[snum].ps->q16horizoff)>>4),HANDHOLDINGLASER+(g_player[snum].ps->access_incs>>3),gs,262144,p);
    else
        G_DrawTileScaled(170+(fix16_to_int(g_player[snum].inputBits->q16avel)>>4)-(g_player[snum].ps->look_ang>>1)+(access_y[g_player[snum].ps->access_incs]>>2),looking_arc+266-(fix16_to_int(g_player[snum].ps->q16horiz-g_player[snum].ps->q16horizoff)>>4),HANDHOLDINGACCESS,gs,4+262144,p);

    return 1;
}


static int fistsign;

void P_DisplayWeapon(int snum)
{
    int gun_pos, looking_arc, cw;
    int weapon_xoffset, i, j;
    int o = 0,pal = 0;
    DukePlayer_t *p = g_player[snum].ps;
    short *kb = &p->kickback_pic;
    int gs;

    looking_arc = klabs(p->look_ang)/9;

    gs = sprite[p->i].shade;
    if (gs > 24) gs = 24;

    if (p->newowner >= 0 || ud.camerasprite >= 0 || p->over_shoulder_on > 0 || (sprite[p->i].pal != 1 && sprite[p->i].extra <= 0) || P_DisplayFistAnim(gs,snum) || P_DisplayKnuckleAnim(gs,snum) || P_DisplayTipAnim(gs,snum) || P_DisplayAccessAnim(gs,snum))
        return;

    P_DisplayKneeAnim(gs,snum);

    gun_pos = 80-(p->weapon_pos*p->weapon_pos);

    weapon_xoffset = (160)-90;

    if (ud.weaponsway)
    {
        weapon_xoffset -= (sintable[((p->weapon_sway>>1)+512)&2047]/(1024+512));

        if (sprite[p->i].xrepeat < 32)
            gun_pos -= klabs(sintable[(p->weapon_sway<<2)&2047]>>9);
        else gun_pos -= klabs(sintable[(p->weapon_sway>>1)&2047]>>10);
    }
    else gun_pos -= 16;

    weapon_xoffset -= 58 + p->weapon_ang;
    gun_pos -= (p->hard_landing<<3);

    if (p->last_weapon >= 0)
        cw = aplWeaponWorksLike[p->last_weapon][snum];
    else
        cw = aplWeaponWorksLike[p->curr_weapon][snum];

    g_gun_pos=gun_pos;
    g_looking_arc=looking_arc;
    g_currentweapon=cw;
    g_weapon_xoffset=weapon_xoffset;
    g_gs=gs;
    g_kb=*kb;
    g_looking_angSR1=p->look_ang>>1;

    Gv_SetVar(g_iReturnVarID,0,p->i,snum);
    X_OnEvent(EVENT_DISPLAYWEAPON, p->i, screenpeek, -1);

    if (Gv_GetVar(g_iReturnVarID,p->i,snum) == 0)
    {
        j = 14-p->quick_kick;
        if (j != 14 || p->last_quick_kick)
        {
            if (sprite[p->i].pal == 1)
                pal = 1;
            else
            {
                if (p->cursectnum >= 0)
                    pal = sector[p->cursectnum].floorpal;
                if (pal == 0)
                    pal = p->palookup;
            }

            guniqhudid = 100;
            if (j < 6 || j > 12)
                G_DrawTileScaled(weapon_xoffset+80-(p->look_ang>>1),
                                 looking_arc+250-gun_pos,KNEE,gs,o|4|262144,pal);
            else G_DrawTileScaled(weapon_xoffset+160-16-(p->look_ang>>1),
                                      looking_arc+214-gun_pos,KNEE+1,gs,o|4|262144,pal);
            guniqhudid = 0;
        }

        if (sprite[p->i].xrepeat < 40)
        {
            if (p->jetpack_on == 0)
            {
                i = sprite[p->i].xvel;
                looking_arc += 32-(i>>1);
                fistsign += i>>1;
            }
            cw = weapon_xoffset;
            weapon_xoffset += sintable[(fistsign)&2047]>>10;
            G_DrawTile(weapon_xoffset+250-(p->look_ang>>1),
                       looking_arc+258-(klabs(sintable[(fistsign)&2047]>>8)),
                       FIST,gs,o);
            weapon_xoffset = cw;
            weapon_xoffset -= sintable[(fistsign)&2047]>>10;
            G_DrawTile(weapon_xoffset+40-(p->look_ang>>1),
                       looking_arc+200+(klabs(sintable[(fistsign)&2047]>>8)),
                       FIST,gs,o|4);
        }
        else switch (cw)

            {

            case KNEE_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if ((*kb) > 0)
                    {
                        if (sprite[p->i].pal == 1)
                            pal = 1;
                        else
                        {
                            if (p->cursectnum >= 0)
                                pal = sector[p->cursectnum].floorpal;
                            if (pal == 0)
                                pal = p->palookup;
                        }
                        guniqhudid = cw;
                        if ((*kb) < 5 || (*kb) > 9)
                            G_DrawTileScaled(weapon_xoffset+220-(p->look_ang>>1),
                                             looking_arc+250-gun_pos,KNEE,gs,o,pal);
                        else
                            G_DrawTileScaled(weapon_xoffset+160-(p->look_ang>>1),
                                             looking_arc+214-gun_pos,KNEE+1,gs,o,pal);
                        guniqhudid = 0;
                    }
                }
                break;

            case TRIPBOMB_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    weapon_xoffset += 8;
                    gun_pos -= 10;

                    if ((*kb) > 6)
                        looking_arc += ((*kb)<<3);
                    else if ((*kb) < 4)
                    {
                        guniqhudid = cw<<2;
                        G_DrawWeaponTile(weapon_xoffset+142-(p->look_ang>>1),
                                         looking_arc+234-gun_pos,HANDHOLDINGLASER+3,gs,o,pal);
                    }

                    guniqhudid = cw;
                    G_DrawWeaponTile(weapon_xoffset+130-(p->look_ang>>1),
                                     looking_arc+249-gun_pos,
                                     HANDHOLDINGLASER+((*kb)>>2),gs,o,pal);

                    guniqhudid = cw<<1;
                    G_DrawWeaponTile(weapon_xoffset+152-(p->look_ang>>1),
                                     looking_arc+249-gun_pos,
                                     HANDHOLDINGLASER+((*kb)>>2),gs,o|4,pal);
                    guniqhudid = 0;
                }
                break;

            case RPG_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    weapon_xoffset -= sintable[(768+((*kb)<<7))&2047]>>11;
                    gun_pos += sintable[(768+((*kb)<<7))&2047]>>11;

                    if (*kb > 0)
                    {
                        if (*kb < 8)
                        {
                            G_DrawWeaponTile(weapon_xoffset+164,(looking_arc<<1)+176-gun_pos,
                                             RPGGUN+((*kb)>>1),gs,o,pal);
                        }
                    }

                    G_DrawWeaponTile(weapon_xoffset+164,(looking_arc<<1)+176-gun_pos,
                                     RPGGUN,gs,o,pal);
                }
                break;

            case SHOTGUN_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;


                    weapon_xoffset -= 8;

                    switch (*kb)
                    {
                    case 1:
                    case 2:
                        guniqhudid = cw<<1;
                        G_DrawWeaponTile(weapon_xoffset+168-(p->look_ang>>1),looking_arc+201-gun_pos,
                                         SHOTGUN+2,-128,o,pal);
                        fallthrough__;
                    case 0:
                    case 6:
                    case 7:
                    case 8:
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+146-(p->look_ang>>1),looking_arc+202-gun_pos,
                                         SHOTGUN,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 3:
                    case 4:
                    case 5:
                    case 9:
                    case 10:
                    case 11:
                    case 12:
                        if (*kb > 1 && *kb < 5)
                        {
                            gun_pos -= 40;
                            weapon_xoffset += 20;

                            guniqhudid = cw<<1;
                            G_DrawWeaponTile(weapon_xoffset+178-(p->look_ang>>1),looking_arc+194-gun_pos,
                                             SHOTGUN+1+((*(kb)-1)>>1),-128,o,pal);
                        }
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+158-(p->look_ang>>1),looking_arc+220-gun_pos,
                                         SHOTGUN+3,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 13:
                    case 14:
                    case 15:
                        guniqhudid = cw;
                        G_DrawWeaponTile(32+weapon_xoffset+166-(p->look_ang>>1),looking_arc+210-gun_pos,
                                         SHOTGUN+4,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 16:
                    case 17:
                    case 18:
                    case 19:
                        guniqhudid = cw;
                        G_DrawWeaponTile(64+weapon_xoffset+170-(p->look_ang>>1),looking_arc+196-gun_pos,
                                         SHOTGUN+5,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 20:
                    case 21:
                    case 22:
                    case 23:
                        guniqhudid = cw;
                        G_DrawWeaponTile(64+weapon_xoffset+176-(p->look_ang>>1),looking_arc+196-gun_pos,
                                         SHOTGUN+6,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 24:
                    case 25:
                    case 26:
                    case 27:
                        guniqhudid = cw;
                        G_DrawWeaponTile(64+weapon_xoffset+170-(p->look_ang>>1),looking_arc+196-gun_pos,
                                         SHOTGUN+5,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    case 28:
                    case 29:
                    case 30:
                        guniqhudid = cw;
                        G_DrawWeaponTile(32+weapon_xoffset+156-(p->look_ang>>1),looking_arc+206-gun_pos,
                                         SHOTGUN+4,gs,o,pal);
                        guniqhudid = 0;
                        break;
                    }
                }
                break;


            case CHAINGUN_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    if (*kb > 0)
                        gun_pos -= sintable[(*kb)<<7]>>12;

                    if (*kb > 0 && sprite[p->i].pal != 1) weapon_xoffset += 1-(rand()&3);

                    G_DrawWeaponTile(weapon_xoffset+168-(p->look_ang>>1),looking_arc+260-gun_pos,
                                     CHAINGUN,gs,o,pal);
                    switch (*kb)
                    {
                    case 0:
                        G_DrawWeaponTile(weapon_xoffset+178-(p->look_ang>>1),looking_arc+233-gun_pos,
                                         CHAINGUN+1,gs,o,pal);
                        break;
                    default:
                        if (*kb > *aplWeaponFireDelay[CHAINGUN_WEAPON] && *kb < *aplWeaponTotalTime[CHAINGUN_WEAPON])
                        {
                            i = 0;
                            if (sprite[p->i].pal != 1) i = rand()&7;
                            G_DrawWeaponTile(i+weapon_xoffset-4+140-(p->look_ang>>1),i+looking_arc-((*kb)>>1)+208-gun_pos,
                                             CHAINGUN+5+((*kb-4)/5),gs,o,pal);
                            if (sprite[p->i].pal != 1) i = rand()&7;
                            G_DrawWeaponTile(i+weapon_xoffset-4+184-(p->look_ang>>1),i+looking_arc-((*kb)>>1)+208-gun_pos,
                                             CHAINGUN+5+((*kb-4)/5),gs,o,pal);
                        }
                        if (*kb < *aplWeaponTotalTime[CHAINGUN_WEAPON]-4)
                        {
                            i = rand()&7;
                            G_DrawWeaponTile(i+weapon_xoffset-4+162-(p->look_ang>>1),i+looking_arc-((*kb)>>1)+208-gun_pos,
                                             CHAINGUN+5+((*kb-2)/5),gs,o,pal);
                            G_DrawWeaponTile(weapon_xoffset+178-(p->look_ang>>1),looking_arc+233-gun_pos,
                                             CHAINGUN+1+((*kb)>>1),gs,o,pal);
                        }
                        else G_DrawWeaponTile(weapon_xoffset+178-(p->look_ang>>1),looking_arc+233-gun_pos,
                                                  CHAINGUN+1,gs,o,pal);
                        break;
                    }
                }
                break;

            case PISTOL_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    if ((*kb) < *aplWeaponTotalTime[PISTOL_WEAPON]+1)
                    {
                        static short kb_frames[] = {0,1,2};
                        int l = 195-12+weapon_xoffset;

                        if ((*kb) == *aplWeaponFireDelay[PISTOL_WEAPON])
                            l -= 3;

                        guniqhudid = cw;
                        G_DrawWeaponTile((l-(p->look_ang>>1)),(looking_arc+244-gun_pos),FIRSTGUN+kb_frames[*kb>2?0:*kb],gs,2,pal);
                        guniqhudid = 0;
                    }
                    else
                    {

                        if ((*kb) < *aplWeaponReload[PISTOL_WEAPON]-17)
                        {
                            guniqhudid = cw;
                            G_DrawWeaponTile(194-(p->look_ang>>1),looking_arc+230-gun_pos,FIRSTGUN+4,gs,o,pal);
                            guniqhudid = 0;
                        }
                        else if ((*kb) < *aplWeaponReload[PISTOL_WEAPON]-12)
                        {
                            G_DrawWeaponTile(244-((*kb)<<3)-(p->look_ang>>1),looking_arc+130-gun_pos+((*kb)<<4),FIRSTGUN+6,gs,o,pal);
                            guniqhudid = cw;
                            G_DrawWeaponTile(224-(p->look_ang>>1),looking_arc+220-gun_pos,FIRSTGUN+5,gs,o,pal);
                            guniqhudid = 0;
                        }
                        else if ((*kb) < *aplWeaponReload[PISTOL_WEAPON]-7)
                        {
                            G_DrawWeaponTile(124+((*kb)<<1)-(p->look_ang>>1),looking_arc+430-gun_pos-((*kb)<<3),FIRSTGUN+6,gs,o,pal);
                            guniqhudid = cw;
                            G_DrawWeaponTile(224-(p->look_ang>>1),looking_arc+220-gun_pos,FIRSTGUN+5,gs,o,pal);
                            guniqhudid = 0;
                        }

                        else if ((*kb) < *aplWeaponReload[PISTOL_WEAPON]-4)
                        {
                            G_DrawWeaponTile(184-(p->look_ang>>1),looking_arc+235-gun_pos,FIRSTGUN+8,gs,o,pal);
                            guniqhudid = cw;
                            G_DrawWeaponTile(224-(p->look_ang>>1),looking_arc+210-gun_pos,FIRSTGUN+5,gs,o,pal);
                            guniqhudid = 0;
                        }
                        else if ((*kb) < *aplWeaponReload[PISTOL_WEAPON]-2)
                        {
                            G_DrawWeaponTile(164-(p->look_ang>>1),looking_arc+245-gun_pos,FIRSTGUN+8,gs,o,pal);
                            guniqhudid = cw;
                            G_DrawWeaponTile(224-(p->look_ang>>1),looking_arc+220-gun_pos,FIRSTGUN+5,gs,o,pal);
                            guniqhudid = 0;
                        }
                        else if ((*kb) < *aplWeaponReload[PISTOL_WEAPON])
                        {
                            guniqhudid = cw;
                            G_DrawWeaponTile(194-(p->look_ang>>1),looking_arc+235-gun_pos,FIRSTGUN+5,gs,o,pal);
                            guniqhudid = 0;
                        }

                    }
                }

                break;
            case HANDBOMB_WEAPON:
            {
                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;
                    guniqhudid = cw;
                    if ((*kb))
                    {
                        if ((*kb) < (*aplWeaponTotalTime[p->curr_weapon]))
                        {

                            static char throw_frames[]
                            = {0,0,0,0,0,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2};

                            if ((*kb) < 7)
                                gun_pos -= 10*(*kb);        //D
                            else if ((*kb) < 12)
                                gun_pos += 20*((*kb)-10); //U
                            else if ((*kb) < 20)
                                gun_pos -= 9*((*kb)-14);  //D

                            G_DrawWeaponTile(weapon_xoffset+190-(p->look_ang>>1),looking_arc+250-gun_pos,HANDTHROW+throw_frames[(*kb)],gs,o,pal);
                        }
                    }
                    else
                        G_DrawWeaponTile(weapon_xoffset+190-(p->look_ang>>1),looking_arc+260-gun_pos,HANDTHROW,gs,o,pal);
                    guniqhudid = 0;
                }
            }
            break;

            case HANDREMOTE_WEAPON:
            {
                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    static char remote_frames[] = {0,1,1,2,1,1,0,0,0,0,0};
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    weapon_xoffset = -48;
                    guniqhudid = cw;
                    if ((*kb))
                        G_DrawWeaponTile(weapon_xoffset+150-(p->look_ang>>1),looking_arc+258-gun_pos,HANDREMOTE+remote_frames[(*kb)],gs,o,pal);
                    else
                        G_DrawWeaponTile(weapon_xoffset+150-(p->look_ang>>1),looking_arc+258-gun_pos,HANDREMOTE,gs,o,pal);
                    guniqhudid = 0;
                }
            }
            break;

            case DEVISTATOR_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;

                    if ((*kb) < (*aplWeaponTotalTime[DEVISTATOR_WEAPON]+1) && (*kb) > 0)
                    {
                        static char cycloidy[] = {0,4,12,24,12,4,0};

                        i = ksgn((*kb)>>2);

                        if (p->hbomb_hold_delay)
                        {
                            guniqhudid = cw;
                            G_DrawWeaponTile((cycloidy[*kb]>>1)+weapon_xoffset+268-(p->look_ang>>1),cycloidy[*kb]+looking_arc+238-gun_pos,DEVISTATOR+i,-32,o,pal);
                            guniqhudid = cw<<1;
                            G_DrawWeaponTile(weapon_xoffset+30-(p->look_ang>>1),looking_arc+240-gun_pos,DEVISTATOR,gs,o|4,pal);
                            guniqhudid = 0;
                        }
                        else
                        {
                            guniqhudid = cw<<1;
                            G_DrawWeaponTile(-(cycloidy[*kb]>>1)+weapon_xoffset+30-(p->look_ang>>1),cycloidy[*kb]+looking_arc+240-gun_pos,DEVISTATOR+i,-32,o|4,pal);
                            guniqhudid = cw;
                            G_DrawWeaponTile(weapon_xoffset+268-(p->look_ang>>1),looking_arc+238-gun_pos,DEVISTATOR,gs,o,pal);
                            guniqhudid = 0;
                        }
                    }
                    else
                    {
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+268-(p->look_ang>>1),looking_arc+238-gun_pos,DEVISTATOR,gs,o,pal);
                        guniqhudid = cw<<1;
                        G_DrawWeaponTile(weapon_xoffset+30-(p->look_ang>>1),looking_arc+240-gun_pos,DEVISTATOR,gs,o|4,pal);
                        guniqhudid = 0;
                    }
                }
                break;

            case FREEZE_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;
                    if ((*kb) < (aplWeaponTotalTime[p->curr_weapon][snum]+1) && (*kb) > 0)
                    {
                        static char cat_frames[] = { 0,0,1,1,2,2 };

                        if (sprite[p->i].pal != 1)
                        {
                            weapon_xoffset += rand()&3;
                            looking_arc += rand()&3;
                        }
                        gun_pos -= 16;
                        guniqhudid = 0;
                        G_DrawWeaponTile(weapon_xoffset+210-(p->look_ang>>1),looking_arc+261-gun_pos,FREEZE+2,-32,o,pal);
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+210-(p->look_ang>>1),looking_arc+235-gun_pos,FREEZE+3+cat_frames[*kb%6],-32,o,pal);
                        guniqhudid = 0;
                    }
                    else
                    {
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+210-(p->look_ang>>1),looking_arc+261-gun_pos,FREEZE,gs,o,pal);
                        guniqhudid = 0;
                    }
                }
                break;

            case GROW_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    weapon_xoffset += 28;
                    looking_arc += 18;
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;
                    {
                        if ((*kb) < aplWeaponTotalTime[p->curr_weapon][snum] && (*kb) > 0)
                        {
                            if (sprite[p->i].pal != 1)
                            {
                                weapon_xoffset += rand()&3;
                                gun_pos += (rand()&3);
                            }

                            guniqhudid = cw<<1;
                            G_DrawWeaponTile(weapon_xoffset+184-(p->look_ang>>1),
                                             looking_arc+240-gun_pos,SHRINKER+3+((*kb)&3),-32,
                                             o,2);

                            guniqhudid = cw;
                            G_DrawWeaponTile(weapon_xoffset+188-(p->look_ang>>1),
                                             looking_arc+240-gun_pos,SHRINKER-1,gs,o,pal);
                            guniqhudid = 0;
                        }
                        else
                        {
                            guniqhudid = cw<<1;
                            G_DrawWeaponTile(weapon_xoffset+184-(p->look_ang>>1),
                                             looking_arc+240-gun_pos,SHRINKER+2,
                                             16-(sintable[p->random_club_frame&2047]>>10),
                                             o,2);

                            guniqhudid = cw;
                            G_DrawWeaponTile(weapon_xoffset+188-(p->look_ang>>1),
                                             looking_arc+240-gun_pos,SHRINKER-2,gs,o,pal);
                            guniqhudid = 0;
                        }
                    }
                }
                break;

            case SHRINKER_WEAPON:

                Gv_SetVar(g_iReturnVarID,0,g_player[screenpeek].ps->i,screenpeek);
                X_OnEvent(EVENT_DRAWWEAPON,g_player[screenpeek].ps->i,screenpeek, -1);
                if (Gv_GetVar(g_iReturnVarID,g_player[screenpeek].ps->i,screenpeek) == 0)
                {
                    weapon_xoffset += 28;
                    looking_arc += 18;
                    if (sprite[p->i].pal == 1)
                        pal = 1;
                    else if (p->cursectnum >= 0)
                        pal = sector[p->cursectnum].floorpal;
                    else pal = 0;
                    if (((*kb) > 0) && ((*kb) < aplWeaponTotalTime[p->curr_weapon][snum]))
                    {
                        if (sprite[p->i].pal != 1)
                        {
                            weapon_xoffset += rand()&3;
                            gun_pos += (rand()&3);
                        }
                        guniqhudid = cw<<1;
                        G_DrawWeaponTile(weapon_xoffset+184-(p->look_ang>>1),
                                         looking_arc+240-gun_pos,SHRINKER+3+((*kb)&3),-32,
                                         o,0);
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+188-(p->look_ang>>1),
                                         looking_arc+240-gun_pos,SHRINKER+1,gs,o,pal);
                        guniqhudid = 0;

                    }
                    else
                    {
                        guniqhudid = cw<<1;
                        G_DrawWeaponTile(weapon_xoffset+184-(p->look_ang>>1),
                                         looking_arc+240-gun_pos,SHRINKER+2,
                                         16-(sintable[p->random_club_frame&2047]>>10),
                                         o,0);
                        guniqhudid = cw;
                        G_DrawWeaponTile(weapon_xoffset+188-(p->look_ang>>1),
                                         looking_arc+240-gun_pos,SHRINKER,gs,o,pal);
                        guniqhudid = 0;
                    }
                }
                break;

            }
    }
    P_DisplaySpitAnim(snum);

}

#define TURBOTURNTIME (TICRATE/8) // 7
#define NORMALTURN   15
#define PREAMBLETURN 5
#define NORMALKEYMOVE 40
#define MAXVEL       ((NORMALKEYMOVE*2)+10)
#define MAXSVEL      ((NORMALKEYMOVE*2)+10)
#define MAXANGVEL    1024
#define MAXHORIZ     127

int g_myAimMode = 0, g_myAimStat = 0, g_oldAimStat = 0;
int32_t mouseyaxismode = -1;
int jump_input = 0;

void getinput(int snum)
{
    int j, daang;
    static ControlInfo info[2];
    static int32_t     dyaw;
    int32_t tics;
    bool running;
    int32_t turnamount;
    int32_t keymove;
    int32_t momx = 0,momy = 0;
    DukePlayer_t *p = g_player[snum].ps;

    if ((p->gm&MODE_MENU) || (p->gm&MODE_TYPE) || (ud.pause_on && !KB_KeyPressed(sc_Pause)) || (numplayers > 1 && (int32_t)totalclock < 10))
        // HACK: kill getinput() for the first 10 tics of a new map in multi
    {
        if (!(p->gm&MODE_MENU))
            CONTROL_GetInput(&info[0]);
        memset(&info[1], 0, sizeof(info[1]));
        loc.fvel = vel = 0;
        loc.svel = svel = 0;
        loc.q16avel = q16angvel = 0;
        loc.q16horz = q16horiz = 0;
        loc.bits = (((int)g_gameQuit)<<SK_GAMEQUIT);
        loc.extbits = (g_player[snum].pteam != g_player[snum].ps->team)<<6;
        loc.extbits |= (1<<7);
        dyaw = 0;
        return;
    }

    if (ud.mouseaiming)
        g_myAimMode = BUTTON(gamefunc_Mouse_Aiming);
    else
    {
        g_oldAimStat = g_myAimStat;
        g_myAimStat = BUTTON(gamefunc_Mouse_Aiming);
        if (g_myAimStat > g_oldAimStat)
        {
            g_myAimMode ^= 1;
            P_DoQuote(44+g_myAimMode,p);
        }
    }

    CONTROL_GetInput(&info[0]);

    if (ud.config.MouseBias)
    {
        if (klabs(info[0].mousex) > klabs(info[0].mousey))
            info[0].mousey = tabledivide32_noinline(info[0].mousey, ud.config.MouseBias);
        else
            info[0].mousex = tabledivide32_noinline(info[0].mousex, ud.config.MouseBias);
    }

    tics = (int32_t)totalclock-lastcontroltime;
    lastcontroltime = (int32_t)totalclock;

    if (multiflag == 1)
    {
        loc.bits =   1<<SK_MULTIFLAG;
        loc.bits |=   multiwhat<<(SK_MULTIFLAG+1);
        loc.bits |=   multipos<<(SK_MULTIFLAG+2);
        multiflag = 0;
        return;
    }

    if (BUTTON(gamefunc_Jump) && p->on_ground)
        jump_input = 4;

    loc.bits = (jump_input > 0 || BUTTON(gamefunc_Jump))<<SK_JUMP;   //BUTTON(gamefunc_Jump);
    loc.bits |=   BUTTON(gamefunc_Crouch)<<SK_CROUCH;
    loc.bits |=   BUTTON(gamefunc_Fire)<<SK_FIRE;
    loc.bits |=   BUTTON(gamefunc_Aim_Up)<<SK_AIM_UP;
    loc.bits |=   BUTTON(gamefunc_Aim_Down)<<SK_AIM_DOWN;
    if (ud.runkey_mode) loc.bits |= (ud.auto_run | BUTTON(gamefunc_Run))<<SK_RUN;
    else loc.bits |= (BUTTON(gamefunc_Run) ^ ud.auto_run)<<SK_RUN;
    loc.bits |=   BUTTON(gamefunc_Look_Left)<<SK_LOOK_LEFT;
    loc.bits |=   BUTTON(gamefunc_Look_Right)<<SK_LOOK_RIGHT;

    if (aplWeaponFlags[g_player[snum].ps->curr_weapon][snum] & WEAPON_SEMIAUTO && BUTTON(gamefunc_Fire))
        CONTROL_ClearButton(gamefunc_Fire);

    if (jump_input > 0)
        jump_input--;

    j=0;

    if (BUTTON(gamefunc_Weapon_1))
        j = 1;
    if (BUTTON(gamefunc_Weapon_2))
        j = 2;
    if (BUTTON(gamefunc_Weapon_3))
        j = 3;
    if (BUTTON(gamefunc_Weapon_4))
        j = 4;
    if (BUTTON(gamefunc_Weapon_5))
        j = 5;
    if (BUTTON(gamefunc_Weapon_6))
        j = 6;
    if (BUTTON(gamefunc_Weapon_7))
        j = 7;
    if (BUTTON(gamefunc_Weapon_8))
        j = 8;
    if (BUTTON(gamefunc_Weapon_9))
        j = 9;
    if (BUTTON(gamefunc_Weapon_10))
        j = 10;
    if (BUTTON(gamefunc_Previous_Weapon))
        j = 11;
    if (BUTTON(gamefunc_Next_Weapon))
        j = 12;
    if (BUTTON(gamefunc_Alt_Weapon))
        j = 13;
    if (BUTTON(gamefunc_Last_Weapon))
        j = 14;

    loc.bits |=   j<<SK_WEAPON_BITS;
    loc.bits |=   BUTTON(gamefunc_Steroids)<<SK_STEROIDS;
    loc.bits |=   BUTTON(gamefunc_Look_Up)<<SK_LOOK_UP;
    loc.bits |=   BUTTON(gamefunc_Look_Down)<<SK_LOOK_DOWN;
    loc.bits |=   BUTTON(gamefunc_NightVision)<<SK_NIGHTVISION;
    loc.bits |=   BUTTON(gamefunc_MedKit)<<SK_MEDKIT;
    loc.bits |=   BUTTON(gamefunc_Center_View)<<SK_CENTER_VIEW;
    loc.bits |=   BUTTON(gamefunc_Holster_Weapon)<<SK_HOLSTER;
    loc.bits |=   BUTTON(gamefunc_Inventory_Left)<<SK_INV_LEFT;
    loc.bits |=   KB_KeyPressed(sc_Pause)<<SK_PAUSE;
    loc.bits |=   BUTTON(gamefunc_Quick_Kick)<<SK_QUICK_KICK;
    loc.bits |=   g_myAimMode<<SK_AIMMODE;
    loc.bits |=   BUTTON(gamefunc_Holo_Duke)<<SK_HOLODUKE;
    loc.bits |=   BUTTON(gamefunc_Jetpack)<<SK_JETPACK;
    loc.bits |= (((int)g_gameQuit)<<SK_GAMEQUIT);
    loc.bits |=   BUTTON(gamefunc_Inventory_Right)<<SK_INV_RIGHT;
    loc.bits |=   BUTTON(gamefunc_TurnAround)<<SK_TURNAROUND;
    loc.bits |=   BUTTON(gamefunc_Open)<<SK_OPEN;
    loc.bits |=   BUTTON(gamefunc_Inventory)<<SK_INVENTORY;
    loc.bits |=   KB_KeyPressed(sc_Escape)<<SK_ESCAPE;

    //    running = BUTTON(gamefunc_Run)|ud.auto_run;
    // JBF: Run key behaviour is selectable
    if (ud.runkey_mode)
    {
        running = BUTTON(gamefunc_Run) | ud.auto_run; // classic
    }
    else
    {
        running = ud.auto_run^BUTTON(gamefunc_Run); // modern
    }

    svel = vel = q16angvel = 0;
    q16horiz = 0;

    if (BUTTON(gamefunc_Strafe))
    {
        svel = -(info[0].mousex + dyaw) >> 3;
        dyaw = (info[0].mousex + dyaw) % 8;
    }
    else
    {
        q16angvel = fix16_sadd(q16angvel, fix16_sdiv(fix16_from_int(info[0].mousex), F16(32)));
    }

    if (g_myAimMode)
        q16horiz = fix16_sadd(q16horiz, fix16_sdiv(fix16_from_int(info[0].mousey), F16(64)));
    else
        vel = -(info[0].mousey >> 3);

    if (ud.mouseflip) q16horiz = -q16horiz;

    svel -= info[0].dx;
    vel -= info[0].dz>>6;

    if (running)
    {
        turnamount = NORMALTURN<<1;
        keymove = NORMALKEYMOVE<<1;
    }
    else
    {
        turnamount = NORMALTURN;
        keymove = NORMALKEYMOVE;
    }

    if (BUTTON(gamefunc_Strafe))
    {
        if (BUTTON(gamefunc_Turn_Left) && !(g_player[snum].ps->movement_lock&4))
            svel -= -keymove;
        if (BUTTON(gamefunc_Turn_Right) && !(g_player[snum].ps->movement_lock&8))
            svel -= keymove;
    }
    else
    {
        if (BUTTON(gamefunc_Turn_Left))
        {
            turnheldtime += tics;
            q16angvel -= fix16_from_int((turnheldtime >= TURBOTURNTIME) ? (turnamount << 1) : (PREAMBLETURN << 1));
        }
        else if (BUTTON(gamefunc_Turn_Right))
        {
            turnheldtime += tics;
            q16angvel += fix16_from_int((turnheldtime >= TURBOTURNTIME) ? (turnamount << 1) : (PREAMBLETURN << 1));
        }
        else
            turnheldtime=0;
    }

    if (BUTTON(gamefunc_Strafe_Left) && !(g_player[snum].ps->movement_lock&4))
        svel += keymove;
    if (BUTTON(gamefunc_Strafe_Right) && !(g_player[snum].ps->movement_lock&8))
        svel += -keymove;
    if (BUTTON(gamefunc_Move_Forward) && !(g_player[snum].ps->movement_lock&1))
        vel += keymove;
    if (BUTTON(gamefunc_Move_Backward) && !(g_player[snum].ps->movement_lock&2))
        vel += -keymove;

    if (vel < -MAXVEL) vel = -MAXVEL;
    if (vel > MAXVEL) vel = MAXVEL;
    if (svel < -MAXSVEL) svel = -MAXSVEL;
    if (svel > MAXSVEL) svel = MAXSVEL;

    //q16angvel = fix16_clamp(q16angvel, F16(-MAXANGVEL), F16(MAXANGVEL));
    //q16horiz = fix16_clamp(q16horiz, F16(-MAXHORIZ), F16(MAXHORIZ));

    loc.extbits = 0;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_MOVEFORWARD])
        loc.extbits |= BUTTON(gamefunc_Move_Forward) || (vel > 0);
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_MOVEBACKWARD])
        loc.extbits |= (BUTTON(gamefunc_Move_Backward) || (vel < 0))<<1;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_STRAFELEFT])
        loc.extbits |= (BUTTON(gamefunc_Strafe_Left) || (svel > 0))<<2;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_STRAFERIGHT])
        loc.extbits |= (BUTTON(gamefunc_Strafe_Right) || (svel < 0))<<3;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_TURNLEFT])
        loc.extbits |= BUTTON(gamefunc_Turn_Left)<<4;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_TURNRIGHT])
        loc.extbits |= BUTTON(gamefunc_Turn_Right)<<5;
    if (apScriptGameEvent[EVENT_PROCESSINPUT] || apScriptGameEvent[EVENT_ALTFIRE])
        loc.extbits |= BUTTON(gamefunc_Alt_Fire) << 6;
    // used for changing team
    loc.extbits |= (g_player[snum].pteam != g_player[snum].ps->team)<<6;

    if (ud.scrollmode && ud.overhead_on)
    {
        ud.folfvel = vel;
        ud.folavel = fix16_to_int(q16angvel);
        loc.fvel = 0;
        loc.svel = 0;
        loc.q16avel = 0;
        loc.q16horz = 0;
        return;
    }

    if (numplayers > 1)
        daang = fix16_to_int(predictedPlayer.q16ang);
    else daang = fix16_to_int(p->q16ang);

    momx = mulscale9(vel,sintable[(daang+2560)&2047]);
    momy = mulscale9(vel,sintable[(daang+2048)&2047]);

    momx += mulscale9(svel,sintable[(daang+2048)&2047]);
    momy += mulscale9(svel,sintable[(daang+1536)&2047]);

    momx += fricxv;
    momy += fricyv;

    loc.fvel = momx;
    loc.svel = momy;

    loc.q16avel = q16angvel;
    loc.q16horz = q16horiz;
}

static int P_DoCounters(DukePlayer_t *p)
{
    if (oldnet_predicting)
        return 0;

    int snum = sprite[p->i].yvel;

    //    j = g_player[snum].inputBits->avel;
    //    p->weapon_ang = -(j/5);

    if (p->invdisptime > 0)
        p->invdisptime--;

    if (p->tipincs > 0) p->tipincs--;

    if (p->last_pissed_time > 0)
    {
        p->last_pissed_time--;

        if (p->last_pissed_time == (26*219))
        {
            A_PlaySound(FLUSH_TOILET,p->i);
            if (snum == screenpeek || GTFLAGS(GAMETYPE_COOPSOUND))
                A_PlaySound(DUKE_PISSRELIEF,p->i);
        }

        if (p->last_pissed_time == (26*218))
        {
            p->holster_weapon = 0;
            p->weapon_pos = 10;
        }
    }

    if (p->crack_time > 0)
    {
        p->crack_time--;
        if (p->crack_time == 0)
        {
            p->knuckle_incs = 1;
            p->crack_time = 777;
        }
    }

    if (p->steroids_amount > 0 && p->steroids_amount < 400)
    {
        p->steroids_amount--;
        if (p->steroids_amount == 0)
            P_SelectNextInvItem(p);
        if (!(p->steroids_amount&7))
            if (snum == screenpeek || GTFLAGS(GAMETYPE_COOPSOUND))
                A_PlaySound(DUKE_HARTBEAT,p->i);
    }

    if (p->heat_on && p->heat_amount > 0)
    {
        p->heat_amount--;
        if (p->heat_amount == 0)
        {
            p->heat_on = 0;
            P_SelectNextInvItem(p);
            A_PlaySound(NITEVISION_ONOFF,p->i);
            P_UpdateScreenPal(p);
        }
    }

    if (p->holoduke_on >= 0)
    {
        p->holoduke_amount--;
        if (p->holoduke_amount <= 0)
        {
            A_PlaySound(TELEPORTER,p->i);
            p->holoduke_on = -1;
            P_SelectNextInvItem(p);
        }
    }

    if (p->jetpack_on && p->jetpack_amount > 0)
    {
        p->jetpack_amount--;
        if (p->jetpack_amount <= 0)
        {
            p->jetpack_on = 0;
            P_SelectNextInvItem(p);
            A_PlaySound(DUKE_JETPACK_OFF,p->i);
            S_StopEnvSound(DUKE_JETPACK_IDLE,p->i);
            S_StopEnvSound(DUKE_JETPACK_ON,p->i);
        }
    }

    if (p->quick_kick > 0 && sprite[p->i].pal != 1)
    {
        p->last_quick_kick = p->quick_kick+1;
        p->quick_kick--;
        if (p->quick_kick == 8)
            A_Shoot(p->i,KNEE);
    }
    else if (p->last_quick_kick > 0) p->last_quick_kick--;

    if (p->access_incs && sprite[p->i].pal != 1)
    {
        p->access_incs++;
        if (sprite[p->i].extra <= 0)
            p->access_incs = 12;
        if (p->access_incs == 12)
        {
            char keyname[7];
            int keypal;

            if (p->access_spritenum >= 0)
            {
                P_ActivateSwitch(snum,p->access_spritenum,1);

                if (ud.multimode < 2 || (ud.multimode > 1 && !(GametypeFlags[ud.coop] & GAMETYPE_ACCESSATSTART)))
                {
                    keypal = sprite[p->access_spritenum].pal;
                    switch (keypal)
                    {
                    case 0:
                        Bsprintf(keyname, "Blue");
                        ud.got_access &= (0xffff - 0x1);
                        break;
                    case 21:
                        Bsprintf(keyname, "Red");
                        ud.got_access &= (0xffff - 0x2);
                        break;
                    case 23:
                        Bsprintf(keyname, "Yellow");
                        ud.got_access &= (0xffff - 0x4);
                        break;
                    }
                }

                p->access_spritenum = -1;
            }
            else
            {
                P_ActivateSwitch(snum,p->access_wallnum,0);

                if (ud.multimode < 2 || (ud.multimode > 1 && !(GametypeFlags[ud.coop] & GAMETYPE_ACCESSATSTART)))
                {
                    keypal = wall[p->access_wallnum].pal;
                    switch (keypal)
                    {
                    case 0:
                        Bsprintf(keyname, "Blue");
                        ud.got_access &= (0xffff - 0x1);
                        break;
                    case 21:
                        Bsprintf(keyname, "Red");
                        ud.got_access &= (0xffff - 0x2);
                        break;
                    case 23:
                        Bsprintf(keyname, "Yellow");
                        ud.got_access &= (0xffff - 0x4);
                        break;
                    }
                }
            }

            if (ud.multimode > 1 && !(GametypeFlags[ud.coop] & GAMETYPE_ACCESSATSTART))
            {
                Bsprintf(tempbuf, "^12%s^12 has used a ^%d%s^12 key.", &g_player[snum].user_name[0], keypal, keyname);
                G_AddUserQuote(tempbuf);
            }
        }

        if (p->access_incs > 20)
        {
            p->access_incs = 0;
            p->weapon_pos = 10;
            p->kickback_pic = 0;
        }
    }

    if (p->cursectnum >= 0 && p->scuba_on == 0 && sector[p->cursectnum].lotag == ST_2_UNDERWATER)
    {
        if (p->scuba_amount > 0)
        {
            p->scuba_on = 1;
            p->inven_icon = 6;
            P_DoQuote(76,p);
        }
        else
        {
            if (p->airleft > 0)
                p->airleft--;
            else
            {
                p->extra_extra8 += 32;
                if (p->last_extra < (p->max_player_health>>1) && (p->last_extra&3) == 0)
                    A_PlaySound(DUKE_LONGTERM_PAIN,p->i);
            }
        }
    }
    else if (p->scuba_amount > 0 && p->scuba_on)
    {
        p->scuba_amount--;
        if (p->scuba_amount == 0)
        {
            p->scuba_on = 0;
            P_SelectNextInvItem(p);
        }
    }

    if (p->knuckle_incs)
    {
        p->knuckle_incs ++;
        if (p->knuckle_incs==10)
        {
            if ((int32_t)totalclock > 1024)
                if (snum == screenpeek || GTFLAGS(GAMETYPE_COOPSOUND))
                {

                    if (rand()&1)
                        A_PlaySound(DUKE_CRACK,p->i);
                    else A_PlaySound(DUKE_CRACK2,p->i);

                }

            A_PlaySound(DUKE_CRACK_FIRST,p->i);

        }
        else if (p->knuckle_incs == 22 || TEST_SYNC_KEY(g_player[snum].inputBits->bits, SK_FIRE))
            p->knuckle_incs=0;

        return 1;
    }
    return 0;
}

short WeaponPickupSprites[MAX_WEAPONS] = { KNEE__STATIC, FIRSTGUNSPRITE__STATIC, SHOTGUNSPRITE__STATIC,
        CHAINGUNSPRITE__STATIC, RPGSPRITE__STATIC, HEAVYHBOMB__STATIC, SHRINKERSPRITE__STATIC, DEVISTATORSPRITE__STATIC,
        TRIPBOMBSPRITE__STATIC, FREEZESPRITE__STATIC, HEAVYHBOMB__STATIC, SHRINKERSPRITE__STATIC
                                         };
// this is used for player deaths
void P_DropWeapon(DukePlayer_t *p)
{
    int snum = sprite[p->i].yvel, cw = aplWeaponWorksLike[p->curr_weapon][snum];

    if (cw < 1 || cw >= MAX_WEAPONS) return;

    if (cw)
    {
        if (krand()&1)
            A_Spawn(p->i,WeaponPickupSprites[cw]);
        else switch (cw)
            {
            case RPG_WEAPON:
            case HANDBOMB_WEAPON:
                A_Spawn(p->i,EXPLOSION2);
                break;
            }
    }
}

void P_UpdatePosWhenViewingCam(DukePlayer_t* pPlayer)
{
    if (oldnet_predicting)
        return;

    int const newOwner = pPlayer->newowner;
    //pPlayer->pos = sprite[newOwner].pos;
    pPlayer->q16ang = fix16_from_int(sprite[newOwner].ang);
    pPlayer->vel.x = 0;
    pPlayer->vel.y = 0;
    sprite[pPlayer->i].xvel = 0;
    pPlayer->look_ang = 0;
    pPlayer->rotscrnang = 0;
}

static void P_ProcessWeapon(int playerNum)
{
    if (oldnet_predicting)
        return;

    DukePlayer_t* p = g_player[playerNum].ps;
    unsigned int sb_snum = g_player[playerNum].inputBits->bits;
    short* kb = &p->kickback_pic;
    bool shrunk = (sprite[p->i].yrepeat < 32);

    if (p->weapon_pos != 0)
    {
        if (p->weapon_pos == -9)
        {
            if (p->last_weapon >= 0)
            {
                p->weapon_pos = 10;
                //                if(p->curr_weapon == KNEE_WEAPON) *kb = 1;
                p->last_weapon = -1;
            }
            else if (p->holster_weapon == 0)
                p->weapon_pos = 10;
        }
        else p->weapon_pos--;
    }

    if (TEST_SYNC_KEY(sb_snum, SK_FIRE))
        //    if ((sb_snum&(1<<2)))
    {
        Gv_SetVar(g_iReturnVarID, 0, p->i, playerNum);
        X_OnEvent(EVENT_PRESSEDFIRE, p->i, playerNum, -1);
        if (Gv_GetVar(g_iReturnVarID, p->i, playerNum) != 0)
            sb_snum &= ~BIT(SK_FIRE);
    }

    if (TEST_SYNC_KEY(sb_snum, SK_HOLSTER))   // 'Holster Weapon
    {
        Gv_SetVar(g_iReturnVarID, 0, p->i, playerNum);

        P_SetWeaponGamevars(playerNum, p);

        X_OnEvent(EVENT_HOLSTER, p->i, playerNum, -1);
        if (Gv_GetVar(g_iReturnVarID, p->i, playerNum) == 0)
        {
            if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_HOLSTER_CLEARS_CLIP)
            {
                if (p->ammo_amount[p->curr_weapon] > aplWeaponClip[p->curr_weapon][playerNum]
                    && (p->ammo_amount[p->curr_weapon] % aplWeaponClip[p->curr_weapon][playerNum]) != 0)
                {
                    p->ammo_amount[p->curr_weapon] -=
                        p->ammo_amount[p->curr_weapon] % aplWeaponClip[p->curr_weapon][playerNum];
                    (*kb) = aplWeaponTotalTime[p->curr_weapon][playerNum];
                    sb_snum &= ~BIT(SK_FIRE); // not firing...
                }
                return;
            }
        }
    }

    if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_GLOWS)
        p->random_club_frame += 64; // Glowing

    // this is a hack for WEAPON_FIREEVERYOTHER
    if (ActorExtra[p->i].temp_data[7])
    {
        ActorExtra[p->i].temp_data[7]--;
        if (p->last_weapon == -1 && ActorExtra[p->i].temp_data[7] != 0 && ((ActorExtra[p->i].temp_data[7] & 1) == 0))
        {
            if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_AMMOPERSHOT)
            {
                if (p->ammo_amount[p->curr_weapon] > 0)
                    p->ammo_amount[p->curr_weapon]--;
                else
                {
                    ActorExtra[p->i].temp_data[7] = 0;
                    P_CheckWeapon(p);
                }
            }
            if (ActorExtra[p->i].temp_data[7] != 0)
            {
                A_Shoot(p->i, aplWeaponShoots[p->curr_weapon][playerNum]);
            }
        }
    }

    if (p->rapid_fire_hold == 1)
    {
        if (TEST_SYNC_KEY(sb_snum, SK_FIRE)) return;
        p->rapid_fire_hold = 0;
    }
    if (shrunk || p->tipincs || p->access_incs)
        sb_snum &= ~BIT(SK_FIRE);
    else if (shrunk == 0 && (sb_snum & (1 << 2)) && (*kb) == 0 && p->fist_incs == 0 &&
        p->last_weapon == -1 && (p->weapon_pos == 0 || p->holster_weapon == 1))
    {
        p->crack_time = 777;

        if (p->holster_weapon == 1)
        {
            if (p->last_pissed_time <= (26 * 218) && p->weapon_pos == -9)
            {
                p->holster_weapon = 0;
                p->weapon_pos = 10;
                P_DoQuote(74, p);
            }
        }
        else
        {
            Gv_SetVar(g_iReturnVarID, 0, p->i, playerNum);

            P_SetWeaponGamevars(playerNum, p);

            X_OnEvent(EVENT_FIRE, p->i, playerNum, -1);
            if (Gv_GetVar(g_iReturnVarID, p->i, playerNum) == 0)
            {
                switch (aplWeaponWorksLike[p->curr_weapon][playerNum])
                {
                case HANDBOMB_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);

                    p->hbomb_hold_delay = 0;
                    if (p->ammo_amount[p->curr_weapon] > 0)
                    {
                        (*kb) = 1;
                        if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                        {
                            A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                        }
                    }
                    break;

                case HANDREMOTE_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);
                    p->hbomb_hold_delay = 0;
                    (*kb) = 1;
                    if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                    {
                        A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                    }
                    break;

                case SHOTGUN_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);
                    if (p->ammo_amount[p->curr_weapon] > 0)
                    {
                        (*kb) = 1;
                        if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                        {
                            A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                        }
                    }
                    break;

                case TRIPBOMB_WEAPON:
                    if (p->ammo_amount[p->curr_weapon] > 0)
                    {
                        hitdata_t hit;

                        hitscan(&p->pos,
                            p->cursectnum, sintable[(fix16_to_int(p->q16ang) + 512) & 2047],
                            sintable[fix16_to_int(p->q16ang) & 2047], fix16_to_int(F16(100) - p->q16horiz - p->q16horizoff) * 32,
                            &hit, CLIPMASK1);

                        if (hit.sect < 0 || hit.sprite >= 0)
                            break;

                        if (hit.wall >= 0 && sector[hit.sect].lotag > 2)
                            break;

                        if (hit.wall >= 0 && wall[hit.wall].overpicnum >= 0)
                            if (wall[hit.wall].overpicnum == BIGFORCE)
                                break;

                        int spr = headspritesect[hit.sect];
                        while (spr >= 0)
                        {
                            if (sprite[spr].picnum == TRIPBOMB &&
                                klabs(sprite[spr].z - hit.pos.z) < (12 << 8) && ((sprite[spr].x - hit.pos.x) * (sprite[spr].x - hit.pos.x) + (sprite[spr].y - hit.pos.y) * (sprite[spr].y - hit.pos.y)) < (290 * 290))
                                break;
                            spr = nextspritesect[spr];
                        }

                        if (spr == -1 && hit.wall >= 0 && (wall[hit.wall].cstat & 16) == 0)
                            if ((wall[hit.wall].nextsector >= 0 && sector[wall[hit.wall].nextsector].lotag <= 2) || (wall[hit.wall].nextsector == -1 && sector[hit.sect].lotag <= 2))
                                if (((hit.pos.x - p->pos.x) * (hit.pos.x - p->pos.x) + (hit.pos.y - p->pos.y) * (hit.pos.y - p->pos.y)) < (290 * 290))
                                {
                                    p->pos.z = p->opos.z;
                                    p->vel.z = 0;
                                    (*kb) = 1;
                                    if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                                    {
                                        A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                                    }
                                }
                    }
                    break;

                case PISTOL_WEAPON:
                case CHAINGUN_WEAPON:
                case SHRINKER_WEAPON:
                case GROW_WEAPON:
                case FREEZE_WEAPON:
                case RPG_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);
                    if (p->ammo_amount[p->curr_weapon] > 0)
                    {
                        (*kb) = 1;
                        if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                        {
                            A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                        }
                    }
                    break;

                case DEVISTATOR_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);
                    if (p->ammo_amount[p->curr_weapon] > 0)
                    {
                        (*kb) = 1;
                        p->hbomb_hold_delay = !p->hbomb_hold_delay;
                        if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                        {
                            A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                        }
                    }
                    break;

                case KNEE_WEAPON:
                    X_OnEvent(EVENT_FIREWEAPON, p->i, playerNum, -1);
                    if (p->quick_kick == 0)
                    {
                        (*kb) = 1;
                        if (aplWeaponInitialSound[p->curr_weapon][playerNum])
                        {
                            A_PlaySound(aplWeaponInitialSound[p->curr_weapon][playerNum], p->i);
                        }
                    }
                    break;
                }
            }
        }
    }
    else if (*kb)
    {
        if (aplWeaponWorksLike[p->curr_weapon][playerNum] == HANDBOMB_WEAPON)
        {
            if (aplWeaponHoldDelay[p->curr_weapon][playerNum] && ((*kb) == aplWeaponFireDelay[p->curr_weapon][playerNum]) && TEST_SYNC_KEY(sb_snum, SK_FIRE))
            {
                p->rapid_fire_hold = 1;
                return;
            }
            (*kb)++;
            if ((*kb) == aplWeaponHoldDelay[p->curr_weapon][playerNum])
            {
                int lPipeBombControl;
                int pipeBombZvel;
                int pipeBombFwdVel;

                p->ammo_amount[p->curr_weapon]--;

                if (p->on_ground && TEST_SYNC_KEY(sb_snum, SK_CROUCH))
                {
                    pipeBombFwdVel = 15;
                    pipeBombZvel = (fix16_to_int(p->q16horiz + p->q16horizoff - F16(100)) * 20);
                }
                else
                {
                    pipeBombFwdVel = 140;
                    pipeBombZvel = -512 - (fix16_to_int(p->q16horiz + p->q16horizoff - F16(100)) * 20);
                }

                int pipeSpriteNum = A_InsertSprite(p->cursectnum,
                    p->pos.x + (sintable[(fix16_to_int(p->q16ang) + 512) & 2047] >> 6),
                    p->pos.y + (sintable[fix16_to_int(p->q16ang) & 2047] >> 6),
                    p->pos.z, aplWeaponShoots[p->curr_weapon][playerNum], -16, 9, 9,
                    fix16_to_int(p->q16ang), (pipeBombFwdVel + (p->hbomb_hold_delay << 5)), pipeBombZvel, p->i, 1);

                lPipeBombControl = Gv_GetVarByLabel("PIPEBOMB_CONTROL", PIPEBOMB_REMOTE, -1, playerNum);

                if (lPipeBombControl & PIPEBOMB_TIMER)
                {
                    int lGrenadeLifetime = Gv_GetVarByLabel("GRENADE_LIFETIME", NAM_GRENADE_LIFETIME, -1, playerNum);
                    int lGrenadeLifetimeVar = Gv_GetVarByLabel("GRENADE_LIFETIME_VAR", NAM_GRENADE_LIFETIME_VAR, -1, playerNum);
                    ActorExtra[pipeSpriteNum].temp_data[7] = lGrenadeLifetime
                        + mulscale(krand(), lGrenadeLifetimeVar, 14)
                        - lGrenadeLifetimeVar;
                    ActorExtra[pipeSpriteNum].temp_data[6] = 1;
                }
                else
                    ActorExtra[pipeSpriteNum].temp_data[6] = 2;

                if (pipeBombFwdVel == 15)
                {
                    sprite[pipeSpriteNum].yvel = 3;
                    sprite[pipeSpriteNum].z += (8 << 8);
                }

                if (A_GetHitscanRange(p->i) < 512)
                {
                    sprite[pipeSpriteNum].ang += 1024;
                    sprite[pipeSpriteNum].zvel /= 3;
                    sprite[pipeSpriteNum].xvel /= 3;
                }
                p->hbomb_on = 1;
            }
            else if ((*kb) < aplWeaponHoldDelay[p->curr_weapon][playerNum] && TEST_SYNC_KEY(sb_snum, SK_FIRE))
            {
                p->hbomb_hold_delay++;
            }
            else if ((*kb) > aplWeaponTotalTime[p->curr_weapon][playerNum])
            {
                int lPipeBombControl = Gv_GetVarByLabel("PIPEBOMB_CONTROL", PIPEBOMB_REMOTE, -1, playerNum);

                (*kb) = 0;

                if (lPipeBombControl == PIPEBOMB_REMOTE)
                {
                    p->curr_weapon = HANDREMOTE_WEAPON;
                    p->last_weapon = -1;
                    p->weapon_pos = 10;
                    Gv_SetVar(g_iWeaponVarID, p->curr_weapon, p->i, playerNum);
                }
                else
                {
                    p->weapon_pos = 10;
                    P_CheckWeapon(p);
                }
            }
        }
        else if (aplWeaponWorksLike[p->curr_weapon][playerNum] == HANDREMOTE_WEAPON)
        {
            (*kb)++;

            if ((*kb) == aplWeaponFireDelay[p->curr_weapon][playerNum])
            {
                if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_BOMB_TRIGGER)
                {
                    p->hbomb_on = 0;
                }
                if (aplWeaponShoots[p->curr_weapon][playerNum] != 0)
                {
                    if (!(aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_NOVISIBLE))
                    {
                        lastvisinc = (int32_t)totalclock + 32;
                        p->visibility = 0;
                    }

                    P_SetWeaponGamevars(playerNum, p);

                    A_Shoot(p->i, aplWeaponShoots[p->curr_weapon][playerNum]);
                }
            }

            if ((*kb) >= aplWeaponTotalTime[p->curr_weapon][playerNum])
            {
                int lPipeBombControl = Gv_GetVarByLabel("PIPEBOMB_CONTROL", PIPEBOMB_REMOTE, -1, playerNum);
                (*kb) = 0;
                if ((p->ammo_amount[HANDBOMB_WEAPON] > 0) && lPipeBombControl == PIPEBOMB_REMOTE)
                    P_AddWeapon(p, HANDBOMB_WEAPON, 1);
                else
                    P_CheckWeapon(p);
            }
        }
        else
        {
            // the basic weapon...
            (*kb)++;

            if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_CHECKATRELOAD)
            {
                if (aplWeaponWorksLike[p->curr_weapon][playerNum] == TRIPBOMB_WEAPON)
                {
                    if ((*kb) >= aplWeaponTotalTime[p->curr_weapon][playerNum])
                    {
                        (*kb) = 0;
                        P_CheckWeapon(p);
                        p->weapon_pos = -9;
                    }
                }
                else if (*kb >= aplWeaponReload[p->curr_weapon][playerNum])
                    P_CheckWeapon(p);
            }
            else if (aplWeaponWorksLike[p->curr_weapon][playerNum] != KNEE_WEAPON && *kb >= aplWeaponFireDelay[p->curr_weapon][playerNum])
                P_CheckWeapon(p);

            if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_STANDSTILL
                && *kb < (aplWeaponFireDelay[p->curr_weapon][playerNum] + 1))
            {
                p->pos.z = p->opos.z;
                p->vel.z = 0;
            }
            if (*kb == aplWeaponSound2Time[p->curr_weapon][playerNum])
            {
                if (aplWeaponSound2Sound[p->curr_weapon][playerNum])
                {
                    A_PlaySound(aplWeaponSound2Sound[p->curr_weapon][playerNum], p->i);
                }
            }
            if (*kb == aplWeaponSpawnTime[p->curr_weapon][playerNum])
                P_DoWeaponSpawn(p);

            if ((*kb) >= aplWeaponTotalTime[p->curr_weapon][playerNum])
            {
                if (/*!(aplWeaponFlags[p->curr_weapon][snum] & WEAPON_CHECKATRELOAD) && */ p->reloading == 1 ||
                    (aplWeaponReload[p->curr_weapon][playerNum] > aplWeaponTotalTime[p->curr_weapon][playerNum] && p->ammo_amount[p->curr_weapon] > 0
                        && (aplWeaponClip[p->curr_weapon][playerNum]) && (((p->ammo_amount[p->curr_weapon] % (aplWeaponClip[p->curr_weapon][playerNum])) == 0))))
                {
                    int i = aplWeaponReload[p->curr_weapon][playerNum] - aplWeaponTotalTime[p->curr_weapon][playerNum];

                    p->reloading = 1;

                    if ((*kb) != (aplWeaponTotalTime[p->curr_weapon][playerNum]))
                    {
                        if ((*kb) == (aplWeaponTotalTime[p->curr_weapon][playerNum] + 1))
                        {
                            if (aplWeaponReloadSound1[p->curr_weapon][playerNum])
                                A_PlaySound(aplWeaponReloadSound1[p->curr_weapon][playerNum], p->i);
                        }
                        else if (((*kb) == (aplWeaponReload[p->curr_weapon][playerNum] - (i / 3)) && !(aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RELOAD_TIMING)) ||
                            ((*kb) == (aplWeaponReload[p->curr_weapon][playerNum] - i + 4) && (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RELOAD_TIMING)))
                        {
                            if (aplWeaponReloadSound2[p->curr_weapon][playerNum])
                                A_PlaySound(aplWeaponReloadSound2[p->curr_weapon][playerNum], p->i);
                        }
                        else if ((*kb) >= (aplWeaponReload[p->curr_weapon][playerNum]))
                        {
                            *kb = 0;
                            p->reloading = 0;
                        }
                    }
                }
                else
                {
                    if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_AUTOMATIC &&
                        (aplWeaponWorksLike[p->curr_weapon][playerNum] == KNEE_WEAPON ? 1 : p->ammo_amount[p->curr_weapon] > 0))
                    {
                        if (TEST_SYNC_KEY(sb_snum, SK_FIRE))
                        {
                            if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RANDOMRESTART)
                                *kb = 1 + (krand() & 3);
                            else *kb = 1;
                        }
                        else *kb = 0;
                    }
                    else *kb = 0;

                    if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RESET &&
                        ((aplWeaponWorksLike[p->curr_weapon][playerNum] == KNEE_WEAPON) ? 1 : p->ammo_amount[p->curr_weapon] > 0))
                    {
                        if (TEST_SYNC_KEY(sb_snum, SK_FIRE)) *kb = 1;
                        else *kb = 0;
                    }
                }
            }
            else if (*kb >= aplWeaponFireDelay[p->curr_weapon][playerNum] && (*kb) < aplWeaponTotalTime[p->curr_weapon][playerNum]
                && ((aplWeaponWorksLike[p->curr_weapon][playerNum] == KNEE_WEAPON) ? 1 : p->ammo_amount[p->curr_weapon] > 0))
            {
                if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_AUTOMATIC)
                {
                    if (!(aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_SEMIAUTO))
                    {
                        if (TEST_SYNC_KEY(sb_snum, SK_FIRE) == 0 && aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RESET)
                            *kb = 0;
                        if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_FIREEVERYTHIRD)
                        {
                            if (((*(kb)) % 3) == 0)
                            {
                                P_FireWeapon(p);
                                P_DoWeaponSpawn(p);
                            }
                        }
                        else if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_FIREEVERYOTHER)
                        {
                            P_FireWeapon(p);
                            P_DoWeaponSpawn(p);
                        }
                        else
                        {
                            if (*kb == aplWeaponFireDelay[p->curr_weapon][playerNum])
                            {
                                P_FireWeapon(p);
                                //                                P_DoWeaponSpawn(p);
                            }
                        }
                        if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_RESET &&
                            (*kb) > aplWeaponTotalTime[p->curr_weapon][playerNum] - aplWeaponHoldDelay[p->curr_weapon][playerNum] &&
                            ((aplWeaponWorksLike[p->curr_weapon][playerNum] == KNEE_WEAPON) ? 1 : p->ammo_amount[p->curr_weapon] > 0))
                        {
                            if (TEST_SYNC_KEY(sb_snum, SK_FIRE)) *kb = 1;
                            else *kb = 0;
                        }
                    }
                    else
                    {
                        if (aplWeaponFlags[p->curr_weapon][playerNum] & WEAPON_FIREEVERYOTHER)
                        {
                            P_FireWeapon(p);
                            P_DoWeaponSpawn(p);
                        }
                        else
                        {
                            if (*kb == aplWeaponFireDelay[p->curr_weapon][playerNum])
                            {
                                P_FireWeapon(p);
                                //                                P_DoWeaponSpawn(p);
                            }
                        }
                    }
                }
                else if (*kb == aplWeaponFireDelay[p->curr_weapon][playerNum])
                    P_FireWeapon(p);
            }
        }
    }
}

int g_numObituaries = 0;
int g_numSelfObituaries = 0;

static int P_HandleSpecialTimers(int playerNum) // Ret: 0 = Pass, 1 = Halt ProcessInput
{
    if (oldnet_predicting)
        return 0;

    DukePlayer_t* p = g_player[playerNum].ps;

    p->player_par++;

    if (p->timebeforeexit > 1 && ((p->last_extra > 0) || ud.limit_hit == 1))
    {
        p->timebeforeexit--;
        if (p->timebeforeexit == 26 * 5 && (ud.limit_hit == 0))
        {
            FX_StopAllSounds();
            S_ClearSoundLocks();
            if (p->customexitsound >= 0)
            {
                S_PlaySound(p->customexitsound);
                P_DoQuote(102, p);
            }
        }
        else if (p->timebeforeexit == 1)
        {
            Net_EndOfLevel();
            return 1;
        }
    }

    if (p->loogcnt > 0) p->loogcnt--;
    else p->loogcnt = 0;

    if (p->pals_time >= 0)
        p->pals_time--;

    if (p->fta > 0)
    {
        p->fta--;
        if (p->fta == 0)
        {
            pub = NUMPAGES;
            pus = NUMPAGES;
            p->ftq = 0;
        }
    }

    if (g_levelTextTime > 0)
        g_levelTextTime--;

    return 0;
}

static int P_HandleFist(int playerNum)
{
    if (oldnet_predicting)
        return 0;

    DukePlayer_t* p = g_player[playerNum].ps;
    int i;

    if (p->fist_incs)
    {
        // the fist puching the end-of-level thing...
        p->fist_incs++;
        if (p->fist_incs == 28)
        {
            if (ud.recstat == 1) G_CloseDemoWrite();
            S_PlaySound(PIPEBOMB_EXPLODE);
            p->pals[0] = 64;
            p->pals[1] = 64;
            p->pals[2] = 64;
            p->pals_time = 48;
        }
        if (p->fist_incs > 42)
        {
            if (p->buttonpalette && ud.from_bonus == 0)
            {
                ud.from_bonus = ud.level_number + 1;
                if (ud.secretlevel > 0 && ud.secretlevel < MAXLEVELS) ud.level_number = ud.secretlevel - 1;
                ud.m_level_number = ud.level_number;
            }
            else
            {
                if (ud.from_bonus)
                {
                    ud.level_number = ud.from_bonus;
                    ud.m_level_number = ud.level_number;
                    ud.from_bonus = 0;
                }
                else
                {
                    if (ud.level_number == ud.secretlevel && ud.from_bonus > 0)
                        ud.level_number = ud.from_bonus;
                    else ud.level_number++;

                    if (ud.level_number > MAXLEVELS - 1)
                        ud.level_number = 0;
                    ud.m_level_number = ud.level_number;
                }
            }

            TRAVERSE_CONNECT(i)
                g_player[i].ps->gm = MODE_EOL;

            p->fist_incs = 0;

            return 1;
        }
    }

    return 0;
}

static void P_HandleFragged(int snum)
{
    if (oldnet_predicting)
        return;

    DukePlayer_t* p = g_player[snum].ps;
    DukePlayer_t* frag_p = g_player[p->frag_ps].ps;
    int playerSprite = g_player[snum].ps->i;

    if ((p->frag_ps != snum) && ud.limit_hit != 1)
    {
        if (GTFLAGS(GAMETYPE_TDM) && frag_p->team == g_player[snum].ps->team)
        {
            frag_p->fraggedself++;
            teams[frag_p->team].suicides++;
        }
        else
        {
            frag_p->frag++;
            g_player[p->frag_ps].frags[snum]++;
            teams[frag_p->team].frags++;
        }

        if (snum == screenpeek)
        {
            Bsprintf(ScriptQuotes[115], "KILLED BY %s", &g_player[p->frag_ps].user_name[0]);
            P_DoQuote(115, p);
        }
        else
        {
            Bsprintf(ScriptQuotes[116], "KILLED %s", &g_player[snum].user_name[0]);
            P_DoQuote(116, frag_p);
        }

        if (ud.obituaries)
        {
            //DynamicTileMap[ActorExtra[p->i].picnum]
            Bsprintf(tempbuf, ScriptQuotes[FIRST_OBITUARY_QUOTE + (krand() % g_numObituaries)],
                &g_player[p->frag_ps].user_name[0],
                &g_player[snum].user_name[0]);

            G_AddUserQuote(tempbuf);
        }
        else krand();

        if (GTFLAGS(GAMETYPE_TDM))
        {
            int fraggedbyteam = frag_p->team;

            if ((teams[fraggedbyteam].frags - teams[fraggedbyteam].suicides) >= ud.fraglimit && ud.fraglimit != 0)
            {
                // Fraglimit hit
                Bsprintf(tempbuf, "^%d%s^08 team wins!", teams[fraggedbyteam].palnum, teams[fraggedbyteam].name);
                G_AddUserQuote(tempbuf);

                S_PlaySound(PIPEBOMB_EXPLODE);

                for (int i = 0; i < MAXPLAYERS; i++)
                {
                    if (g_player[i].ps == NULL || !g_player[i].connected)
                        continue;

                    if (g_player[i].ps->team == fraggedbyteam) // Flash screens of all winning players
                    {
                        g_player[i].ps->pals[0] = 64;
                        g_player[i].ps->pals[1] = 64;
                        g_player[i].ps->pals[2] = 64;
                        g_player[i].ps->pals_time = 48;
                    }
                }

                ud.from_bonus = 0;
                ud.limit_hit = 1;
                frag_p->timebeforeexit = 26 * 4;
            }
        }
        else if ((frag_p->frag - frag_p->fraggedself) >= ud.fraglimit && ud.fraglimit != 0)
        {
            // Fraglimit hit
            Bsprintf(tempbuf, "^08%s^08 wins!", &g_player[p->frag_ps].user_name[0]);
            G_AddUserQuote(tempbuf);

            S_PlaySound(PIPEBOMB_EXPLODE);
            frag_p->pals[0] = 64;
            frag_p->pals[1] = 64;
            frag_p->pals[2] = 64;
            frag_p->pals_time = 48;

            ud.from_bonus = 0;
            ud.limit_hit = 1;
            frag_p->timebeforeexit = 26 * 4;
        }
    }
    else if (ud.limit_hit != 1)
    {
        if (ActorExtra[playerSprite].picnum != APLAYERTOP)
        {
            if (GTFLAGS(GAMETYPE_TDM))
            {
                teams[p->team].suicides++;
            }

            p->fraggedself++;
            if (A_CheckEnemyTile(sprite[p->wackedbyactor].picnum))
                Bsprintf(tempbuf, ScriptQuotes[FIRST_OBITUARY_QUOTE + (krand() % g_numObituaries)], "A monster", &g_player[snum].user_name[0]);
            else if (ActorExtra[playerSprite].picnum == NUKEBUTTON)
                Bsprintf(tempbuf, "^02%s^02 tried to leave", &g_player[snum].user_name[0]);
            else
            {
                // random suicide death string
                Bsprintf(tempbuf, ScriptQuotes[FIRST_SUICIDE_QUOTE + (krand() % g_numSelfObituaries)], &g_player[snum].user_name[0]);
            }
        }
        else Bsprintf(tempbuf, "^02%s^02 switched to team %d", &g_player[snum].user_name[0], p->team + 1);

        if (ud.obituaries)
        {
            G_AddUserQuote(tempbuf);
        }
    }

    if (myconnectindex == connecthead)
    {
        Bsprintf(tempbuf, "frag %d killed %d\n", p->frag_ps + 1, snum + 1);
        sendscore(tempbuf);
    }

    p->frag_ps = snum;
    pus = NUMPAGES;
}

static void P_HandleDeath(int snum)
{
    if (oldnet_predicting)
        return;

    DukePlayer_t* p = g_player[snum].ps;
    int playerSprite = g_player[snum].ps->i;

    spritetype* s = &sprite[playerSprite];

    if (p->dead_flag == 0)
    {
        if (s->pal != 1)
        {
            p->pals[0] = 63;
            p->pals[1] = 0;
            p->pals[2] = 0;
            p->pals_time = 63;
            p->pos.z -= (16 << 8);
            s->z -= (16 << 8);
        }

        if (ud.recstat == 1 && ud.multimode < 2)
            G_CloseDemoWrite();

        if (s->pal != 1)
        {
            p->dead_flag = (512 - ((krand() & 1) << 10) + (krand() & 255) - 512) & 2047;
            if (p->dead_flag == 0)
                p->dead_flag++;
        }

        p->jetpack_on = 0;
        p->holoduke_on = -1;

        S_StopEnvSound(DUKE_JETPACK_IDLE, playerSprite);
        if (p->scream_voice > FX_Ok)
        {
            FX_StopSound(p->scream_voice);
            S_Cleanup();
            // S_TestSoundCallback(DUKE_SCREAM);
            p->scream_voice = FX_Ok;
        }

        if (s->pal != 1 && (s->cstat & 32768) == 0) s->cstat = 0;

        if (ud.multimode > 1 && (s->pal != 1 || (s->cstat & 32768)))
        {
            P_HandleFragged(snum);
        }
    }
}

static void P_HandleDamagingFloors(int snum)
{
    if (oldnet_predicting)
        return;

    DukePlayer_t* p = g_player[snum].ps;
    int playerSprite = g_player[snum].ps->i;

    spritetype* s = &sprite[playerSprite];

    int truefdist = klabs(p->pos.z - p->truefz);
    int floorPic = sector[s->sectnum].floorpicnum;
    int useBoot = 0;

    if (floorPic == PURPLELAVA || sector[s->sectnum].ceilingpicnum == PURPLELAVA)
    {
        if (p->boot_amount > 0)
        {
            p->boot_amount--;
            p->inven_icon = 7;
            if (p->boot_amount <= 0)
                P_SelectNextInvItem(p);
        }
        else
        {
            if (!A_CheckSoundPlaying(playerSprite, DUKE_LONGTERM_PAIN))
                A_PlaySound(DUKE_LONGTERM_PAIN, playerSprite);
            p->pals[0] = 0;
            p->pals[1] = 8;
            p->pals[2] = 0;
            p->pals_time = 32;
            s->extra--;
        }
    }

    if (p->on_ground && truefdist <= PHEIGHT + (16 << 8))
    {
        switch (DynamicTileMap[floorPic])
        {
        case HURTRAIL__STATIC:
            if (rnd(32))
            {
                if (p->boot_amount > 0)
                    useBoot = 1;
                else
                {
                    if (!A_CheckSoundPlaying(playerSprite, DUKE_LONGTERM_PAIN))
                        A_PlaySound(DUKE_LONGTERM_PAIN, playerSprite);
                    p->pals[0] = 64;
                    p->pals[1] = 64;
                    p->pals[2] = 64;
                    p->pals_time = 32;
                    s->extra -= 1 + (krand() & 3);
                    if (!A_CheckSoundPlaying(playerSprite, SHORT_CIRCUIT))
                        A_PlaySound(SHORT_CIRCUIT, playerSprite);
                }
            }
            break;
        case FLOORSLIME__STATIC:
            if (rnd(16))
            {
                if (p->boot_amount > 0)
                    useBoot = 1;
                else
                {
                    if (!A_CheckSoundPlaying(playerSprite, DUKE_LONGTERM_PAIN))
                        A_PlaySound(DUKE_LONGTERM_PAIN, playerSprite);

                    p->pals[0] = 0;
                    p->pals[1] = 8;
                    p->pals[2] = 0;
                    p->pals_time = 32;
                    s->extra -= 1 + (krand() & 3);
                }
            }
            break;
        case FLOORPLASMA__STATIC:
            if (rnd(32))
            {
                if (p->boot_amount > 0)
                    useBoot = 1;
                else
                {
                    if (!A_CheckSoundPlaying(playerSprite, DUKE_LONGTERM_PAIN))
                        A_PlaySound(DUKE_LONGTERM_PAIN, playerSprite);
                    p->pals[0] = 8;
                    p->pals[1] = 0;
                    p->pals[2] = 0;
                    p->pals_time = 32;
                    s->extra -= 1 + (krand() & 3);
                }
            }
            break;
        }
    }

    if (useBoot)
    {
        P_DoQuote(75, p);
        p->boot_amount -= 2;
        if (p->boot_amount <= 0)
            P_SelectNextInvItem(p);
    }
}

void P_ProcessInput(int snum)
{
    DukePlayer_t* p;
    DukePlayer_t* p_nopredict = g_player[snum].ps;
    input_t* syn;

    int playerSprite = g_player[snum].ps->i;
    spritetype* s = &sprite[playerSprite];

    unsigned int sb_snum;
    int psect;
    int psectlotag;
    int shrunk = (s->yrepeat < 32);
    int truefdist;
    int floorZOffset;

    int cz, hz, fz, lz;

    int x, y;
    short tempsect;

    int doubvel;

    if (oldnet_predicting)
    {
        syn = (input_t*)&inputfifo[predictfifoplc & (MOVEFIFOSIZ - 1)][myconnectindex];
        p = &predictedPlayer;
    }
    else
    {
        syn = g_player[snum].inputBits;
        p = g_player[snum].ps;
        X_OnEvent(EVENT_PROCESSINPUT, playerSprite, snum, -1);
    }

    sb_snum = syn->bits;

    if (p_nopredict->cheat_phase > 0)
        sb_snum = 0;

    psect = p->cursectnum;

    if (psect == -1)
    {
        if (!oldnet_predicting && s->extra > 0 && ud.clipping == 0)
        {
            P_QuickKill(p);
            A_PlaySound(SQUISHED, playerSprite);
        }
        psect = 0;
    }

    psectlotag = sector[psect].lotag;
    p->spritebridge = 0;
    p->sbs = 0;

    getzrange_old(p->pos.x, p->pos.y, p->pos.z, psect, &cz, &hz, &fz, &lz, 163L, CLIPMASK0);

    yax_getzsofslope(psect, p->pos.x, p->pos.y, &p->truecz, &p->truefz);

    truefdist = klabs(p->pos.z - p->truefz);

    /* wtf is this supposed to do? */
    if ((lz & 49152) == 16384 && psectlotag == ST_1_ABOVE_WATER && truefdist > PHEIGHT + (16 << 8))
        psectlotag = 0;

    if (!oldnet_predicting)
    {
        ActorExtra[playerSprite].floorz = fz;
        ActorExtra[playerSprite].ceilingz = cz;
    }

    p->oq16horiz = p->q16horiz;
    p->oq16horizoff = p->q16horizoff;

    if (p_nopredict->aim_mode == 0 && p->on_ground && psectlotag != ST_2_UNDERWATER && (sector[psect].floorstat & 2))
    {
        x = p->pos.x + (sintable[(fix16_to_int(p->q16ang) + 512) & 2047] >> 5);
        y = p->pos.y + (sintable[fix16_to_int(p->q16ang) & 2047] >> 5);
        tempsect = psect;
        updatesector(x, y, &tempsect);
        if (tempsect >= 0)
        {
            int florZOfSlope = getflorzofslope(psect, x, y);
            if (psect == tempsect)
                p->q16horizoff += fix16_from_int(mulscale16(p->truefz - florZOfSlope, 160));
            else if (klabs(getflorzofslope(tempsect, x, y) - florZOfSlope) <= (4 << 8))
                p->q16horizoff += fix16_from_int(mulscale16(p->truefz - florZOfSlope, 160));
        }
    }

    if (p->q16horizoff > 0)
    {
        p->q16horizoff -= ((p->q16horizoff >> 3) + fix16_one);
        p->q16horizoff = max(p->q16horizoff, 0);
    }
    else if (p->q16horizoff < 0)
    {
        p->q16horizoff += (((-p->q16horizoff) >> 3) + fix16_one);
        p->q16horizoff = min(p->q16horizoff, 0);
    }

    if (hz >= 0 && (hz & 49152) == 49152)
    {
        hz &= (MAXSPRITES - 1);

        if (sprite[hz].statnum == 1 && sprite[hz].extra >= 0)
        {
            hz = 0;
            cz = p->truecz;
        }
    }

    if (lz >= 0 && (lz & 49152) == 49152)
    {
        int spr = lz & (MAXSPRITES - 1);

        if ((sprite[spr].cstat & 33) == 33)
        {
            if (!oldnet_predicting)
                p->footprintcount = 0;

            psectlotag = 0;
            p->spritebridge = 1;
            p->sbs = spr;
        }
        else if (A_CheckEnemySprite(&sprite[spr]) && sprite[spr].xrepeat > 24 && klabs(s->z - sprite[spr].z) < (84 << 8))
        {
            int ang = getangle(sprite[spr].x - p->pos.x, sprite[spr].y - p->pos.y);
            p->vel.x -= sintable[(ang + 512) & 2047] << 4;
            p->vel.y -= sintable[ang & 2047] << 4;
        }
    }

    if (!oldnet_predicting)
    {
        if (s->extra > 0) P_IncurDamage(p);
        else
        {
            s->extra = 0;
            p->shield_amount = 0;
        }

        p->last_extra = s->extra;
    }

    if (P_HandleFist(snum))
        return;

    if (P_HandleSpecialTimers(snum))
        return;

    if (s->extra <= 0)
    {
        P_HandleDeath(snum);

        if (psectlotag == ST_2_UNDERWATER)
        {
            if (p_nopredict->on_warping_sector == 0)
            {
                if (klabs(p->pos.z - fz) > (PHEIGHT >> 1))
                    p->pos.z += 348;
            }
            else
            {
                s->z -= 512;
                s->zvel = -348;
            }

            clipmove_old(&p->pos.x, &p->pos.y,
                &p->pos.z, &p->cursectnum,
                0, 0, 164L, (4L << 8), (4L << 8), CLIPMASK0);
        }

        p->opos.x = p->pos.x;
        p->opos.y = p->pos.y;
        p->opos.z = p->pos.z;
        p->oq16ang = p->q16ang;
        p->opyoff = p->pyoff;

        p->q16horiz = F16(100);
        p->q16horizoff = 0;

        updatesector(p->pos.x, p->pos.y, &p->cursectnum);

        pushmove_old(&p->pos.x, &p->pos.y, &p->pos.z, &p->cursectnum, 128L, (4L << 8), (20L << 8), CLIPMASK0);

        if (fz > cz + (16 << 8) && s->pal != 1)
            p->rotscrnang = (p->dead_flag + ((fz + p->pos.z) >> 7)) & 2047;

        p->on_warping_sector = 0;

        return;
    }

    if (!oldnet_predicting)
    {
        if (p->transporter_hold > 0)
        {
            p->transporter_hold--;
            if (p->transporter_hold == 0 && p->on_warping_sector)
                p->transporter_hold = 2;
        }
        if (p->transporter_hold < 0)
            p->transporter_hold++;
    }

    if (p_nopredict->newowner >= 0) // Using viewscreen
    {
        P_UpdatePosWhenViewingCam(p);

        P_DoCounters(p);

        if (*aplWeaponWorksLike[p_nopredict->curr_weapon] == HANDREMOTE_WEAPON)
            P_ProcessWeapon(snum);

        return;
    }

    doubvel = TICSPERFRAME;

    if (p->rotscrnang > 0) p->rotscrnang -= ((p->rotscrnang >> 1) + 1);
    else if (p->rotscrnang < 0) p->rotscrnang += (((-p->rotscrnang) >> 1) + 1);

    p->look_ang -= (p->look_ang >> 2);

    if (TEST_SYNC_KEY(sb_snum, SK_LOOK_LEFT))
    {
        // look_left
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_LOOKLEFT, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            p->look_ang -= 152;
            p->rotscrnang += 24;
        }
    }

    if (TEST_SYNC_KEY(sb_snum, SK_LOOK_RIGHT))
    {
        // look_right
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_LOOKRIGHT, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            p->look_ang += 152;
            p->rotscrnang -= 24;
        }
    }

    if (p_nopredict->on_crane >= 0)
        goto HORIZONLY;

    if (s->xvel < 32 || p->on_ground == 0 || p->bobcounter == 1024)
    {
        if ((p->weapon_sway & 2047) > (1024 + 96))
            p->weapon_sway -= 96;
        else if ((p->weapon_sway & 2047) < (1024 - 96))
            p->weapon_sway += 96;
        else p->weapon_sway = 1024;
    }
    else p->weapon_sway = p->bobcounter;

    if (!oldnet_predicting)
    {
        s->xvel =
            ksqrt((p->pos.x - p->bobposx) * (p->pos.x - p->bobposx) + (p->pos.y - p->bobposy) * (p->pos.y - p->bobposy));
        if (p->on_ground) p->bobcounter += sprite[playerSprite].xvel >> 1;
    }

    if (ud.clipping == 0 && (sector[p->cursectnum].floorpicnum == MIRROR || p->cursectnum < 0 || p->cursectnum >= MAXSECTORS))
    {
        p->pos.x = p->opos.x;
        p->pos.y = p->opos.y;
    }
    else
    {
        p->opos.x = p->pos.x;
        p->opos.y = p->pos.y;
    }

    p->bobposx = p->pos.x;
    p->bobposy = p->pos.y;

    p->opos.z = p->pos.z;
    p->opyoff = p->pyoff;
    p->oq16ang = p->q16ang;

    if (p_nopredict->one_eighty_count < 0)
    {
        p->one_eighty_count += 128;
        p->q16ang += F16(128);
    }

    // Shrinking code

    floorZOffset = 40;

    if (psectlotag == ST_2_UNDERWATER)
    {
        // under water
        p->jumping_counter = 0;

        p->pycount += 32;
        p->pycount &= 2047;
        p->pyoff = sintable[p->pycount] >> 7;

        if (!oldnet_predicting && !A_CheckSoundPlaying(playerSprite, DUKE_UNDERWATER))
            A_PlaySound(DUKE_UNDERWATER, playerSprite);

        if (TEST_SYNC_KEY(sb_snum, SK_JUMP))
        {
            Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
            X_OnEvent(EVENT_SWIMUP, playerSprite, snum, -1);
            if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
            {
                // jump
                if (p->vel.z > 0) p->vel.z = 0;
                p->vel.z -= 348;
                if (p->vel.z < -(256 * 6)) p->vel.z = -(256 * 6);
            }
        }
        else if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))
        {
            Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
            X_OnEvent(EVENT_SWIMDOWN, playerSprite, snum, -1);
            if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
            {
                // crouch
                if (p->vel.z < 0) p->vel.z = 0;
                p->vel.z += 348;
                if (p->vel.z > (256 * 6)) p->vel.z = (256 * 6);
            }
        }
        else
        {
            // normal view
            if (p->vel.z < 0)
            {
                p->vel.z += 256;
                if (p->vel.z > 0)
                    p->vel.z = 0;
            }
            if (p->vel.z > 0)
            {
                p->vel.z -= 256;
                if (p->vel.z < 0)
                    p->vel.z = 0;
            }
        }

        if (p->vel.z > 2048)
            p->vel.z >>= 1;

        p->pos.z += p->vel.z;

        if (p->pos.z > (fz - (15 << 8)))
            p->pos.z += ((fz - (15 << 8)) - p->pos.z) >> 1;

        if (p->pos.z < (cz + (4 << 8)))
        {
            p->pos.z = cz + (4 << 8);
            p->vel.z = 0;
        }

        if (!oldnet_predicting)
        {
            if (p->scuba_on && (krand() & 255) < 8)
            {
                int spr = A_Spawn(playerSprite, WATERBUBBLE);
                sprite[spr].x +=
                    sintable[(fix16_to_int(p->q16ang) + 512 + 64 - (g_globalRandom & 128)) & 2047] >> 6;
                sprite[spr].y +=
                    sintable[(fix16_to_int(p->q16ang) + 64 - (g_globalRandom & 128)) & 2047] >> 6;
                sprite[spr].xrepeat = 3;
                sprite[spr].yrepeat = 2;
                sprite[spr].z = p->pos.z + (8 << 8);
            }
        }
    }
    else if (p_nopredict->jetpack_on)
    {
        p->on_ground = 0;
        p->jumping_counter = 0;
        p->hard_landing = 0;
        p->falling_counter = 0;

        p->pycount += 32;
        p->pycount &= 2047;
        p->pyoff = sintable[p->pycount] >> 7;

        if (p_nopredict->jetpack_on < 11)
        {
            p->jetpack_on++;
            p->pos.z -= (p_nopredict->jetpack_on << 7); //Goin up
        }
        else if (!oldnet_predicting && p->jetpack_on == 11 && !A_CheckSoundPlaying(playerSprite, DUKE_JETPACK_IDLE))
            A_PlaySound(DUKE_JETPACK_IDLE, playerSprite);

        int const zAdjust = shrunk ? 512 : 2048;

        if (TEST_SYNC_KEY(sb_snum, SK_JUMP))         //A (soar high)
        {
            // jump
            Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
            X_OnEvent(EVENT_SOARUP, playerSprite, snum, -1);
            if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
            {
                p->pos.z -= zAdjust;
                p->crack_time = 777;
            }
        }

        if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))   //Z (soar low)
        {
            // crouch
            Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
            X_OnEvent(EVENT_SOARDOWN, playerSprite, snum, -1);
            if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
            {
                p->pos.z += zAdjust;
                p->crack_time = 777;
            }
        }

        int const Zdiff = (shrunk == 0 && (psectlotag == ST_0_NO_EFFECT || psectlotag == ST_2_UNDERWATER)) ? 32 : 16;

        if (psectlotag != ST_2_UNDERWATER && p->scuba_on == 1)
            p->scuba_on = 0;

        if (p->pos.z > (fz - (Zdiff << 8)))
            p->pos.z += ((fz - (Zdiff << 8)) - p->pos.z) >> 1;
        if (p->pos.z < (cz + (18 << 8)))
            p->pos.z = cz + (18 << 8);

    }
    else if (psectlotag != ST_2_UNDERWATER)
    {
        if (!oldnet_predicting)
        {
            if (p->airleft != 15 * 26)
                p->airleft = 15 * 26; //Aprox twenty seconds.

            if (p->scuba_on == 1)
                p->scuba_on = 0;
        }

        if (psectlotag == ST_1_ABOVE_WATER && p->spritebridge == 0)
        {
            if (shrunk == 0)
            {
                floorZOffset = 34;
                p->pycount += 32;
                p->pycount &= 2047;
                p->pyoff = sintable[p->pycount] >> 6;
            }
            else floorZOffset = 12;

            if (!oldnet_predicting && shrunk == 0 && truefdist <= PHEIGHT)
            {
                if (p->on_ground == 1)
                {
                    if (p->dummyplayersprite == -1 && (yax_getbunch(p->cursectnum, YAX_FLOOR) == -1))
                        p->dummyplayersprite =
                        A_Spawn(playerSprite, PLAYERONWATER);
                    sprite[p->dummyplayersprite].pal = sprite[playerSprite].pal;

                    p->footprintcount = 6;
                    if (sector[p->cursectnum].floorpicnum == FLOORSLIME)
                        p->footprintpal = 8;
                    else p->footprintpal = 0;
                    p->footprintshade = 0;
                }
            }
        }
        else if (!oldnet_predicting)
        {
            if (p->footprintcount > 0 && p->on_ground)
                if (p->cursectnum >= 0 && (sector[p->cursectnum].floorstat & 2) != 2)
                {
                    int spr;

                    for (spr = headspritesect[psect]; spr >= 0; spr = nextspritesect[spr])
                        if (sprite[spr].picnum == FOOTPRINTS || sprite[spr].picnum == FOOTPRINTS2 || sprite[spr].picnum == FOOTPRINTS3 || sprite[spr].picnum == FOOTPRINTS4)
                            if (klabs(sprite[spr].x - p->pos.x) < 384)
                                if (klabs(sprite[spr].y - p->pos.y) < 384)
                                    break;
                    if (spr < 0)
                    {
                        if (sector[p->cursectnum].lotag == ST_0_NO_EFFECT &&
                            sector[p->cursectnum].hitag == 0)

                            if (yax_getbunch(p->cursectnum, YAX_FLOOR) < 0 || (sector[p->cursectnum].floorstat & 512))
                            {
                                switch (krand() & 3)
                                {
                                case 0: spr = A_Spawn(playerSprite, FOOTPRINTS); break;
                                case 1: spr = A_Spawn(playerSprite, FOOTPRINTS2); break;
                                case 2: spr = A_Spawn(playerSprite, FOOTPRINTS3); break;
                                default: spr = A_Spawn(playerSprite, FOOTPRINTS4); break;
                                }
                                sprite[spr].pal = p->footprintpal;
                                sprite[spr].shade = p->footprintshade;
                                p->footprintcount--;
                            }
                    }
                }
        }

        if (p->pos.z < (fz - (floorZOffset << 8)))  //falling
        {
            // not jumping or crouching
            if (!TEST_SYNC_KEY(sb_snum, SK_JUMP) && !TEST_SYNC_KEY(sb_snum, SK_CROUCH) && p->on_ground && (sector[psect].floorstat & 2) && p->pos.z >= (fz - (floorZOffset << 8) - (16 << 8)))
                p->pos.z = fz - (floorZOffset << 8);
            else
            {
                p->on_ground = 0;
                p->vel.z += (g_spriteGravity + 80); // (TICSPERFRAME<<6);

                if (p->vel.z >= (4096 + 2048))
                    p->vel.z = (4096 + 2048);

                if (!oldnet_predicting)
                {
                    if (p->vel.z > 2400 && p->falling_counter < 255)
                    {
                        p->falling_counter++;
                        if (p->falling_counter == 38)
                            p->scream_voice = A_PlaySound(DUKE_SCREAM, playerSprite);
                    }

                    if ((p->pos.z + p->vel.z) >= (fz - (floorZOffset << 8)) && p->cursectnum >= 0)   // hit the ground
                    {
                        if (sector[p->cursectnum].lotag != ST_1_ABOVE_WATER)
                        {
                            if (p->falling_counter > 62) P_QuickKill(p);

                            else if (p->falling_counter > 9)
                            {
                                s->extra -= p->falling_counter - (krand() & 3);
                                if (s->extra <= 0)
                                {
                                    A_PlaySound(SQUISHED, playerSprite);
                                    p->pals[0] = 63;
                                    p->pals[1] = 0;
                                    p->pals[2] = 0;
                                    p->pals_time = 63;
                                }
                                else
                                {
                                    A_PlaySound(DUKE_LAND, playerSprite);
                                    A_PlaySound(DUKE_LAND_HURT, playerSprite);
                                }

                                p->pals[0] = 16;
                                p->pals[1] = 0;
                                p->pals[2] = 0;
                                p->pals_time = 32;
                            }
                            else if (p->vel.z > 2048) A_PlaySound(DUKE_LAND, playerSprite);
                        }
                    }
                }
            }
        }
        else
        {
            p->falling_counter = 0;
            if (p->scream_voice > FX_Ok)
            {
                FX_StopSound(p->scream_voice);
                p->scream_voice = FX_Ok;
            }

            if (psectlotag != ST_1_ABOVE_WATER && psectlotag != ST_2_UNDERWATER && p->on_ground == 0 && p->vel.z > (6144 >> 1))
                p->hard_landing = p->vel.z >> 10;

            p->on_ground = 1;

            if (floorZOffset == 40)
            {
                //Smooth on the ground

                floorZOffset = ((fz - (floorZOffset << 8)) - p->pos.z) >> 1;
                if (klabs(floorZOffset) < 256) floorZOffset = 0;
                p->pos.z += floorZOffset;
                p->vel.z -= 768;
                if (p->vel.z < 0) p->vel.z = 0;
            }
            else if (p->jumping_counter == 0)
            {
                p->pos.z += ((fz - (floorZOffset << 7)) - p->pos.z) >> 1; //Smooth on the water
                if (p_nopredict->on_warping_sector == 0 && p->pos.z > fz - (16 << 8))
                {
                    p->pos.z = fz - (16 << 8);
                    p->vel.z >>= 1;
                }
            }

            p->on_warping_sector = 0;

            if (TEST_SYNC_KEY(sb_snum, SK_CROUCH))
            {
                // crouching
                Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
                X_OnEvent(EVENT_CROUCH, playerSprite, snum, -1);
                if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
                {
                    p->pos.z += (2048 + 768);
                    p->crack_time = 777;
                }
            }

            // jumping
            if (!TEST_SYNC_KEY(sb_snum, SK_JUMP) && p->jumping_toggle == 1)
                p->jumping_toggle = 0;
            else if (TEST_SYNC_KEY(sb_snum, SK_JUMP) && p->jumping_toggle == 0)
            {
                if (p->jumping_counter == 0)
                    if ((fz - cz) > (56 << 8))
                    {
                        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
                        X_OnEvent(EVENT_JUMP, playerSprite, snum, -1);
                        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
                        {
                            p->jumping_counter = 1;
                            p->jumping_toggle = 1;
                        }
                    }
            }

            if (p->jumping_counter && !TEST_SYNC_KEY(sb_snum, SK_JUMP))
                p->jumping_toggle = 0;
        }

        if (p->jumping_counter)
        {
            if (!TEST_SYNC_KEY(sb_snum, SK_JUMP) && p->jumping_toggle == 1)
                p->jumping_toggle = 0;

            if (p->jumping_counter < (1024 + 256))
            {
                if (psectlotag == ST_1_ABOVE_WATER && p->jumping_counter > 768)
                {
                    p->jumping_counter = 0;
                    p->vel.z = -512;
                }
                else
                {
                    p->vel.z -= (sintable[(2048 - 128 + p->jumping_counter) & 2047]) / 12;
                    p->jumping_counter += 180;
                    p->on_ground = 0;
                }
            }
            else
            {
                p->jumping_counter = 0;
                p->vel.z = 0;
            }
        }

        p->pos.z += p->vel.z;

        if (p->pos.z < (cz + (4 << 8)))
        {
            p->jumping_counter = 0;
            if (p->vel.z < 0)
                p->vel.x = p->vel.y = 0;
            p->vel.z = 128;
            p->pos.z = cz + (4 << 8);
        }
    }

    // Halt on Kicking/Tripmines/Nukebuttons
    if (p_nopredict->fist_incs ||
        p_nopredict->transporter_hold > 2 ||
        p->hard_landing ||
        p_nopredict->access_incs > 0 ||
        p_nopredict->knee_incs > 0 ||
        (*aplWeaponWorksLike[p_nopredict->curr_weapon] == TRIPBOMB_WEAPON &&
            p_nopredict->kickback_pic > 1 &&
            p_nopredict->kickback_pic < 4))
    {
        doubvel = 0;
        p->vel.x = 0;
        p->vel.y = 0;
    }
    else if (syn->q16avel)            //p->ang += syncangvel * constant
    {
        //ENGINE calculates angvel for you
        fix16_t inputAng = syn->q16avel;

        p->q16angvel = (psectlotag == ST_2_UNDERWATER) ? fix16_mul(inputAng - (inputAng >> 3), fix16_from_int(ksgn(doubvel)))
            : fix16_mul(inputAng, fix16_from_int(ksgn(doubvel)));
        p->q16ang += p->q16angvel;
        p->q16ang &= 0x7FFFFFF;
        p->crack_time = 777;
    }

    if (p->spritebridge == 0)
    {
        P_HandleDamagingFloors(snum);
    }

    if (syn->extbits & (1))
        X_OnEvent(EVENT_MOVEFORWARD, playerSprite, snum, -1);

    if (syn->extbits & (1 << 1))
        X_OnEvent(EVENT_MOVEBACKWARD, playerSprite, snum, -1);

    if (syn->extbits & (1 << 2))
        X_OnEvent(EVENT_STRAFELEFT, playerSprite, snum, -1);

    if (syn->extbits & (1 << 3))
        X_OnEvent(EVENT_STRAFERIGHT, playerSprite, snum, -1);

    if (syn->extbits & (1 << 4) || syn->q16avel < 0)
        X_OnEvent(EVENT_TURNLEFT, playerSprite, snum, -1);

    if (syn->extbits & (1 << 5) || syn->q16avel > 0)
        X_OnEvent(EVENT_TURNRIGHT, playerSprite, snum, -1);

    if (p->vel.x || p->vel.y || syn->fvel || syn->svel)
    {
        if (!oldnet_predicting)
        {
            p->crack_time = 777;

            int const checkWalkSound = sintable[p->bobcounter & 2047] >> 12;

            if ((truefdist < PHEIGHT + (8 << 8)) && (checkWalkSound == 1 || checkWalkSound == 3))
            {
                if (p->spritebridge == 0 && p->walking_snd_toggle == 0 && p->on_ground)
                {
                    int floorPic;
                    switch (psectlotag)
                    {
                    case ST_0_NO_EFFECT:

                        floorPic = (lz >= 0 && (lz & 49152) == 49152)
                            ? TrackerCast(sprite[lz & (MAXSPRITES - 1)].picnum)
                            : TrackerCast(sector[psect].floorpicnum);

                        switch (DynamicTileMap[floorPic])
                        {
                        case PANNEL1__STATIC:
                        case PANNEL2__STATIC:
                            A_PlaySound(DUKE_WALKINDUCTS, playerSprite);
                            p->walking_snd_toggle = 1;
                            break;
                        }
                        break;
                    case ST_1_ABOVE_WATER:
                        if ((krand() & 1) == 0)
                            A_PlaySound(DUKE_ONWATER, playerSprite);
                        p->walking_snd_toggle = 1;
                        break;
                    }
                }
            }
            else if (p->walking_snd_toggle > 0)
                p->walking_snd_toggle--;
        }

        if (p_nopredict->jetpack_on == 0 && p_nopredict->steroids_amount > 0 && p_nopredict->steroids_amount < 400)
            doubvel <<= 1;

        p->vel.x += ((syn->fvel * doubvel) << 6);
        p->vel.y += ((syn->svel * doubvel) << 6);

        if ((aplWeaponWorksLike[p_nopredict->curr_weapon][snum] == KNEE_WEAPON && p_nopredict->kickback_pic > 10 && p->on_ground) || (p->on_ground && TEST_SYNC_KEY(sb_snum, SK_CROUCH)))
        {
            p->vel.x = mulscale(p->vel.x, p_nopredict->runspeed - 0x2000, 16);
            p->vel.y = mulscale(p->vel.y, p_nopredict->runspeed - 0x2000, 16);
        }
        else
        {
            if (psectlotag == ST_2_UNDERWATER)
            {
                p->vel.x = mulscale(p->vel.x, p_nopredict->runspeed - 0x1400, 16);
                p->vel.y = mulscale(p->vel.y, p_nopredict->runspeed - 0x1400, 16);
            }
            else
            {
                p->vel.x = mulscale(p->vel.x, p_nopredict->runspeed, 16);
                p->vel.y = mulscale(p->vel.y, p_nopredict->runspeed, 16);
            }
        }

        if (klabs(p->vel.x) < 2048 && klabs(p->vel.y) < 2048)
            p->vel.x = p->vel.y = 0;

        if (shrunk)
        {
            p->vel.x =
                mulscale16(p->vel.x, p_nopredict->runspeed - (p_nopredict->runspeed >> 1) + (p_nopredict->runspeed >> 2));
            p->vel.y =
                mulscale16(p->vel.y, p_nopredict->runspeed - (p_nopredict->runspeed >> 1) + (p_nopredict->runspeed >> 2));
        }
    }

HORIZONLY:

    //if (p->cursectnum >= 0 && sector[p->cursectnum].lotag == ST_2_UNDERWATER) k = 0;
    //else k = 1;

    if (ud.clipping)
    {
        p->pos.x += p->vel.x >> 14;
        p->pos.y += p->vel.y >> 14;
        updatesector(p->pos.x, p->pos.y, &p->cursectnum);

        if (!oldnet_predicting)
        {
            changespritesect(playerSprite, p->cursectnum);
        }
    }
    else
    {
        int const playerSectNum = p->cursectnum;
        int16_t   ceilingBunch, floorBunch;

        if (playerSectNum >= 0)
            yax_getbunches(playerSectNum, &ceilingBunch, &floorBunch);

        // This updatesectorz conflicts with Duke3D's way of teleporting through water,
        // so make it a bit conditional... OTOH, this way we have an ugly z jump when
        // changing from above water to underwater

        if ((playerSectNum >= 0 && !(sector[playerSectNum].lotag == ST_1_ABOVE_WATER && p->on_ground && floorBunch >= 0))
            && ((floorBunch >= 0 && !(sector[playerSectNum].floorstat & 512))
                || (ceilingBunch >= 0 && !(sector[playerSectNum].ceilingstat & 512))))
        {
            p->cursectnum += MAXSECTORS;  // skip initial z check, restored by updatesectorz
            updatesectorz(p->pos.x, p->pos.y, p->pos.z, &p->cursectnum);
        }

        int moveResult = clipmove_old(&p->pos.x, &p->pos.y,
            &p->pos.z, &p->cursectnum,
            p->vel.x, p->vel.y, 164L, (4L << 8), (psectlotag == ST_1_ABOVE_WATER || p->spritebridge == 1) ? (4L << 8) : (20L << 8), CLIPMASK0);

        if (p_nopredict->jetpack_on == 0 && psectlotag != ST_2_UNDERWATER && psectlotag != ST_1_ABOVE_WATER && shrunk)
            p->pos.z += 32 << 8;

        if (!oldnet_predicting && moveResult)
            P_CheckTouchDamage(p, moveResult);
    }

    if (p_nopredict->jetpack_on == 0)
    {
        if (s->xvel > 16)
        {
            if (psectlotag != ST_1_ABOVE_WATER && psectlotag != ST_2_UNDERWATER && p->on_ground)
            {
                p->pycount += 52;
                p->pycount &= 2047;
                p->pyoff =
                    klabs(s->xvel * sintable[p->pycount]) / 1596;
            }
        }
        else if (psectlotag != ST_2_UNDERWATER && psectlotag != ST_1_ABOVE_WATER)
            p->pyoff = 0;
    }

    if (!oldnet_predicting)
    {
        // RBG***
        setspritez_old(playerSprite, p->pos.x, p->pos.y, p->pos.z + PHEIGHT);

        if (psectlotag < 3)
        {
            psect = s->sectnum;
            if (ud.clipping == 0 && sector[psect].lotag == ST_31_TWO_WAY_TRAIN)
            {
                if (sprite[sector[psect].hitag].xvel && ActorExtra[sector[psect].hitag].temp_data[0] == 0)
                {
                    P_QuickKill(p);
                    return;
                }
            }
        }

        if (p->cursectnum >= 0 && truefdist < PHEIGHT && p->on_ground && psectlotag != 1 && shrunk == 0 && sector[p->cursectnum].lotag == ST_1_ABOVE_WATER)
            if (!A_CheckSoundPlaying(playerSprite, DUKE_ONWATER))
                A_PlaySound(DUKE_ONWATER, playerSprite);

        if (p->cursectnum != s->sectnum)
            changespritesect(playerSprite, p->cursectnum);
    }

    int mashedPotato = 0;
    if (ud.clipping == 0)
        mashedPotato = ((pushmove_old(&p->pos.x, &p->pos.y, &p->pos.z, &p->cursectnum, 164L, (4L << 8), (4L << 8), CLIPMASK0) < 0) && (A_GetFurthestAngle(p->i, 8) < 512));

    if (!oldnet_predicting && ud.clipping == 0)
    {
        if (klabs(ActorExtra[playerSprite].floorz - ActorExtra[playerSprite].ceilingz) < (48 << 8) || mashedPotato)
        {
            if (!(sector[s->sectnum].lotag & 0x8000) && (isanunderoperator(sector[s->sectnum].lotag) ||
                isanearoperator(sector[s->sectnum].lotag)))
                activatebysector(s->sectnum, playerSprite);
            if (mashedPotato)
            {
                P_QuickKill(p);
                return;
            }
        }
        else if (klabs(fz - cz) < (32 << 8) && isanunderoperator(sector[psect].lotag))
            activatebysector(psect, playerSprite);
    }

    // center_view
    int center_view = 0;
    if (TEST_SYNC_KEY(sb_snum, SK_CENTER_VIEW) || p->hard_landing)
    {
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_RETURNTOCENTER, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            p->return_to_center = 9;
        }
    }

    if (TEST_SYNC_KEY(sb_snum, SK_LOOK_UP))
    {
        // look_up
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_LOOKUP, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            p->return_to_center = 9;
            if (TEST_SYNC_KEY(sb_snum, SK_RUN)) p->q16horiz += fix16_from_int(12);     // running
            p->q16horiz += fix16_from_int(12);
            center_view++;
        }
    }

    else if (TEST_SYNC_KEY(sb_snum, SK_LOOK_DOWN))
    {
        // look_down
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_LOOKDOWN, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            p->return_to_center = 9;
            if (TEST_SYNC_KEY(sb_snum, SK_RUN)) p->q16horiz -= fix16_from_int(12);
            p->q16horiz -= fix16_from_int(12);
            center_view++;
        }
    }

    else if (TEST_SYNC_KEY(sb_snum, SK_AIM_UP))
    {
        // aim_up
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_AIMUP, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            // running
            if (TEST_SYNC_KEY(sb_snum, SK_RUN)) p->q16horiz += fix16_from_int(6);
            p->q16horiz += fix16_from_int(6);
            center_view++;
        }
    }

    else if (TEST_SYNC_KEY(sb_snum, SK_AIM_DOWN))
    {
        // aim_down
        Gv_SetVar(g_iReturnVarID, 0, playerSprite, snum);
        X_OnEvent(EVENT_AIMDOWN, playerSprite, snum, -1);
        if (Gv_GetVar(g_iReturnVarID, playerSprite, snum) == 0)
        {
            // running
            if (TEST_SYNC_KEY(sb_snum, SK_RUN)) p->q16horiz -= fix16_from_int(6);
            p->q16horiz -= fix16_from_int(6);
            center_view++;
        }
    }
    if (p->return_to_center > 0)
        if (TEST_SYNC_KEY(sb_snum, SK_LOOK_UP) == 0 && TEST_SYNC_KEY(sb_snum, SK_LOOK_DOWN) == 0)
        {
            p->return_to_center--;
            p->q16horiz += F16(33) - fix16_div(p->q16horiz, F16(3));
            center_view++;
        }

    if (p->hard_landing > 0)
    {
        p->hard_landing--;
        p->q16horiz -= fix16_from_int(p->hard_landing << 4);
    }

    if (center_view)
    {
        if (p->q16horiz > F16(95) && p->q16horiz < F16(105)) p->q16horiz = F16(100);
        if (p->q16horizoff > F16(-5) && p->q16horizoff < F16(5)) p->q16horizoff = 0;
    }

    p->q16horiz += syn->q16horz;
    p->q16horiz = fix16_clamp(p->q16horiz, F16(HORIZ_MIN), F16(HORIZ_MAX));

    //Shooting code/changes

    if (!oldnet_predicting && p->show_empty_weapon > 0)
    {
        p->show_empty_weapon--;
        if (p->show_empty_weapon == 0 && (p->weaponswitch & 2) && p->ammo_amount[p->curr_weapon] <= 0)
        {
            if (p->last_full_weapon == GROW_WEAPON)
                p->subweapon |= (1 << GROW_WEAPON);
            else if (p->last_full_weapon == SHRINKER_WEAPON)
                p->subweapon &= ~(1 << GROW_WEAPON);
            P_AddWeapon(p, p->last_full_weapon, 1);
            return;
        }
    }

    if (p_nopredict->knee_incs > 0)
    {
        p->knee_incs++;
        p->q16horiz -= F16(48);
        p->return_to_center = 9;
        if (!oldnet_predicting && p->knee_incs > 15)
        {
            p->knee_incs = 0;
            p->holster_weapon = 0;
            if (p->weapon_pos < 0)
                p->weapon_pos = -p->weapon_pos;
            if (p->actorsqu >= 0 && dist(&sprite[playerSprite], &sprite[p->actorsqu]) < 1400 && sprite[p->actorsqu].statnum != MAXSTATUS)
            {
                A_DoGuts(p->actorsqu, JIBS6, 7);
                A_Spawn(p->actorsqu, BLOODPOOL);
                A_PlaySound(SQUISHED, p->actorsqu);
                switch (DynamicTileMap[sprite[p->actorsqu].picnum])
                {
                case FEM1__STATIC:
                case FEM2__STATIC:
                case FEM3__STATIC:
                case FEM4__STATIC:
                case FEM5__STATIC:
                case FEM6__STATIC:
                case FEM7__STATIC:
                case FEM8__STATIC:
                case FEM9__STATIC:
                case FEM10__STATIC:
                case PODFEM1__STATIC:
                case NAKED1__STATIC:
                case STATUE__STATIC:
                    if (sprite[p->actorsqu].yvel)
                        G_OperateRespawns(sprite[p->actorsqu].yvel);
                    break;
                }

                if (sprite[p->actorsqu].picnum == APLAYER)
                {
                    P_QuickKill(g_player[sprite[p->actorsqu].yvel].ps);
                    g_player[sprite[p->actorsqu].yvel].ps->frag_ps = snum;
                }
                else if (A_CheckEnemySprite(&sprite[p->actorsqu]))
                {
                    deletesprite(p->actorsqu);
                    p->actors_killed++;
                }
                else deletesprite(p->actorsqu);
            }
            p->actorsqu = -1;
        }
        else if (p->actorsqu >= 0)
        {
            int angleDelta = G_GetAngleDelta(fix16_to_int(p->q16ang), getangle(sprite[p->actorsqu].x - p->pos.x, sprite[p->actorsqu].y - p->pos.y)) >> 2;
            p->q16ang += fix16_from_int(angleDelta);
        }
    }

    if (P_DoCounters(p)) return;

    P_ProcessWeapon(snum);
}

//UPDATE THIS FILE OVER THE OLD GETSPRITESCORE/COMPUTERGETINPUT FUNCTIONS
int getspritescore(int snum, int dapicnum)
{
    switch (DynamicTileMap[dapicnum])
    {
    case FIRSTGUNSPRITE__STATIC:
        return(20);
    case CHAINGUNSPRITE__STATIC:
        return(50);
    case RPGSPRITE__STATIC:
        return(200);
    case FREEZESPRITE__STATIC:
        return(25);
    case SHRINKERSPRITE__STATIC:
        return(80);
    case HEAVYHBOMB__STATIC:
        return(60);
    case TRIPBOMBSPRITE__STATIC:
        return(50);
    case SHOTGUNSPRITE__STATIC:
        return(120);
    case DEVISTATORSPRITE__STATIC:
        return(120);

    case FREEZEAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[FREEZE_WEAPON] < g_player[snum].ps->max_ammo_amount[FREEZE_WEAPON]) return(10);
        return(1);
    case AMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[PISTOL_WEAPON] < g_player[snum].ps->max_ammo_amount[PISTOL_WEAPON]) return(10);
        return(1);
    case BATTERYAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[CHAINGUN_WEAPON] < g_player[snum].ps->max_ammo_amount[CHAINGUN_WEAPON]) return(20);
        return(1);
    case DEVISTATORAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[DEVISTATOR_WEAPON] < g_player[snum].ps->max_ammo_amount[DEVISTATOR_WEAPON]) return(25);
        return(1);
    case RPGAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[RPG_WEAPON] < g_player[snum].ps->max_ammo_amount[RPG_WEAPON]) return(50);
        return(1);
    case CRYSTALAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[SHRINKER_WEAPON] < g_player[snum].ps->max_ammo_amount[SHRINKER_WEAPON]) return(10);
        return(1);
    case HBOMBAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[HANDBOMB_WEAPON] < g_player[snum].ps->max_ammo_amount[HANDBOMB_WEAPON]) return(30);
        return(1);
    case SHOTGUNAMMO__STATIC:
        if (g_player[snum].ps->ammo_amount[SHOTGUN_WEAPON] < g_player[snum].ps->max_ammo_amount[SHOTGUN_WEAPON]) return(25);
        return(1);

    case COLA__STATIC:
        if (sprite[g_player[snum].ps->i].extra < g_player[snum].ps->max_player_health) return(10);
        return(1);
    case SIXPAK__STATIC:
        if (sprite[g_player[snum].ps->i].extra < g_player[snum].ps->max_player_health) return(30);
        return(1);
    case FIRSTAID__STATIC:
        if (g_player[snum].ps->firstaid_amount < g_player[snum].ps->max_player_health) return(100);
        return(1);
    case SHIELD__STATIC:
        if (g_player[snum].ps->shield_amount < g_player[snum].ps->max_shield_amount) return(50);
        return(1);
    case STEROIDS__STATIC:
        if (g_player[snum].ps->steroids_amount < 400) return(30);
        return(1);
    case AIRTANK__STATIC:
        if (g_player[snum].ps->scuba_amount < 6400) return(30);
        return(1);
    case JETPACK__STATIC:
        if (g_player[snum].ps->jetpack_amount < 1600) return(100);
        return(1);
    case HEATSENSOR__STATIC:
        if (g_player[snum].ps->heat_amount < 1200) return(5);
        return(1);
    case ACCESSCARD__STATIC:
        return(1);
    case BOOTS__STATIC:
        if (g_player[snum].ps->boot_amount < 200) return(15);
        return(1);
    case ATOMICHEALTH__STATIC:
        if (sprite[g_player[snum].ps->i].extra < g_player[snum].ps->max_player_health<<1) return(50);
        return(1);
    case HOLODUKE__STATIC:
        if (g_player[snum].ps->holoduke_amount < 2400) return(5);
        return(1);
    case MUSICANDSFX__STATIC:
        return(1);
    }
    return(0);
}

static int fdmatrix[12][12] =
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

static int goalx[MAXPLAYERS], goaly[MAXPLAYERS], goalz[MAXPLAYERS];
static int goalsect[MAXPLAYERS], goalwall[MAXPLAYERS], goalsprite[MAXPLAYERS], goalspritescore[MAXPLAYERS];
static int goalplayer[MAXPLAYERS], clipmovecount[MAXPLAYERS];
short searchsect[MAXSECTORS], searchparent[MAXSECTORS];
char dashow2dsector[(MAXSECTORS+7)>>3];
void computergetinput(int snum, input_t *syn)
{
    int i, j, k, l, dx, dy;
    int dist, daang, zang, fightdist, damyang, damysect;
    int startsect, endsect, splc, send, startwall, endwall;

    DukePlayer_t *p = g_player[snum].ps;
    walltype *wal;

    vec3_t pos1, pos2, pos3;
    hitdata_t hit;

    syn->fvel = 0;
    syn->svel = 0;
    syn->q16avel = 0;
    syn->q16horz = 0;
    syn->bits = 0;
    syn->extbits = 0;

    pos1.x = sprite[p->i].x;
    pos1.y = sprite[p->i].y;
    pos1.z = sprite[p->i].z;
    damyang = sprite[p->i].ang;
    damysect = sprite[p->i].sectnum;
    if ((numplayers >= 2) && (snum == myconnectindex))
    {
        pos1.x = predictedPlayer.pos.x;
        pos1.y = predictedPlayer.pos.y;
        pos1.z = predictedPlayer.pos.z+PHEIGHT;
        damyang = fix16_to_int(predictedPlayer.q16ang);
        damysect = predictedPlayer.cursectnum;
    }

    if (!(numframes&7))
    {
        pos2.x = sprite[g_player[goalplayer[snum]].ps->i].x;
        pos2.y = sprite[g_player[goalplayer[snum]].ps->i].y;
        pos2.z = sprite[g_player[goalplayer[snum]].ps->i].z;

        if (!cansee(pos1.x,pos1.y,pos1.z-(48<<8),damysect, pos2.x, pos2.y, pos2.z-(48<<8),sprite[g_player[goalplayer[snum]].ps->i].sectnum))
            goalplayer[snum] = snum;
    }

    if ((goalplayer[snum] == snum) || (g_player[goalplayer[snum]].ps->dead_flag != 0))
    {
        j = 0x7fffffff;
        TRAVERSE_CONNECT(i)
        if (i != snum && !(GTFLAGS(GAMETYPE_TDM) && g_player[snum].ps->team == g_player[i].ps->team))
        {
            dist = ksqrt((sprite[g_player[i].ps->i].x-pos1.x)*(sprite[g_player[i].ps->i].x-pos1.x)+(sprite[g_player[i].ps->i].y-pos1.y)*(sprite[g_player[i].ps->i].y-pos1.y));

            pos2.x = sprite[g_player[i].ps->i].x;
            pos2.y = sprite[g_player[i].ps->i].y;
            pos2.z = sprite[g_player[i].ps->i].z;
            if (!cansee(pos1.x,pos1.y,pos1.z-(48<<8),damysect, pos2.x, pos2.y, pos2.z-(48<<8),sprite[g_player[i].ps->i].sectnum))
                dist <<= 1;

            if (dist < j)
            {
                j = dist;
                goalplayer[snum] = i;
            }

        }
    }

    pos2.x = sprite[g_player[goalplayer[snum]].ps->i].x;
    pos2.y = sprite[g_player[goalplayer[snum]].ps->i].y;
    pos2.z = sprite[g_player[goalplayer[snum]].ps->i].z;

    if (p->dead_flag) syn->bits |= BIT(SK_OPEN);
    if ((p->firstaid_amount > 0) && (p->last_extra < 100))
        syn->bits |= BIT(SK_MEDKIT);

    for (j=headspritestat[STAT_PROJECTILE];j>=0;j=nextspritestat[j])
    {
        switch (DynamicTileMap[sprite[j].picnum])
        {
        case TONGUE__STATIC:
            k = 4;
            break;
        case FREEZEBLAST__STATIC:
            k = 4;
            break;
        case SHRINKSPARK__STATIC:
            k = 16;
            break;
        case RPG__STATIC:
            k = 16;
            break;
        default:
            k = 0;
            break;
        }
        if (k)
        {
            pos3.x = sprite[j].x;
            pos3.y = sprite[j].y;
            pos3.z = sprite[j].z;
            for (l=0;l<=8;l++)
            {
                if (tmulscale11(pos3.x-pos1.x, pos3.x-pos1.x, pos3.y-pos1.y, pos3.y-pos1.y,(pos3.z-pos1.z)>>4,(pos3.z-pos1.z)>>4) < 3072)
                {
                    dx = sintable[(sprite[j].ang+512)&2047];
                    dy = sintable[sprite[j].ang&2047];
                    if ((pos1.x - pos3.x)*dy > (pos1.y - pos3.y)*dx) i = -k*512;
                    else i = k*512;
                    syn->fvel -= mulscale17(dy,i);
                    syn->svel += mulscale17(dx,i);
                }
                if (l < 7)
                {
                    pos3.x += (mulscale14(sprite[j].xvel,sintable[(sprite[j].ang+512)&2047])<<2);
                    pos3.y += (mulscale14(sprite[j].xvel,sintable[sprite[j].ang&2047])<<2);
                    pos3.z += (sprite[j].zvel<<2);
                }
                else
                {
                    hitscan(&sprite[j].pos,sprite[j].sectnum,
                            mulscale14(sprite[j].xvel,sintable[(sprite[j].ang+512)&2047]),
                            mulscale14(sprite[j].xvel,sintable[sprite[j].ang&2047]),
                            (int)sprite[j].zvel,
                            &hit,CLIPMASK1);
                }
            }
        }
    }

    if ((g_player[goalplayer[snum]].ps->dead_flag == 0) &&
            ((cansee(pos1.x, pos1.y, pos1.z,damysect,pos2.x, pos2.y, pos2.z,sprite[g_player[goalplayer[snum]].ps->i].sectnum)) ||
             (cansee(pos1.x, pos1.y, pos1.z-(24<<8),damysect, pos2.x, pos2.y, pos2.z-(24<<8),sprite[g_player[goalplayer[snum]].ps->i].sectnum)) ||
             (cansee(pos1.x, pos1.y, pos1.z-(48<<8),damysect, pos2.x, pos2.y, pos2.z-(48<<8),sprite[g_player[goalplayer[snum]].ps->i].sectnum))))
    {
        syn->bits |= BIT(SK_FIRE);

        if ((p->curr_weapon == HANDBOMB_WEAPON) && (!(rand()&7)))
            syn->bits &= ~BIT(SK_FIRE);

        if (p->curr_weapon == TRIPBOMB_WEAPON)
            syn->bits |= ((rand()%MAX_WEAPONS)<<SK_WEAPON_BITS);

        if (p->curr_weapon == RPG_WEAPON)
        {
            pos1.z -= PHEIGHT;
            hitscan(&pos1,damysect,sintable[(damyang+512)&2047],sintable[damyang&2047],
                    fix16_to_int(F16(100)-p->q16horiz-p->q16horizoff)*32,&hit,CLIPMASK1);
            pos1.z += PHEIGHT;

            if ((pos3.x - pos1.x)*(pos3.x - pos1.x)+(pos3.y - pos1.y)*(pos3.y - pos1.y) < 2560*2560) syn->bits &= ~BIT(SK_FIRE);
        }


        fightdist = fdmatrix[p->curr_weapon][g_player[goalplayer[snum]].ps->curr_weapon];
        if (fightdist < 128) fightdist = 128;
        dist = ksqrt((pos2.x - pos1.x)*(pos2.x - pos1.x)+(pos2.y - pos1.y)*(pos2.y - pos1.y));
        if (dist == 0) dist = 1;
        daang = getangle(pos2.x+(g_player[goalplayer[snum]].ps->vel.x>>14)- pos1.x, pos2.y+(g_player[goalplayer[snum]].ps->vel.y>>14)-pos1.y);
        zang = 100-((pos2.z-pos1.z)*8)/dist;
        fightdist = max(fightdist,(klabs(pos2.z-pos1.z)>>4));

        if (sprite[g_player[goalplayer[snum]].ps->i].yrepeat < 32)
        {
            fightdist = 0;
            syn->bits &= ~BIT(SK_FIRE);
        }
        if (sprite[g_player[goalplayer[snum]].ps->i].pal == 1)
        {
            fightdist = 0;
            syn->bits &= ~BIT(SK_FIRE);
        }

        if (dist < 256) syn->bits |= BIT(SK_QUICK_KICK);

        pos3.x = pos2.x+((pos1.x-pos2.x)*fightdist/dist);
        pos3.y = pos2.y+((pos1.y- pos2.y)*fightdist/dist);
        syn->fvel += (pos3.x- pos1.x)*2047/dist;
        syn->svel += (pos3.y- pos1.y)*2047/dist;

        //Strafe attack
        if (fightdist)
        {
            j = (int32_t)totalclock+snum*13468;
            i = sintable[(j<<6)&2047];
            i += sintable[((j+4245)<<5)&2047];
            i += sintable[((j+6745)<<4)&2047];
            i += sintable[((j+15685)<<3)&2047];
            dx = sintable[(sprite[g_player[goalplayer[snum]].ps->i].ang+512)&2047];
            dy = sintable[sprite[g_player[goalplayer[snum]].ps->i].ang&2047];
            if ((pos1.x - pos2.x)*dy > (pos1.y - pos2.y)*dx) i += 8192;
            else i -= 8192;
            syn->fvel += ((sintable[(daang+1024)&2047]*i)>>17);
            syn->svel += ((sintable[(daang+512)&2047]*i)>>17);
        }

        syn->q16avel = min(max(fix16_from_int((((daang+1024-damyang)&2047)-1024)>>1),F16(-127)),F16(127));
        syn->q16horz = min(max(fix16_from_int(zang)-p->q16horiz,F16(-MAXHORIZ)),F16(MAXHORIZ));
        syn->bits |= BIT(SK_AIMMODE);
        return;
    }

    goalsect[snum] = -1;
    if (goalsect[snum] < 0)
    {
        goalwall[snum] = -1;
        startsect = sprite[p->i].sectnum;
        endsect = sprite[g_player[goalplayer[snum]].ps->i].sectnum;

        clearbufbyte(dashow2dsector,(MAXSECTORS+7)>>3,0L);
        searchsect[0] = startsect;
        searchparent[0] = -1;
        dashow2dsector[startsect>>3] |= (1<<(startsect&7));
        for (splc=0,send=1;splc<send;splc++)
        {
            startwall = sector[searchsect[splc]].wallptr;
            endwall = startwall + sector[searchsect[splc]].wallnum;
            for (i=startwall,wal=&wall[startwall];i<endwall;i++,wal++)
            {
                j = wal->nextsector;
                if (j < 0) continue;

                dx = ((wall[wal->point2].x+wal->x)>>1);
                dy = ((wall[wal->point2].y+wal->y)>>1);
                if ((getceilzofslope(j,dx,dy) > getflorzofslope(j,dx,dy)-(28<<8)) && ((sector[j].lotag < 15) || (sector[j].lotag > 22)))
                    continue;
                if (getflorzofslope(j,dx,dy) < getflorzofslope(searchsect[splc],dx,dy)-(72<<8))
                    continue;
                if ((dashow2dsector[j>>3]&(1<<(j&7))) == 0)
                {
                    dashow2dsector[j>>3] |= (1<<(j&7));
                    searchsect[send] = (short)j;
                    searchparent[send] = (short)splc;
                    send++;
                    if (j == endsect)
                    {
                        clearbufbyte(dashow2dsector,(MAXSECTORS+7)>>3,0L);
                        for (k=send-1;k>=0;k=searchparent[k])
                            dashow2dsector[searchsect[k]>>3] |= (1<<(searchsect[k]&7));

                        for (k=send-1;k>=0;k=searchparent[k])
                            if (!searchparent[k]) break;

                        goalsect[snum] = searchsect[k];
                        startwall = sector[goalsect[snum]].wallptr;
                        endwall = startwall+sector[goalsect[snum]].wallnum;
                        pos3.x = pos3.y = 0;
                        for (i=startwall;i<endwall;i++)
                        {
                            pos3.x += wall[i].x;
                            pos3.y += wall[i].y;
                        }
                        pos3.x /= (endwall-startwall);
                        pos3.y /= (endwall-startwall);

                        startwall = sector[startsect].wallptr;
                        endwall = startwall+sector[startsect].wallnum;
                        l = 0;
                        k = startwall;
                        for (i=startwall;i<endwall;i++)
                        {
                            if (wall[i].nextsector != goalsect[snum]) continue;
                            dx = wall[wall[i].point2].x-wall[i].x;
                            dy = wall[wall[i].point2].y-wall[i].y;

                            //if (dx*(y1-wall[i].y) <= dy*(x1-wall[i].x))
                            //   if (dx*(y2-wall[i].y) >= dy*(x2-wall[i].x))
                            if ((pos3.x-pos1.x)*(wall[i].y- pos1.y) <= (pos3.y - pos1.y)*(wall[i].x- pos1.x))
                                if ((pos3.x - pos1.x)*(wall[wall[i].point2].y - pos1.y) >= (pos3.y - pos1.y)*(wall[wall[i].point2].x - pos1.x))
                                {
                                    k = i;
                                    break;
                                }

                            dist = ksqrt(dx*dx+dy*dy);
                            if (dist > l)
                            {
                                l = dist;
                                k = i;
                            }

                        }
                        goalwall[snum] = k;
                        daang = ((getangle(wall[wall[k].point2].x-wall[k].x,wall[wall[k].point2].y-wall[k].y)+1536)&2047);
                        goalx[snum] = ((wall[k].x+wall[wall[k].point2].x)>>1)+(sintable[(daang+512)&2047]>>8);
                        goaly[snum] = ((wall[k].y+wall[wall[k].point2].y)>>1)+(sintable[daang&2047]>>8);
                        goalz[snum] = sector[goalsect[snum]].floorz-(32<<8);
                        break;
                    }
                }
            }

            for (i=headspritesect[searchsect[splc]];i>=0;i=nextspritesect[i])
                if (sprite[i].lotag == 7)
                {
                    j = sprite[sprite[i].owner].sectnum;
                    if ((dashow2dsector[j>>3]&(1<<(j&7))) == 0)
                    {
                        dashow2dsector[j>>3] |= (1<<(j&7));
                        searchsect[send] = (short)j;
                        searchparent[send] = (short)splc;
                        send++;
                        if (j == endsect)
                        {
                            clearbufbyte(dashow2dsector,(MAXSECTORS+7)>>3,0L);
                            for (k=send-1;k>=0;k=searchparent[k])
                                dashow2dsector[searchsect[k]>>3] |= (1<<(searchsect[k]&7));

                            for (k=send-1;k>=0;k=searchparent[k])
                                if (!searchparent[k]) break;

                            goalsect[snum] = searchsect[k];
                            startwall = sector[startsect].wallptr;
                            endwall = startwall+sector[startsect].wallnum;
                            l = 0;
                            k = startwall;
                            for (i=startwall;i<endwall;i++)
                            {
                                dx = wall[wall[i].point2].x-wall[i].x;
                                dy = wall[wall[i].point2].y-wall[i].y;
                                dist = ksqrt(dx*dx+dy*dy);
                                if ((wall[i].nextsector == goalsect[snum]) && (dist > l))
                                {
                                    l = dist;
                                    k = i;
                                }

                            }
                            goalwall[snum] = k;
                            daang = ((getangle(wall[wall[k].point2].x-wall[k].x,wall[wall[k].point2].y-wall[k].y)+1536)&2047);
                            goalx[snum] = ((wall[k].x+wall[wall[k].point2].x)>>1)+(sintable[(daang+512)&2047]>>8);
                            goaly[snum] = ((wall[k].y+wall[wall[k].point2].y)>>1)+(sintable[daang&2047]>>8);
                            goalz[snum] = sector[goalsect[snum]].floorz-(32<<8);
                            break;
                        }
                    }
                }
            if (goalwall[snum] >= 0) break;
        }
    }

    if ((goalsect[snum] < 0) || (goalwall[snum] < 0))
    {
        if (goalsprite[snum] < 0 || !cansee(pos1.x, pos1.y, pos1.z-(32<<8),damysect,sprite[goalsprite[snum]].x,sprite[goalsprite[snum]].y,sprite[goalsprite[snum]].z-(4<<8),i))
        {
            int bestsprite = -1, spritescore = 0;

            for (k=0;k<16;k++)
            {
                i = (rand()%numsectors);
                for (j=headspritesect[i];j>=0;j=nextspritesect[j])
                {
                    if ((sprite[j].xrepeat <= 0) || (sprite[j].yrepeat <= 0)) continue;
                    if (getspritescore(snum,sprite[j].picnum) <= 0) continue;
                    if (cansee(pos1.x, pos1.y, pos1.z-(32<<8),damysect,sprite[j].x,sprite[j].y,sprite[j].z-(4<<8),i))
                    {
                        if (getspritescore(snum,sprite[j].picnum) > spritescore)
                        {
                            spritescore = getspritescore(snum,sprite[j].picnum);
                            bestsprite = j;
                        }
//                        break;
                    }
                }
            }
            if (bestsprite != -1 && (goalsprite[snum] < 0 || spritescore > goalspritescore[snum]))
            {
                goalx[snum] = sprite[bestsprite].x;
                goaly[snum] = sprite[bestsprite].y;
                goalz[snum] = sprite[bestsprite].z;
                goalsprite[snum] = bestsprite;
                goalspritescore[snum] = spritescore;
            }
        }
        pos2.x = goalx[snum];
        pos2.y = goaly[snum];
        dist = ksqrt((pos2.x - pos1.x)*(pos2.x - pos1.x)+(pos2.y - pos1.y)*(pos2.y - pos1.y));
        if (!dist) return;
        daang = getangle(pos2.x - pos1.x, pos2.y - pos1.y);
        syn->fvel += (pos2.x - pos1.x)*2047/dist;
        syn->svel += (pos2.y - pos1.y)*2047/dist;
        syn->q16avel = min(max(fix16_from_int((((daang+1024-damyang)&2047)-1024)>>3), F16(-127)),F16(127));
    }
    else if (goalsprite[snum] != -1)
    {
        if (!cansee(pos1.x, pos1.y, pos1.z-(32<<8),damysect,sprite[goalsprite[snum]].x,sprite[goalsprite[snum]].y,sprite[goalsprite[snum]].z-(4<<8),i))
        {
            goalspritescore[snum] = 0;
            goalsprite[snum] = -1;
        }
    }

    pos3.x = p->pos.x;
    pos3.y = p->pos.y;
    pos3.z = p->pos.z;
    int16_t dasect = p->cursectnum;
    i = clipmove_old(&pos3.x,&pos3.y,&pos3.z,&dasect,p->vel.x,p->vel.y,164L,4L<<8,4L<<8,CLIPMASK0);
    if (!i)
    {
        pos3.x = p->pos.x;
        pos3.y = p->pos.y;
        pos3.z = p->pos.z+(24<<8);
        dasect = p->cursectnum;
        i = clipmove_old(&pos3.x,&pos3.y,&pos3.z,&dasect,p->vel.x,p->vel.y,164L,4L<<8,4L<<8,CLIPMASK0);
    }
    if (i)
    {
        clipmovecount[snum]++;

        j = 0;
        if ((i&0xc000) == 32768)  //Hit a wall (49152 for sprite)
            if (wall[i&(MAXWALLS-1)].nextsector >= 0)
            {
                if (getflorzofslope(wall[i&(MAXWALLS-1)].nextsector,p->pos.x,p->pos.y) <= p->pos.z+(24<<8)) j |= 1;
                if (getceilzofslope(wall[i&(MAXWALLS-1)].nextsector,p->pos.x,p->pos.y) >= p->pos.z-(24<<8)) j |= 2;
            }
        if ((i&0xc000) == 49152) j = 1;
        if (j&1) if (clipmovecount[snum] == 4) syn->bits |= BIT(SK_JUMP);
        if (j&2) syn->bits |= BIT(SK_CROUCH);

        //Strafe attack
        daang = getangle(pos2.x - pos1.x, pos2.y - pos1.y);
        if ((i&0xc000) == 32768)
            daang = getangle(wall[wall[i&(MAXWALLS-1)].point2].x-wall[i&(MAXWALLS-1)].x,wall[wall[i&(MAXWALLS-1)].point2].y-wall[i&(MAXWALLS-1)].y);
        j = (int32_t)totalclock+snum*13468;
        i = sintable[(j<<6)&2047];
        i += sintable[((j+4245)<<5)&2047];
        i += sintable[((j+6745)<<4)&2047];
        i += sintable[((j+15685)<<3)&2047];
        syn->fvel += ((sintable[(daang+1024)&2047]*i)>>17);
        syn->svel += ((sintable[(daang+512)&2047]*i)>>17);

        if ((clipmovecount[snum]&31) == 2) syn->bits |= BIT(SK_OPEN);
        if ((clipmovecount[snum]&31) == 17) syn->bits |= BIT(SK_QUICK_KICK);
        if (clipmovecount[snum] > 32)
        {
            goalsect[snum] = -1;
            goalwall[snum] = -1;
            clipmovecount[snum] = 0;
        }

        if (goalsprite[snum] != -1)
        {
            if (!cansee(pos1.x, pos1.y, pos1.z-(32<<8),damysect,sprite[goalsprite[snum]].x,sprite[goalsprite[snum]].y,sprite[goalsprite[snum]].z-(4<<8),i))
            {
                goalsprite[snum] = -1;
                goalspritescore[snum] = 0;
            }
        }
    }
    else
        clipmovecount[snum] = 0;

    if ((goalsect[snum] >= 0) && (goalwall[snum] >= 0))
    {
        pos2.x = goalx[snum];
        pos2.y = goaly[snum];
        dist = ksqrt((pos2.x - pos1.x)*(pos2.x - pos1.x)+(pos2.y - pos1.y)*(pos2.y - pos1.y));
        if (!dist) return;
        daang = getangle(pos2.x - pos1.x, pos2.y - pos1.y);
        if ((goalwall[snum] >= 0) && (dist < 4096))
            daang = ((getangle(wall[wall[goalwall[snum]].point2].x-wall[goalwall[snum]].x,wall[wall[goalwall[snum]].point2].y-wall[goalwall[snum]].y)+1536)&2047);
        syn->fvel += (pos2.x - pos1.x)*2047/dist;
        syn->svel += (pos2.y - pos1.y)*2047/dist;
        syn->q16avel = min(max(fix16_from_int((((daang+1024-damyang)&2047)-1024)>>3),-127),127);
    }
}

