//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2020 EDuke32 developers and contributors
Copyright (C) 2020 sirlemonhead, Nuke.YKT

This file is part of PCExhumed.

PCExhumed is free software; you can redistribute it and/or
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

#include "ramses.h"
#include "engine.h"
#include "exhumed.h"
#include "random.h"
#include "cd.h"
#include "sound.h"
#include "view.h"
#include "light.h"
#include "lighting.h"
#include "names.h"
#include "player.h"
#include "gun.h"
#include "kexbreak.h"
#include "init.h"


#define kSpiritX   106
#define kSpiritY   97

short cPupData[300];
uint8_t worktile[(kSpiritY * 2) * (kSpiritX * 2)] = {0};
int lHeadStartClock;
short* pPupData;
int lNextStateChange;
int nPixels;
int nHeadTimeStart;
short nHeadStage;
short curx[kSpiritY * kSpiritX];
short cury[kSpiritY * kSpiritX];
int8_t destvelx[kSpiritY * kSpiritX];
int8_t destvely[kSpiritY * kSpiritX];
uint8_t pixelval[kSpiritY * kSpiritX];
int8_t origy[kSpiritY * kSpiritX];
int8_t origx[kSpiritY * kSpiritX];
int8_t velx[kSpiritY * kSpiritX];
int8_t vely[kSpiritY * kSpiritX];
short nMouthTile;

short nPupData = 0;
buildvfs_kfd headfd = buildvfs_kfd_invalid;

short word_964E8 = 0;
short word_964EA = 0;
short word_964EC = 10;

short nSpiritRepeatX;
short nSpiritRepeatY;
short nSpiritSprite;
short nPixelsToShow;
short nTalkTime = 0;


// ---------------------------------------------------------------------------
// The remaster's Ramses. His speeches ship complete in the .kpf: the audio as
// ramses/SP_<LANG><NN>.wav in four languages, and the lip-sync as AngelScript
// const-int arrays in scripts/ramses/SP_*.txt, one 4-bit head state per 1/30s
// of audio -- bit 3 is the mouth. Everything else about the spirit head --
// the swirlies, the materialise, the eyes and blinks, the mouth tiles -- is
// this file's own machinery and works untouched; all a speech IS to it is a
// list of totalclock timestamps at which the mouth toggles (the .PUP format),
// so the kex path derives those timestamps from the bit-3 edges of the script
// array (one entry = 4 ticks of the 120 Hz clock) and plays the wav through
// the music channel, which is what CDplaying() watches anyway. Which speech:
// the hub script picks by the artifacts collected; artifacts do not exist
// here yet, so the weapon that marks the same stage of the game stands in.
// ---------------------------------------------------------------------------
int kex_language = 0;      // cvar: 0 english, 1 french, 2 german, 3 spanish

static short sKexPup[1200];
static uint8_t sKexWasHidden[(kMaxSprites + 7) >> 3];
static int nKexSpeaking = 0;

static int Kex_RamsesSpeech(void)
{
    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    if (Bstrstr(nameUp, "TOMBEND"))
        return 13;         // the finale address; 14 is the all-dolls variant
    uint16_t const w = nPlayerWeapons[0];
    if (w & (1 << kWeaponStaff))   return 9;
    if (w & (1 << kWeaponFlamer))  return 7;
    if (w & (1 << kWeaponGrenade)) return 5;
    if (w & (1 << kWeaponM60))     return 3;
    return 1;
}

static int Kex_RamsesStart(void)
{
    static char const * const langs[] = { "ENG", "FRE", "GER", "SPA" };
    char const *lang = langs[clamp(kex_language, 0, 3)];
    int const nSpeech = Kex_RamsesSpeech();

    char path[80];
    Bsprintf(path, "scripts/ramses/SP_%s%02d.txt", lang, nSpeech);
    int const fil = kopen4loadfrommod(path, 0);
    if (fil == -1)
        return 0;
    int const len = kfilelength(fil);
    char *buf = (char *)Xmalloc(len + 1);
    kread(fil, buf, len); buf[len] = 0; kclose(fil);

    // Mouth-open (bit 3) edges -> .PUP-style toggle timestamps.
    int prev = 0, n = 0, idx = 0;
    for (char *p = buf; *p; )
    {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) break;
        int const v = (int)strtol(p, &p, 10);
        if (((v ^ prev) & 8) && n < (int)ARRAY_SIZE(sKexPup))
        {
            sKexPup[n++] = (short)(idx << 2);
            prev = v;
        }
        idx++;
    }
    Xfree(buf);
    if (n == 0)
        return 0;

    Bsprintf(path, "ramses/SP_%s%02d.wav", lang, nSpeech);
    playMusicFile(path, false);

    pPupData = sKexPup;
    nPupData = (short)(n * 2);
    bSubTitles = kFalse;       // nothing to caption: the kpf ships no transcripts
    nKexSpeaking = 1;
    return 1;
}

// The Build game ends the level when Ramses finishes -- his speech IS its
// intro cinematic. In the remaster's hub he vanishes and play resumes, so
// this puts back everything InitSpiritHead() took: the other sprites, the
// player's freeze, the one-shot pad flag, and the level's own music.
static void Kex_RamsesEnd(void)
{
    OSD_Printf("Kex Ramses: speech over, play resumes\n");
    nKexSpeaking = 0;

    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum && !(sKexWasHidden[i >> 3] & (1 << (i & 7))))
            sprite[i].cstat &= 0x7FFF;
    }
    sprite[nSpiritSprite].cstat |= 0x8000;
    sprite[nSpiritSprite].xrepeat = (uint8_t)nSpiritRepeatX;
    sprite[nSpiritSprite].yrepeat = (uint8_t)nSpiritRepeatY;

    for (int i = 0; i < numsectors; i++)
        SectFlag[i] &= 0x7FFF;

    nFreeze = 0;
    fadecdaudio();
    char const *track = Kex_MusicForMap(gUserMapFilename);
    if (track)
        playMusicFile(track, true);
}

void InitSpiritHead()
{
    char filename[20];

    nPixels = 0;

    nSpiritRepeatX = sprite[nSpiritSprite].xrepeat;
    nSpiritRepeatY = sprite[nSpiritSprite].yrepeat;

    if (!waloff[kTileRamsesNormal]) tileLoad(kTileRamsesNormal); // Ramses Normal Head
    // The gold head is read below without ever being drawn first; every other
    // caller happened to have it cached, the kex path does not.
    if (!waloff[kTileRamsesGold]) tileLoad(kTileRamsesGold);

    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum) {
            if (sprite[i].cstat & 0x8000)
                sKexWasHidden[i >> 3] |= (uint8_t)(1 << (i & 7));
            else
                sKexWasHidden[i >> 3] &= (uint8_t)~(1 << (i & 7));
            sprite[i].cstat |= 0x8000;
        }
    }

    uint8_t* pTile = (uint8_t*)waloff[kTileRamsesNormal];

    for (int x = 0; x < kSpiritY; x++)
    {
        for (int y = 0; y < kSpiritX; y++)
        {
            if (*pTile != 255)
            {
                pixelval[nPixels] = *(uint8_t*)(waloff[kTileRamsesGold] + x * kSpiritX + y);
                origx[nPixels] = x - 48;
                origy[nPixels] = y - 53;
                curx[nPixels] = 0;
                cury[nPixels] = 0;
                vely[nPixels] = 0;
                velx[nPixels] = 0;

                destvelx[nPixels] = RandomSize(2) + 1;

                if (curx[nPixels] > 0) {
                    destvelx[nPixels] = -destvelx[nPixels];
                }

                destvely[nPixels] = RandomSize(2) + 1;

                if (cury[nPixels] > 0) {
                    destvely[nPixels] = -destvely[nPixels];
                }

                nPixels++;
            }

            pTile++;
        }
    }

    waloff[kTileRamsesWorkTile] = (intptr_t)worktile;

    sprite[nSpiritSprite].yrepeat = 140;
    sprite[nSpiritSprite].xrepeat = 140;
    sprite[nSpiritSprite].picnum = kTileRamsesWorkTile;

    nHeadStage = 0;

    // work tile is twice as big as the normal head size
    tilesiz[kTileRamsesWorkTile].x = kSpiritY * 2;
    tilesiz[kTileRamsesWorkTile].y = kSpiritX * 2;

    sprite[nSpiritSprite].cstat &= 0x7FFF;

    nHeadTimeStart = (int)totalclock;

    memset(worktile, -1, sizeof(worktile));
    tileInvalidate(kTileRamsesWorkTile, -1, -1);

    nPixelsToShow = 0;

    fadecdaudio();

    if (!g_kexMode)
    {
        // The DOS game's speeches are CD audio: track 3 opens the game, 7
        // opens the second act. The kex path plays its wav instead, from
        // Kex_RamsesStart() below.
        bSubTitles = playCDtrack(levelnum == 1 ? 3 : 7, false) == 0;
    }

    StartSwirlies();

    lNextStateChange = (int)totalclock;
    lHeadStartClock = (int)totalclock;

    if (!(g_kexMode && Kex_RamsesStart()))
    {
        sprintf(filename, "LEV%d.PUP", levelnum);

        headfd = kopen4loadfrommod(filename, 0);
        nPupData = kread(headfd, cPupData, sizeof(cPupData));
        #if B_BIG_ENDIAN == 1
        for (int i = 0; i < ARRAY_SIZE(cPupData); i++)
        {
            cPupData[i] = B_LITTLE16(cPupData[i]);
        }
        #endif
        pPupData = cPupData;
        kclose(headfd);
        headfd = buildvfs_kfd_invalid;
    }
    nMouthTile = 0;
    nTalkTime = 1;
}

void DimSector(short nSector)
{
    short startwall = sector[nSector].wallptr;
    short nWalls = sector[nSector].wallnum;

    for (int i = 0; i < nWalls; i++)
    {
        if (wall[startwall + i].shade < 40) {
            wall[startwall + i].shade++;
        }
    }

    if (sector[nSector].floorshade < 40) {
        sector[nSector].floorshade++;
    }

    if (sector[nSector].ceilingshade < 40) {
        sector[nSector].ceilingshade++;
    }
}

void CopyHeadToWorkTile(short nTile)
{
    if (!waloff[nTile]) tileLoad(nTile);

    uint8_t* pSrc = (uint8_t*)waloff[nTile];
    uint8_t* pDest = (uint8_t*)&worktile[212 * 49 + 53];

    for (int i = 0; i < kSpiritY; i++)
    {
        memcpy(pDest, pSrc, kSpiritX);

        pDest += 212;
        pSrc += kSpiritX;
    }
}

int DoSpiritHead()
{
    static short word_964E6 = 0;

    nVertPan[0] += (nDestVertPan[0] - nVertPan[0]) / 4;

    tileInvalidate(kTileRamsesWorkTile, -1, -1);

    if (nHeadStage < 2) {
        memset(worktile, -1, sizeof(worktile));
    }

    if (nHeadStage < 2 || nHeadStage != 5)
    {
        nPixelsToShow = ((int)totalclock - nHeadTimeStart) * 15;

        if (nPixelsToShow > nPixels) {
            nPixelsToShow = nPixels;
        }

        if (nHeadStage < 3)
        {
            UpdateSwirlies();

            if (sprite[nSpiritSprite].shade > -127) {
                sprite[nSpiritSprite].shade--;
            }

            word_964E6--;
            if (word_964E6 < 0)
            {
                DimSector(sprite[nSpiritSprite].sectnum);
                word_964E6 = 5;
            }

            if (!nHeadStage)
            {
                if (((int)totalclock - nHeadTimeStart) > 480)
                {
                    nHeadStage = 1;
                    nHeadTimeStart = (int)totalclock + 480;
                }

                for (int i = 0; i < nPixelsToShow; i++)
                {
                    if (destvely[i] >= 0)
                    {
                        vely[i]++;

                        if (vely[i] >= destvely[i]) {
                            destvely[i] = -(RandomSize(2) + 1);
                        }
                    }
                    else
                    {
                        vely[i]--;

                        if (vely[i] <= destvely[i]) {
                            destvely[i] = RandomSize(2) + 1;
                        }
                    }

                    if (destvelx[i] >= 0)
                    {
                        velx[i]++;

                        if (velx[i] >= destvelx[i]) {
                            destvelx[i] = -(RandomSize(2) + 1);
                        }
                    }
                    else
                    {
                        velx[i]--;

                        if (velx[i] <= destvelx[i]) {
                            destvelx[i] = RandomSize(2) + 1;
                        }
                    }

                    int esi = vely[i] + (cury[i] >> 8);

                    if (esi < kSpiritX)
                    {
                        if (esi < -105)
                        {
                            vely[i] = 0;
                            esi = 0;
                        }
                    }
                    else
                    {
                        vely[i] = 0;
                        esi = 0;
                    }

                    int ebx = velx[i] + (curx[i] >> 8);

                    if (ebx < kSpiritY)
                    {
                        if (ebx < -96)
                        {
                            velx[i] = 0;
                            ebx = 0;
                        }
                    }
                    else
                    {
                        velx[i] = 0;
                        ebx = 0;
                    }

                    curx[i] = ebx * 256;
                    cury[i] = esi * 256;

                    esi += (ebx + kSpiritY) * 212;

                    worktile[kSpiritX + esi] = pixelval[i];
                }

                return 1;
            }
            else
            {
                if (nHeadStage != 1) {
                    return 1;
                }

                uint8_t nXRepeat = sprite[nSpiritSprite].xrepeat;
                if (nXRepeat > nSpiritRepeatX)
                {
                    sprite[nSpiritSprite].xrepeat -= 2;

                    nXRepeat = sprite[nSpiritSprite].xrepeat;
                    if (nXRepeat < nSpiritRepeatX) {
                        sprite[nSpiritSprite].xrepeat = nSpiritRepeatX;
                    }
                }

                uint8_t nYRepeat = sprite[nSpiritSprite].yrepeat;
                if (nYRepeat > nSpiritRepeatY)
                {
                    sprite[nSpiritSprite].yrepeat -= 2;

                    nYRepeat = sprite[nSpiritSprite].yrepeat;
                    if (nYRepeat < nSpiritRepeatY) {
                        sprite[nSpiritSprite].yrepeat = nSpiritRepeatY;
                    }
                }

                int esi = 0;

                for (int i = 0; i < nPixels; i++)
                {
                    int eax = (origx[i] << 8) - curx[i];
                    int ecx = eax;

                    if (eax)
                    {
                        if (eax < 0) {
                            eax = -eax;
                        }

                        if (eax < 8)
                        {
                            curx[i] = origx[i] << 8;
                            ecx = 0;
                        }
                        else {
                            ecx >>= 3;
                        }
                    }
                    else
                    {
                        ecx >>= 3;
                    }

                    int var_1C = (origy[i] << 8) - cury[i];
                    int ebp = var_1C;

                    if (var_1C)
                    {
                        eax = ebp;

                        if (eax < 0) {
                            eax = -eax;
                        }

                        if (eax < 8)
                        {
                            cury[i] = origy[i] << 8;
                            var_1C = 0;
                        }
                        else {
                            var_1C >>= 3;
                        }
                    }
                    else
                    {
                        var_1C >>= 3;
                    }

                    if (var_1C || ecx)
                    {
                        curx[i] += ecx;
                        cury[i] += var_1C;

                        esi++;
                    }

                    ecx = (((curx[i] >> 8) + kSpiritY) * 212) + (cury[i] >> 8);

                    worktile[kSpiritX + ecx] = pixelval[i];
                }

                if (((int)totalclock - lHeadStartClock) > 600)
                {
                    CopyHeadToWorkTile(kTileRamsesGold);
                }

                int eax = ((nPixels << 4) - nPixels) / 16;

                if (esi < eax)
                {
                    SoundBigEntrance();
                    AddGlow(sprite[nSpiritSprite].sectnum, 20);
                    AddFlash(
                        sprite[nSpiritSprite].sectnum,
                        sprite[nSpiritSprite].x,
                        sprite[nSpiritSprite].y,
                        sprite[nSpiritSprite].z,
                        128);

                    nHeadStage = 3;
                    TintPalette(255, 255, 255);
                    CopyHeadToWorkTile(kTileRamsesNormal);
                }

                return 1;
            }
        }
        else
        {
            FixPalette();

            if (!nPalDiff)
            {
                nFreeze = 2;
                nHeadStage++;
            }

            return 0;
        }
    }
    else
    {
        if (lNextStateChange <= (int)totalclock)
        {
            if (nPupData)
            {
                short nPupVal = *pPupData;
                pPupData++;
                nPupData -= 2;

                if (nPupData > 0)
                {
                    lNextStateChange = (nPupVal + lHeadStartClock) - 10;
                    nTalkTime = !nTalkTime;
                }
                else
                {
                    nTalkTime = 0;
                    nPupData = 0;
                }
            }
            else if (nKexSpeaking)
            {
                // The remaster's hub: Ramses finishes and play resumes.
                if (!CDplaying())
                    Kex_RamsesEnd();
            }
            else if (!bSubTitles)
            {
                if (!CDplaying())
                {
                    levelnew = levelnum + 1;
                    fadecdaudio();
                }
            }
        }

        word_964E8--;
        if (word_964E8 <= 0)
        {
            word_964EA = RandomBit() * 2;
            word_964E8 = RandomSize(5) + 4;
        }

        int ebx = 592;
        word_964EC--;

        if (word_964EC < 3)
        {
            ebx = 593;
            if (word_964EC <= 0) {
                word_964EC = RandomSize(6) + 4;
            }
        }

        ebx += word_964EA;

        if (!waloff[ebx]) tileLoad(ebx);

        uint8_t* pDest = (uint8_t*)&worktile[10441];
        uint8_t* pSrc = (uint8_t*)waloff[ebx];

        for (int i = 0; i < kSpiritY; i++)
        {
            memcpy(pDest, pSrc, kSpiritX);

            pDest += 212;
            pSrc += kSpiritX;
        }

        if (nTalkTime)
        {
            if (nMouthTile < 2) {
                nMouthTile++;
            }
        }
        else if (nMouthTile != 0)
        {
            nMouthTile--;
        }

        if (nMouthTile)
        {
            short nTile = nMouthTile + 598;

            if (!waloff[nTile]) tileLoad(nTile);

            short nTileSizeX = tilesiz[nTile].x;
            short nTileSizeY = tilesiz[nTile].y;

            uint8_t* pDest = (uint8_t*)&worktile[212 * (kSpiritY - nTileSizeX / 2)] + (159 - nTileSizeY);
            uint8_t* pSrc = (uint8_t*)waloff[nTile];

            while (nTileSizeX > 0)
            {
                memcpy(pDest, pSrc, nTileSizeY);

                nTileSizeX--;
                pDest += 212;
                pSrc += nTileSizeY;
            }
        }

        return 1;
    }

    return 0;
}
