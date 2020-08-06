//-------------------------------------------------------------------------
/*
Copyright (C) 2016 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

//#include "anim.h"
#include "colmatch.h"
#include "compat.h"
//#include "demo.h"
#include "duke3d.h"
//#include "input.h"
#include "mdsprite.h"
//#include "sbar.h"
#include "screens.h"

palette_t CrosshairColors = { 255, 255, 0, 0 };
palette_t DefaultCrosshairColors = { 0, 0, 0, 0 };
int g_crosshairSum = 0;

void P_SetGamePalette(DukePlayer_t* player, uint32_t palid, int32_t set)
{
    if (palid >= MAXBASEPALS)
        palid = 0;

    player->palette = palid;

    if (player != g_player[screenpeek].ps)
        return;

    videoSetPalette(ud.brightness >> 2, palid, set);
}

void G_GetCrosshairColor(void)
{
    if (DefaultCrosshairColors.f)
        return;

    tileLoad(CROSSHAIR);

    if (!waloff[CROSSHAIR])
        return;

    char const* ptr = (char const*)waloff[CROSSHAIR];

    // find the brightest color in the original 8-bit tile
    int32_t ii = tilesiz[CROSSHAIR].x * tilesiz[CROSSHAIR].y;
    int32_t bri = 0, j = 0, i;

    Bassert(ii > 0);

    do
    {
        if (*ptr != 255)
        {
            i = curpalette[(int32_t)*ptr].r + curpalette[(int32_t)*ptr].g + curpalette[(int32_t)*ptr].b;
            if (i > j) { j = i; bri = *ptr; }
        }
        ptr++;
    } while (--ii);

    Bmemcpy(&CrosshairColors, &curpalette[bri], sizeof(palette_t));
    Bmemcpy(&DefaultCrosshairColors, &curpalette[bri], sizeof(palette_t));
    DefaultCrosshairColors.f = 1; // this flag signifies that the color has been detected
}

void G_SetCrosshairColor(int32_t r, int32_t g, int32_t b)
{
    if (g_crosshairSum == r + (g << 8) + (b << 16))
        return;

    tileLoad(CROSSHAIR);

    if (!waloff[CROSSHAIR])
        return;

    if (!DefaultCrosshairColors.f)
        G_GetCrosshairColor();

    g_crosshairSum = r + (g << 8) + (b << 16);
    CrosshairColors.r = r;
    CrosshairColors.g = g;
    CrosshairColors.b = b;

    char* ptr = (char*)waloff[CROSSHAIR];

    int32_t ii = tilesiz[CROSSHAIR].x * tilesiz[CROSSHAIR].y;

    Bassert(ii > 0);

    int32_t i = (videoGetRenderMode() == REND_CLASSIC)
        ? paletteGetClosestColor(CrosshairColors.r, CrosshairColors.g, CrosshairColors.b)
        : paletteGetClosestColor(255, 255, 255);  // use white in GL so we can tint it to the right color

    do
    {
        if (*ptr != 255)
            *ptr = i;
        ptr++;
    } while (--ii);

    paletteMakeLookupTable(CROSSHAIR_PAL, NULL, CrosshairColors.r, CrosshairColors.g, CrosshairColors.b, 1);

#ifdef USE_OPENGL
    // XXX: this makes us also load all hightile textures tinted with the crosshair color!
    polytint_t& crosshairtint = hictinting[CROSSHAIR_PAL];
    crosshairtint.r = CrosshairColors.r;
    crosshairtint.g = CrosshairColors.g;
    crosshairtint.b = CrosshairColors.b;
    crosshairtint.f = HICTINT_USEONART | HICTINT_GRAYSCALE;
#endif
    tileInvalidate(CROSSHAIR, -1, -1);
}

void G_ShowScores(void)
{
    int t, i, y, deaths, nemesis_kills, nemesis;

    if (playerswhenstarted > 1 && (GametypeFlags[ud.coop] & GAMETYPE_SCORESHEET))
    {
        rotatesprite(160 << 16, 34 << 16, 65536L, 0, INGAMEDUKETHREEDEE, 0, 0, 10, 0, 0, xdim - 1, ydim - 1);
        if (PLUTOPAK)   // JBF 20030804
            rotatesprite((260) << 16, 36 << 16, 65536L, 0, PLUTOPAKSPRITE + 2, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        gametext(160, 58 + 2, MapInfo[(ud.volume_number * MAXLEVELS) + ud.last_level - 1].name, 0, 2 + 8 + 16);

        //        gametext(160,165,"PRESS ANY KEY TO CONTINUE",0,2+8+16);

        int start_x = 64;
        int name_x = start_x + 12;
        int frags_x = start_x + 66;
        int deaths_x = start_x + 98;
        int nemesis_x = start_x + 134;
        int ping_x = start_x + 180;

        t = 0;
        minitext(start_x, 70, "#", 8, 2 + 8 + 16 + 128);
        minitext(name_x, 70, "NAME", 8, 2 + 8 + 16 + 128);
        minitext(frags_x, 70, "FRAGS", 8, 2 + 8 + 16 + 128);
        minitext(deaths_x, 70, "DEATHS", 8, 2 + 8 + 16 + 128);
        minitext(nemesis_x, 70, "NEMESIS", 8, 2 + 8 + 16 + 128);
        minitext(ping_x, 70, "PING", 8, 2 + 8 + 16 + 128);

        for (i = 0; i < playerswhenstarted; i++)
        {
            Bsprintf(tempbuf, "%d", i + 1);

            minitext(start_x, 80 + t, tempbuf, 0, 2 + 8 + 16 + 128);

            minitext(name_x, 80 + t, g_player[i].user_name, g_player[i].ps->palookup, 2 + 8 + 16 + 128);

            Bsprintf(tempbuf, "%-4d", g_player[i].ps->frag - g_player[i].ps->fraggedself);
            minitext(frags_x, 80 + t, tempbuf, 2, 2 + 8 + 16 + 128);

            deaths = 0;
            nemesis = -1;
            nemesis_kills = 0;
            for (y = 0; y < playerswhenstarted; y++)
            {
                int killer_frags = g_player[y].frags[i];
                deaths += killer_frags;

                if (killer_frags > nemesis_kills)
                {
                    nemesis_kills = killer_frags;
                    nemesis = y;
                }
            }

            Bsprintf(tempbuf, "%-4d", deaths + g_player[i].ps->fraggedself);
            minitext(deaths_x, 80 + t, tempbuf, 2, 2 + 8 + 16 + 128);

            if (nemesis > -1)
            {
                minitext(nemesis_x, 80 + t, g_player[nemesis].user_name, g_player[nemesis].ps->palookup, 2 + 8 + 16 + 128);
            }
            else if (g_player[i].ps->fraggedself > nemesis_kills)
            {
                minitext(nemesis_x, 80 + t, "Themself!", 6, 2 + 8 + 16 + 128);
            }
            else
            {
                minitext(nemesis_x, 80 + t, "Nobody", 12, 2 + 8 + 16 + 128);
            }

            if ((g_networkBroadcastMode == NETMODE_MASTERSLAVE) && i == connecthead)
            {
                minitext(ping_x, 80 + t, "Master", 8, 2 + 8 + 16 + 128);
            }
            else if ((g_networkBroadcastMode == NETMODE_P2P))
            {
                minitext(ping_x, 80 + t, "N/A", 2, 2 + 8 + 16 + 128);
            }
            else
            {
                Bsprintf(tempbuf, "%d", g_player[i].ping);
                minitext(ping_x, 80 + t, tempbuf, 12, 2 + 8 + 16 + 128);
            }

            t += 7;
        }
    }
}

void G_FadePalette(int r, int g, int b, int e)
{
    int tc;

    videoFadePalette(clamp(r << 2, 0, 255), clamp(g << 2, 0, 255), clamp(b << 2, 0, 255), (e&127) << 2);
    if (videoGetRenderMode() >= 3) pus = pub = NUMPAGES; // JBF 20040110: redraw the status bar next time
    if ((e & 128) == 0)
    {
        videoNextPage();
        for (tc = (int32_t)totalclock; totalclock < tc + 4; handleevents(), Net_GetPackets());
    }
}

void fadepal(int r, int g, int b, int start, int end, int step)
{
    if (videoGetRenderMode() >= 3) return;
    if (step > 0) for (; start < end; start += step) G_FadePalette(r, g, b, start);
    else for (; start >= end; start += step) G_FadePalette(r, g, b, start);
}

void G_DoOrderScreen(void)
{
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

    fadepal(0, 0, 0, 0, 63, 7);
    //g_player[myconnectindex].ps->palette = palette;
    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 1);    // JBF 20040308
    KB_FlushKeyboardQueue();
    rotatesprite(0, 0, 65536L, 0, ORDERING, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
    fadepal(0, 0, 0, 63, 0, -7);
    while (!KB_KeyWaiting())
    {
        handleevents();
        Net_GetPackets();
    }

    fadepal(0, 0, 0, 0, 63, 7);
    KB_FlushKeyboardQueue();
    rotatesprite(0, 0, 65536L, 0, ORDERING + 1, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
    fadepal(0, 0, 0, 63, 0, -7);
    while (!KB_KeyWaiting())
    {
        handleevents();
        Net_GetPackets();
    }

    fadepal(0, 0, 0, 0, 63, 7);
    KB_FlushKeyboardQueue();
    rotatesprite(0, 0, 65536L, 0, ORDERING + 2, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
    fadepal(0, 0, 0, 63, 0, -7);
    while (!KB_KeyWaiting())
    {
        handleevents();
        Net_GetPackets();
    }

    fadepal(0, 0, 0, 0, 63, 7);
    KB_FlushKeyboardQueue();
    rotatesprite(0, 0, 65536L, 0, ORDERING + 3, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
    fadepal(0, 0, 0, 63, 0, -7);
    while (!KB_KeyWaiting())
    {
        handleevents();
        Net_GetPackets();
    }
}

// Keep on bottom
void G_BonusScreen(int bonusonly)
{
    int t, gfx_offset;
    int i, y, xfragtotal, yfragtotal;
    int bonuscnt;
    int clockpad = 2;
    char* lastmapname;
    int playerbest = -1;

    int breathe[] =
    {
        0,  30,VICTORY1 + 1,176,59,
        30,  60,VICTORY1 + 2,176,59,
        60,  90,VICTORY1 + 1,176,59,
        90, 120,0         ,176,59
    };

    int bossmove[] =
    {
        0, 120,VICTORY1 + 3,86,59,
        220, 260,VICTORY1 + 4,86,59,
        260, 290,VICTORY1 + 5,86,59,
        290, 320,VICTORY1 + 6,86,59,
        320, 350,VICTORY1 + 7,86,59,
        350, 380,VICTORY1 + 8,86,59,
        350, 380,VICTORY1 + 8,86,59 // duplicate row to alleviate overflow in the for loop below "boss"
    };

    Bsprintf(tempbuf, "%s - " APPNAME, duke3dgrpstring);
    wm_setapptitle(tempbuf);

    if (ud.volume_number == 0 && ud.last_level == 8 && boardfilename[0])
    {
        lastmapname = Bstrrchr(boardfilename, '\\');
        if (!lastmapname) lastmapname = Bstrrchr(boardfilename, '/');
        if (!lastmapname) lastmapname = boardfilename;
    }
    else lastmapname = MapInfo[(ud.volume_number * MAXLEVELS) + ud.last_level - 1].name;

    bonuscnt = 0;

    fadepal(0, 0, 0, 0, 64, 7);
    videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
    videoClearViewableArea(0L);
    videoNextPage();
    renderFlushPerms();

    FX_StopAllSounds();
    S_ClearSoundLocks();
    FX_SetReverb(0L);
    CONTROL_BindsEnabled = 1; // so you can use your screenshot bind on the score screens

    if (bonusonly) goto FRAGBONUS;

    if (numplayers < 2 && ud.eog && ud.from_bonus == 0)
        switch (ud.volume_number)
        {
        case 0:
            if (ud.lockout == 0)
            {
                P_SetGamePalette(g_player[myconnectindex].ps, ENDINGPAL, 11); // JBF 20040308
                videoClearViewableArea(0L);
                rotatesprite(0, 50 << 16, 65536L, 0, VICTORY1, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                videoNextPage();
                //g_player[myconnectindex].ps->palette = endingpal;
                fadepal(0, 0, 0, 63, 0, -1);

                KB_FlushKeyboardQueue();
                totalclock = 0;
                while (1)
                {
                    videoClearViewableArea(0L);
                    rotatesprite(0, 50 << 16, 65536L, 0, VICTORY1, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);

                    // boss
                    if (totalclock > 390 && totalclock < 780)
                        for (t = 0; t < 35; t += 5) if (bossmove[t + 2] && (totalclock % 390) > bossmove[t] && (totalclock % 390) <= bossmove[t + 1])
                        {
                            if (t == 10 && bonuscnt == 1)
                            {
                                S_PlaySound(SHOTGUN_FIRE);
                                S_PlaySound(SQUISHED);
                                bonuscnt++;
                            }
                            rotatesprite(bossmove[t + 3] << 16, bossmove[t + 4] << 16, 65536L, 0, bossmove[t + 2], 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                        }

                    // Breathe
                    if (totalclock < 450 || totalclock >= 750)
                    {
                        if (totalclock >= 750)
                        {
                            rotatesprite(86 << 16, 59 << 16, 65536L, 0, VICTORY1 + 8, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                            if (totalclock >= 750 && bonuscnt == 2)
                            {
                                S_PlaySound(DUKETALKTOBOSS);
                                bonuscnt++;
                            }

                        }
                        for (t = 0; t < 20; t += 5)
                            if (breathe[t + 2] && (totalclock % 120) > breathe[t] && (totalclock % 120) <= breathe[t + 1])
                            {
                                if (t == 5 && bonuscnt == 0)
                                {
                                    S_PlaySound(BOSSTALKTODUKE);
                                    bonuscnt++;
                                }
                                rotatesprite(breathe[t + 3] << 16, breathe[t + 4] << 16, 65536L, 0, breathe[t + 2], 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                            }
                    }
                    handleevents();
                    Net_GetPackets();
                    videoNextPage();
                    if (KB_KeyWaiting()) break;
                }
            }

            fadepal(0, 0, 0, 0, 64, 1);

            KB_FlushKeyboardQueue();
            //g_player[myconnectindex].ps->palette = palette;
            P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 11);   // JBF 20040308

            rotatesprite(0, 0, 65536L, 0, 3292, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
            IFISSOFTMODE fadepal(0, 0, 0, 63, 0, -1);
            else videoNextPage();
            while (!KB_KeyWaiting() && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                handleevents();
                Net_GetPackets();
            }
            fadepal(0, 0, 0, 0, 64, 1);
            S_StopMusic();
            FX_StopAllSounds();
            S_ClearSoundLocks();
            break;
        case 1:
            S_StopMusic();
            videoClearViewableArea(0L);
            videoNextPage();

            if (ud.lockout == 0)
            {
                G_PlayAnim("cineov2.anm", 1);
                KB_FlushKeyboardQueue();
                videoClearViewableArea(0L);
                videoNextPage();
            }

            S_PlaySound(PIPEBOMB_EXPLODE);

            fadepal(0, 0, 0, 0, 64, 1);
            videoSetViewableArea(0, 0, xdim - 1, ydim - 1);
            KB_FlushKeyboardQueue();
            //g_player[myconnectindex].ps->palette = palette;
            P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 11);   // JBF 20040308
            rotatesprite(0, 0, 65536L, 0, 3293, 0, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
            IFISSOFTMODE fadepal(0, 0, 0, 63, 0, -1);
            else videoNextPage();
            while (!KB_KeyWaiting() && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                handleevents();
                Net_GetPackets();
            }
            IFISSOFTMODE fadepal(0, 0, 0, 0, 64, 1);

            break;

        case 3:

            videoSetViewableArea(0, 0, xdim - 1, ydim - 1);

            S_StopMusic();
            videoClearViewableArea(0L);
            videoNextPage();

            if (ud.lockout == 0)
            {
                KB_FlushKeyboardQueue();
                G_PlayAnim("vol4e1.anm", 8);
                videoClearViewableArea(0L);
                videoNextPage();
                G_PlayAnim("vol4e2.anm", 10);
                videoClearViewableArea(0L);
                videoNextPage();
                G_PlayAnim("vol4e3.anm", 11);
                videoClearViewableArea(0L);
                videoNextPage();
            }

            FX_StopAllSounds();
            S_ClearSoundLocks();
            S_PlaySound(ENDSEQVOL3SND4);
            KB_FlushKeyboardQueue();

            //g_player[myconnectindex].ps->palette = palette;
            P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 11);   // JBF 20040308
            IFISSOFTMODE G_FadePalette(0, 0, 0, 63);
            videoClearViewableArea(0L);
            menutext(160, 60, 0, 0, "THANKS TO ALL OUR");
            menutext(160, 60 + 16, 0, 0, "FANS FOR GIVING");
            menutext(160, 60 + 16 + 16, 0, 0, "US BIG HEADS.");
            menutext(160, 70 + 16 + 16 + 16, 0, 0, "LOOK FOR A DUKE NUKEM 3D");
            menutext(160, 70 + 16 + 16 + 16 + 16, 0, 0, "SEQUEL SOON.");
            videoNextPage();

            fadepal(0, 0, 0, 63, 0, -3);
            KB_FlushKeyboardQueue();
            while (!KB_KeyWaiting() && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                handleevents();
                Net_GetPackets();
            }
            fadepal(0, 0, 0, 0, 64, 3);

            videoClearViewableArea(0L);
            videoNextPage();
            MOUSE_ClearButton(LEFT_MOUSE);

            G_PlayAnim("DUKETEAM.ANM", 4);

            KB_FlushKeyboardQueue();
            while (!KB_KeyWaiting() && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                handleevents();
                Net_GetPackets();
            }

            videoClearViewableArea(0L);
            videoNextPage();
            IFISSOFTMODE G_FadePalette(0, 0, 0, 63);

            FX_StopAllSounds();
            S_ClearSoundLocks();
            KB_FlushKeyboardQueue();
            MOUSE_ClearButton(LEFT_MOUSE);

            break;

        case 2:

            S_StopMusic();
            videoClearViewableArea(0L);
            videoNextPage();
            if (ud.lockout == 0)
            {
                fadepal(0, 0, 0, 63, 0, -1);
                G_PlayAnim("cineov3.anm", 2);
                KB_FlushKeyboardQueue();
                ototalclock = (int32_t)totalclock + 200;
                while (totalclock < ototalclock)
                {
                    handleevents();
                    Net_GetPackets();
                }
                videoClearViewableArea(0L);
                videoNextPage();

                FX_StopAllSounds();
                S_ClearSoundLocks();
            }

            G_PlayAnim("RADLOGO.ANM", 3);

            if (ud.lockout == 0 && !KB_KeyWaiting() && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                S_PlaySound(ENDSEQVOL3SND5);
                while (S_CheckSoundPlaying(ENDSEQVOL3SND5))
                {
                    handleevents();
                    Net_GetPackets();
                }
                if (KB_KeyWaiting() || MOUSE_GetButtons() & LEFT_MOUSE) goto ENDANM;
                S_PlaySound(ENDSEQVOL3SND6);
                while (S_CheckSoundPlaying(ENDSEQVOL3SND6))
                {
                    handleevents();
                    Net_GetPackets();
                }
                if (KB_KeyWaiting() || MOUSE_GetButtons() & LEFT_MOUSE) goto ENDANM;
                S_PlaySound(ENDSEQVOL3SND7);
                while (S_CheckSoundPlaying(ENDSEQVOL3SND7))
                {
                    handleevents();
                    Net_GetPackets();
                }
                if (KB_KeyWaiting() || MOUSE_GetButtons() & LEFT_MOUSE) goto ENDANM;
                S_PlaySound(ENDSEQVOL3SND8);
                while (S_CheckSoundPlaying(ENDSEQVOL3SND8))
                {
                    handleevents();
                    Net_GetPackets();
                }
                if (KB_KeyWaiting() || MOUSE_GetButtons() & LEFT_MOUSE) goto ENDANM;
                S_PlaySound(ENDSEQVOL3SND9);
                while (S_CheckSoundPlaying(ENDSEQVOL3SND9))
                {
                    handleevents();
                    Net_GetPackets();
                }

            }
            MOUSE_ClearButton(LEFT_MOUSE);
            KB_FlushKeyboardQueue();
            totalclock = 0;
            while (!KB_KeyWaiting() && totalclock < 120 && !MOUSE_GetButtons() & LEFT_MOUSE)
            {
                handleevents();
                Net_GetPackets();
            }

        ENDANM:
            MOUSE_ClearButton(LEFT_MOUSE);
            FX_StopAllSounds();
            S_ClearSoundLocks();

            KB_FlushKeyboardQueue();

            videoClearViewableArea(0L);

            break;
        }

FRAGBONUS:

    //g_player[myconnectindex].ps->palette = palette;
    P_SetGamePalette(g_player[myconnectindex].ps, BASEPAL, 11);   // JBF 20040308
    IFISSOFTMODE G_FadePalette(0, 0, 0, 63);   // JBF 20031228
    KB_FlushKeyboardQueue();
    totalclock = 0;
    bonuscnt = 0;

    S_StopMusic();
    FX_StopAllSounds();
    S_ClearSoundLocks();

    if (playerswhenstarted > 1 && (GametypeFlags[ud.coop] & GAMETYPE_SCORESHEET))
    {
        if (!(ud.config.MusicToggle == 0 || ud.config.MusicDevice < 0))
            S_PlaySound(BONUSMUSIC);

        rotatesprite(0, 0, 65536L, 0, MENUSCREEN, 16, 0, 2 + 8 + 16 + 64, 0, 0, xdim - 1, ydim - 1);
        rotatesprite(160 << 16, 34 << 16, 65536L, 0, INGAMEDUKETHREEDEE, 0, 0, 10, 0, 0, xdim - 1, ydim - 1);
        if (PLUTOPAK)   // JBF 20030804
            rotatesprite((260) << 16, 36 << 16, 65536L, 0, PLUTOPAKSPRITE + 2, 0, 0, 2 + 8, 0, 0, xdim - 1, ydim - 1);
        gametext(160, 58 + 2, "MULTIPLAYER TOTALS", 0, 2 + 8 + 16);
        gametext(160, 58 + 10, MapInfo[(ud.volume_number * MAXLEVELS) + ud.last_level - 1].name, 0, 2 + 8 + 16);

        gametext(160, 165, "PRESS ANY KEY TO CONTINUE", 0, 2 + 8 + 16);

        t = 0;
        minitext(23, 80, "   NAME                                           KILLS", 8, 2 + 8 + 16 + 128);
        for (i = 0; i < playerswhenstarted; i++)
        {
            Bsprintf(tempbuf, "%-4d", i + 1);
            minitext(92 + (i * 23), 80, tempbuf, 3, 2 + 8 + 16 + 128);
        }

        for (i = 0; i < playerswhenstarted; i++)
        {
            xfragtotal = 0;
            Bsprintf(tempbuf, "%d", i + 1);

            minitext(30, 90 + t, tempbuf, 0, 2 + 8 + 16 + 128);
            minitext(38, 90 + t, g_player[i].user_name, g_player[i].ps->palookup, 2 + 8 + 16 + 128);

            for (y = 0; y < playerswhenstarted; y++)
            {
                if (i == y)
                {
                    Bsprintf(tempbuf, "%-4d", g_player[y].ps->fraggedself);
                    minitext(92 + (y * 23), 90 + t, tempbuf, 2, 2 + 8 + 16 + 128);
                    xfragtotal -= g_player[y].ps->fraggedself;
                }
                else
                {
                    Bsprintf(tempbuf, "%-4d", g_player[i].frags[y]);
                    minitext(92 + (y * 23), 90 + t, tempbuf, 0, 2 + 8 + 16 + 128);
                    xfragtotal += g_player[i].frags[y];
                }

                if (myconnectindex == connecthead)
                {
                    Bsprintf(tempbuf, "stats %d killed %d %d\n", i + 1, y + 1, g_player[i].frags[y]);
                    sendscore(tempbuf);
                }
            }

            Bsprintf(tempbuf, "%-4d", xfragtotal);
            minitext(101 + (8 * 23), 90 + t, tempbuf, 2, 2 + 8 + 16 + 128);

            t += 7;
        }

        for (y = 0; y < playerswhenstarted; y++)
        {
            yfragtotal = 0;
            for (i = 0; i < playerswhenstarted; i++)
            {
                if (i == y)
                    yfragtotal += g_player[i].ps->fraggedself;
                yfragtotal += g_player[i].frags[y];
            }
            Bsprintf(tempbuf, "%-4d", yfragtotal);
            minitext(92 + (y * 23), 96 + (8 * 7), tempbuf, 2, 2 + 8 + 16 + 128);
        }

        minitext(45, 96 + (8 * 7), "DEATHS", 8, 2 + 8 + 16 + 128);
        videoNextPage();

        fadepal(0, 0, 0, 63, 0, -7);

        KB_FlushKeyboardQueue();

        {
            int tc = (int32_t)totalclock;
            while (KB_KeyWaiting() == 0)
            {
                // continue after 10 seconds...
                if (totalclock > tc + (120 * 10)) break;
                handleevents();
                Net_GetPackets();
            }
        }

        if (bonusonly || ud.multimode > 1) return;

        fadepal(0, 0, 0, 0, 64, 7);
    }

    if (bonusonly || ud.multimode > 1) return;

    switch (ud.volume_number)
    {
    case 1:
        gfx_offset = 5;
        break;
    default:
        gfx_offset = 0;
        break;
    }

    rotatesprite(0, 0, 65536L, 0, BONUSSCREEN + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);

    menutext(160, 20 - 6, 0, 0, lastmapname);
    menutext(160, 36 - 6, 0, 0, "COMPLETED");

    gametext(160, 192, "PRESS ANY KEY TO CONTINUE", 16, 2 + 8 + 16);

    if (!(ud.config.MusicToggle == 0 || ud.config.MusicDevice < 0))
        S_PlaySound(BONUSMUSIC);

    videoNextPage();
    KB_FlushKeyboardQueue();
    fadepal(0, 0, 0, 63, 0, -1);
    bonuscnt = 0;
    totalclock = 0;

    playerbest = CONFIG_GetMapBestTime(MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].filename);

    if (g_player[myconnectindex].ps->player_par < playerbest || playerbest < 0)
    {
        CONFIG_SetMapBestTime(MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].filename, g_player[myconnectindex].ps->player_par);
        //        if(playerbest != -1)
        //            playerbest = g_player[myconnectindex].ps->player_par;
    }

    {
        int ii, ij;

        for (ii = g_player[myconnectindex].ps->player_par / (26 * 60), ij = 1; ii > 9; ii /= 10, ij++);
        clockpad = max(clockpad, ij);
        if (!(ud.volume_number == 0 && ud.last_level - 1 == 7))
        {
            for (ii = MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].partime / (26 * 60), ij = 1; ii > 9; ii /= 10, ij++);
            clockpad = max(clockpad, ij);
            if (!NAM)
            {
                for (ii = MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].designertime / (26 * 60), ij = 1; ii > 9; ii /= 10, ij++);
                clockpad = max(clockpad, ij);
            }
        }
        if (playerbest > 0) for (ii = playerbest / (26 * 60), ij = 1; ii > 9; ii /= 10, ij++);
        clockpad = max(clockpad, ij);
    }

    while (1)
    {
        int yy = 0, zz;

        Net_GetPackets();
        handleevents();
        MUSIC_Update();

        if (g_player[myconnectindex].ps->gm & MODE_EOL)
        {
            rotatesprite(0, 0, 65536L, 0, BONUSSCREEN + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);

            if (totalclock > (1000000000L) && totalclock < (1000000320L))
            {
                switch (((int32_t)totalclock >> 4) % 15)
                {
                case 0:
                    if (bonuscnt == 6)
                    {
                        bonuscnt++;
                        S_PlaySound(SHOTGUN_COCK);
                        switch (rand() & 3)
                        {
                        case 0:
                            S_PlaySound(BONUS_SPEECH1);
                            break;
                        case 1:
                            S_PlaySound(BONUS_SPEECH2);
                            break;
                        case 2:
                            S_PlaySound(BONUS_SPEECH3);
                            break;
                        case 3:
                            S_PlaySound(BONUS_SPEECH4);
                            break;
                        }
                    }
                    fallthrough__;
                case 1:
                case 4:
                case 5:
                    rotatesprite(199 << 16, 31 << 16, 65536L, 0, BONUSSCREEN + 3 + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                    break;
                case 2:
                case 3:
                    rotatesprite(199 << 16, 31 << 16, 65536L, 0, BONUSSCREEN + 4 + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                    break;
                }
            }
            else if (totalclock > (10240 + 120L)) break;
            else
            {
                switch (((int32_t)totalclock >> 5) & 3)
                {
                case 1:
                case 3:
                    rotatesprite(199 << 16, 31 << 16, 65536L, 0, BONUSSCREEN + 1 + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                    break;
                case 2:
                    rotatesprite(199 << 16, 31 << 16, 65536L, 0, BONUSSCREEN + 2 + gfx_offset, 0, 0, 2 + 8 + 16 + 64 + 128, 0, 0, xdim - 1, ydim - 1);
                    break;
                }
            }

            menutext(160, 20 - 6, 0, 0, lastmapname);
            menutext(160, 36 - 6, 0, 0, "COMPLETED");

            gametext(160, 192, "PRESS ANY KEY TO CONTINUE", 16, 2 + 8 + 16);

            if (totalclock > (60 * 3))
            {
                yy = zz = 59;
                gametext(10, yy + 9, "Your Time:", 0, 2 + 8 + 16);
                yy += 10;
                if (!(ud.volume_number == 0 && ud.last_level - 1 == 7))
                {
                    gametext(10, yy + 9, "Par Time:", 0, 2 + 8 + 16);
                    yy += 10;
                    if (!NAM)
                    {
                        gametext(10, yy + 9, "3D Realms' Time:", 0, 2 + 8 + 16);
                        yy += 10;
                    }

                }
                if (playerbest > 0)
                {
                    gametext(10, yy + 9, g_player[myconnectindex].ps->player_par < playerbest ? "Prev Best Time:" : "Your Best Time:", 0, 2 + 8 + 16);
                    yy += 10;
                }

                if (bonuscnt == 0)
                    bonuscnt++;

                yy = zz;
                if (totalclock > (60 * 4))
                {
                    if (bonuscnt == 1)
                    {
                        bonuscnt++;
                        S_PlaySound(PIPEBOMB_EXPLODE);
                    }

                    Bsprintf(tempbuf, "%0*d:%02d.%02d", clockpad,
                        (g_player[myconnectindex].ps->player_par / (26 * 60)),
                        (g_player[myconnectindex].ps->player_par / 26) % 60,
                        ((g_player[myconnectindex].ps->player_par % 26) * 38) / 10
                    );
                    gametext((320 >> 2) + 71, yy + 9, tempbuf, 0, 2 + 8 + 16);
                    if (g_player[myconnectindex].ps->player_par < playerbest)
                        gametext((320 >> 2) + 89 + (clockpad * 24), yy + 9, "New record!", 0, 2 + 8 + 16);
                    yy += 10;

                    if (!(ud.volume_number == 0 && ud.last_level - 1 == 7))
                    {
                        Bsprintf(tempbuf, "%0*d:%02d", clockpad,
                            (MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].partime / (26 * 60)),
                            (MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].partime / 26) % 60);
                        gametext((320 >> 2) + 71, yy + 9, tempbuf, 0, 2 + 8 + 16);
                        yy += 10;

                        if (!NAM)
                        {
                            Bsprintf(tempbuf, "%0*d:%02d", clockpad,
                                (MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].designertime / (26 * 60)),
                                (MapInfo[ud.volume_number * MAXLEVELS + ud.last_level - 1].designertime / 26) % 60);
                            gametext((320 >> 2) + 71, yy + 9, tempbuf, 0, 2 + 8 + 16);
                            yy += 10;
                        }
                    }

                    if (playerbest > 0)
                    {
                        sprintf(tempbuf, "%0*d:%02d.%02d", clockpad,
                            (playerbest / (26 * 60)),
                            (playerbest / 26) % 60,
                            ((playerbest % 26) * 38) / 10
                        );
                        gametext((320 >> 2) + 71, yy + 9, tempbuf, 0, 2 + 8 + 16);
                        yy += 10;
                    }
                }
            }

            zz = yy += 5;
            if (totalclock > (60 * 6))
            {
                gametext(10, yy + 9, "Enemies Killed:", 0, 2 + 8 + 16);
                yy += 10;
                gametext(10, yy + 9, "Enemies Left:", 0, 2 + 8 + 16);
                yy += 10;

                if (bonuscnt == 2)
                {
                    bonuscnt++;
                    S_PlaySound(FLY_BY);
                }

                yy = zz;

                if (totalclock > (60 * 7))
                {
                    if (bonuscnt == 3)
                    {
                        bonuscnt++;
                        S_PlaySound(PIPEBOMB_EXPLODE);
                    }
                    sprintf(tempbuf, "%-3d", g_player[myconnectindex].ps->actors_killed);
                    gametext((320 >> 2) + 70, yy + 9, tempbuf, 0, 2 + 8 + 16);
                    yy += 10;
                    if (ud.player_skill > 3)
                    {
                        sprintf(tempbuf, "N/A");
                        gametext((320 >> 2) + 70, yy + 9, tempbuf, 0, 2 + 8 + 16);
                        yy += 10;
                    }
                    else
                    {
                        if ((g_player[myconnectindex].ps->max_actors_killed - g_player[myconnectindex].ps->actors_killed) < 0)
                            sprintf(tempbuf, "%-3d", 0);
                        else sprintf(tempbuf, "%-3d", g_player[myconnectindex].ps->max_actors_killed - g_player[myconnectindex].ps->actors_killed);
                        gametext((320 >> 2) + 70, yy + 9, tempbuf, 0, 2 + 8 + 16);
                        yy += 10;
                    }
                }
            }

            zz = yy += 5;
            if (totalclock > (60 * 9))
            {
                gametext(10, yy + 9, "Secrets Found:", 0, 2 + 8 + 16);
                yy += 10;
                gametext(10, yy + 9, "Secrets Missed:", 0, 2 + 8 + 16);
                yy += 10;
                if (bonuscnt == 4) bonuscnt++;

                yy = zz;
                if (totalclock > (60 * 10))
                {
                    if (bonuscnt == 5)
                    {
                        bonuscnt++;
                        S_PlaySound(PIPEBOMB_EXPLODE);
                    }
                    sprintf(tempbuf, "%-3d", g_player[myconnectindex].ps->secret_rooms);
                    gametext((320 >> 2) + 70, yy + 9, tempbuf, 0, 2 + 8 + 16);
                    yy += 10;
                    if (g_player[myconnectindex].ps->secret_rooms > 0)
                        sprintf(tempbuf, "%-3d%%", (100 * g_player[myconnectindex].ps->secret_rooms / g_player[myconnectindex].ps->max_secret_rooms));
                    sprintf(tempbuf, "%-3d", g_player[myconnectindex].ps->max_secret_rooms - g_player[myconnectindex].ps->secret_rooms);
                    gametext((320 >> 2) + 70, yy + 9, tempbuf, 0, 2 + 8 + 16);
                    yy += 10;
                }
            }

            if (totalclock > 10240 && totalclock < 10240 + 10240)
                totalclock = 1024;

            if (((MOUSE_GetButtons() & 7) || KB_KeyWaiting()) && totalclock > (60 * 2)) // JBF 20030809
            {
                MOUSE_ClearButton(7);
                if (totalclock < (60 * 13))
                {
                    KB_FlushKeyboardQueue();
                    totalclock = (60 * 13);
                }
                else if (totalclock < (1000000000L))
                    totalclock = (1000000000L);
            }
        }
        else break;
        X_OnEvent(EVENT_DISPLAYBONUSSCREEN, g_player[screenpeek].ps->i, screenpeek, -1);
        videoNextPage();
    }
}