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
#include "move.h"
#include "kexbreak.h"
#include "kexinv.h"
#include "kextable.h"
#include "init.h"
#include "input.h"
#include <math.h>


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
// The remaster's Ramses. Its .kpf carries the complete 55-frame materialise,
// twelve manually selected talk poses, 21-frame vanish and TOMBEND resting
// body, plus one 4-bit facial state per 1/30 second of each speech. Converted
// KEX installs use those assets and the retail level-script timings directly.
// The original pixel-head/PUP machinery below remains the fallback and the
// unchanged presentation for DOS maps.
// ---------------------------------------------------------------------------
int kex_language = 0;      // cvar: English, French, German, Spanish, Italian

int KexLanguage_Index()
{
    return clamp(kex_language, 0, 4);
}

int KexLanguage_VoiceIndex()
{
    // The KPF has Italian text only. Its remaster presentation falls back to
    // English because there are no Italian Ramses scripts or movie files.
    int const language = KexLanguage_Index();
    return language == 4 ? 0 : language;
}

char const *KexLanguage_Code()
{
    static char const * const codes[] = { "en", "fr", "de", "es", "it" };
    return codes[KexLanguage_Index()];
}

static short sKexPup[1200];
static uint8_t sKexTalkData[8192];
static int sKexTalkCount;
static uint8_t sKexWasHidden[(kMaxSprites + 7) >> 3];
static int nKexSpeaking = 0;

enum KexRamsesPhase
{
    kKexRamsesNone,
    kKexRamsesPrelude,
    kKexRamsesSpawn,
    kKexRamsesTalk,
    kKexRamsesVanish,
};

enum { kKexRamsesSpawnAnim, kKexRamsesTalkAnim, kKexRamsesVanishAnim,
       kKexRamsesBodyAnim, kKexRamsesAnimCount };

struct KexRamsesAnim
{
    int first, count, repeat, ticks;
};

static KexRamsesAnim s_kexRamsesAnim[kKexRamsesAnimCount];
static int s_kexRamsesArtLoaded = -1;
static int s_kexRamsesNative;
static int s_kexRamsesPhase;
static int s_kexRamsesFrame;
static int s_kexRamsesNextFrame;
static int s_kexRamsesPhaseClock;
static int s_kexRamsesSpawnClock;
static int s_kexRamsesBodyMoveClock;
static int s_kexRamsesTalkClock;
static int s_kexRamsesEndClock;
static int s_kexRamsesSkipClock;
static int s_kexRamsesRearmClock;
static int s_kexRamsesPadSector = -1;
static uint8_t s_kexRamsesPadSet[kMaxSectors];
static int s_kexRamsesPadCount;
static int s_kexRamsesBaseZ;
static int s_kexRamsesTalkMask;
static int s_kexRamsesBobTicks;
static int s_kexRamsesSeenPhases;
static int s_kexRamsesSawBody;
static int s_kexRamsesTestStarted;
// TOMBEND's EndRamsesEvent starts $script 2 one second after the good
// address. That script starts $script 3, whose only persistent work is a
// random, repeating quake. Keep its time in totalclock's 120-Hz units so the
// source's seconds-based delay remains independent of the 30-Hz Build tick.
static int s_kexTombEndAftermathStartClock;
static int s_kexTombEndAftermathNextClock;
static int s_kexTombEndAftermathPulses;
static int s_kexTombEndAftermathTest;

static bool Kex_RamsesArtParse(char *line, int)
{
    char phase[16];
    int first, count, repeat, ticks;
    if (Bsscanf(line, "%15s %d %d %d %d", phase, &first, &count, &repeat, &ticks) != 5
        || first <= 0 || count <= 0 || repeat <= 0 || ticks < 0)
        return false;

    int which = -1;
    if (!Bstrcmp(phase, "spawn")) which = kKexRamsesSpawnAnim;
    else if (!Bstrcmp(phase, "talk")) which = kKexRamsesTalkAnim;
    else if (!Bstrcmp(phase, "vanish")) which = kKexRamsesVanishAnim;
    else if (!Bstrcmp(phase, "body")) which = kKexRamsesBodyAnim;
    if (which < 0)
        return false;
    s_kexRamsesAnim[which] = { first, count, repeat, ticks };
    return true;
}

static int Kex_RamsesLoadArt(void)
{
    if (s_kexRamsesArtLoaded >= 0)
        return s_kexRamsesArtLoaded;
    Bmemset(s_kexRamsesAnim, 0, sizeof(s_kexRamsesAnim));
    KexTable_Load("kexramses.txt", kKexRamsesAnimCount, Kex_RamsesArtParse);
    s_kexRamsesArtLoaded = 1;
    for (auto const &a : s_kexRamsesAnim)
        s_kexRamsesArtLoaded &= a.first > 0 && a.count > 0;
    return s_kexRamsesArtLoaded;
}

// TOMB_script.txt's script 2 lerps the player to (0, 2816, -864) in Kex
// coordinates while turning toward ramsesOrigin (-13, 3143, -734). The map
// converter uses WORLD=6, flips Y, and stores Kex Z as -z*16*WORLD, so these
// are the corresponding Build positions. Level scripts restart on the same
// 30 Hz simulation cadence as Exhumed; the authored interpolation is 1/16 per
// restart. Visual animation itself is selected at render time below.
#define KEX_TOMB_CAMERA_X 0
#define KEX_TOMB_CAMERA_Y (-2816 * 6)
#define KEX_TOMB_CAMERA_Z (864 * 16 * 6)
#define KEX_TOMB_RAMSES_X (-13 * 6)
#define KEX_TOMB_RAMSES_Y (-3143 * 6)
#define KEX_TOMB_RAMSES_Z (734 * 16 * 6)
#define KEX_TOMBEND_CAMERA_X 0
#define KEX_TOMBEND_CAMERA_Y (-2816 * 6)
#define KEX_TOMBEND_CAMERA_Z (224 * 16 * 6)
#define KEX_TOMBEND_RAMSES_X 0
#define KEX_TOMBEND_RAMSES_Y (-3136 * 6)
#define KEX_TOMBEND_RAMSES_Z (64 * 16 * 6)
static int s_kexCameraMove;

static int Kex_IsTomb(void)
{
    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    return Bstrstr(nameUp, "TOMB.MAP") != NULL && Bstrstr(nameUp, "TOMBEND") == NULL;
}

static int Kex_IsTombEnd(void)
{
    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    return Bstrstr(nameUp, "TOMBEND") != NULL;
}

static void Kex_RamsesTombEndAftermathTick(void)
{
    if (!g_kexMode || !s_kexTombEndAftermathStartClock || !Kex_IsTombEnd()
        || (int)totalclock < s_kexTombEndAftermathStartClock)
        return;

    if (!s_kexTombEndAftermathNextClock)
        s_kexTombEndAftermathNextClock = (int)totalclock;
    if ((int)totalclock < s_kexTombEndAftermathNextClock)
        return;

    // TOMBEND $script 3: SetShakeTime(Math::RandByte() & 0x3F), then
    // quake.wav, delay(Randomizer::Float() * 2.13 + .3333), and restart.
    // The initial start is delayed one second by $script 2. KEX's script
    // clock is 120 Hz here: .3333 * 120 truncates to 40, while the randomized
    // 2.13-second span contributes 0..255 more ticks.
    int const shake = (uint8_t)RandomByte() & 0x3f;
    KexQuake(shake);
    int const quake = KexSound_Get("k_quake");
    if (quake >= 0)
        PlayLocalSound(quake, 0);
    int const delay = 40 + KexRandomMax(256);
    s_kexTombEndAftermathNextClock = (int)totalclock + delay;
    s_kexTombEndAftermathPulses++;

    if (s_kexTombEndAftermathTest && s_kexTombEndAftermathPulses == 2)
    {
        OSD_Printf("Kex TOMBEND aftermath test: PASS (script 2 delay, "
                   "script 3 random shake 0..63, delay 40..295)\n");
        s_kexTombEndAftermathTest = 0;
    }
}

static void Kex_RamsesCameraTick(void)
{
    if (!s_kexCameraMove)
        return;
    int const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
    if ((unsigned)nPlayerSprite >= (unsigned)kMaxSprites)
        return;

    int const tombEnd = Kex_IsTombEnd();
    int const cameraX = tombEnd ? KEX_TOMBEND_CAMERA_X : KEX_TOMB_CAMERA_X;
    int const cameraY = tombEnd ? KEX_TOMBEND_CAMERA_Y : KEX_TOMB_CAMERA_Y;
    int const cameraZ = tombEnd ? KEX_TOMBEND_CAMERA_Z : KEX_TOMB_CAMERA_Z;
    int const ramsesX = tombEnd ? KEX_TOMBEND_RAMSES_X : KEX_TOMB_RAMSES_X;
    int const ramsesY = tombEnd ? KEX_TOMBEND_RAMSES_Y : KEX_TOMB_RAMSES_Y;
    int const ramsesZ = tombEnd ? KEX_TOMBEND_RAMSES_Z : KEX_TOMB_RAMSES_Z;

    auto &pl = PlayerList[nLocalPlayer];
    auto &p = sprite[nPlayerSprite];
    pl.opos = p.xyz;
    pl.q16oangle = pl.q16angle;
    pl.q16ohoriz = pl.q16horiz;

    p.x += (cameraX - p.x) / 16;
    p.y += (cameraY - p.y) / 16;
    p.z += (cameraZ - p.z) / 16;
    if (abs(cameraX - p.x) <= 15 && abs(cameraY - p.y) <= 15
        && abs(cameraZ - p.z) <= 15)
    {
        p.x = cameraX;
        p.y = cameraY;
        p.z = cameraZ;
    }

    int16_t nSector = p.sectnum;
    updatesectorz(p.x, p.y, p.z, &nSector);
    if (nSector >= 0)
    {
        if (p.sectnum != nSector)
            mychangespritesect(nPlayerSprite, nSector);
        nPlayerViewSect[nLocalPlayer] = nSector;
    }

    int const targetAngle = getangle(ramsesX - p.x, ramsesY - p.y);
    int angle = fix16_to_int(pl.q16angle) & kAngleMask;
    int angleDelta = ((targetAngle - angle + 1024) & kAngleMask) - 1024;
    if (abs(angleDelta) < 16)
        angle = targetAngle;
    else
        angle = (angle + angleDelta / 16) & kAngleMask;
    pl.q16angle = fix16_from_int(angle);
    p.ang = angle;

    // Build z has sixteen sub-units per horizontal world unit. Aim from the
    // rendered eye, not the player's floor-aligned sprite origin.
    int const dx = ramsesX - p.x;
    int const dy = ramsesY - p.y;
    int const distance = max(1, Blrintf(sqrtf((float)dx * dx + (float)dy * dy)));
    int desiredHoriz = 92;
    if (!(tombEnd && s_kexRamsesPhase == kKexRamsesPrelude))
        desiredHoriz = clamp(92 + (8 * (ramsesZ - (p.z + eyelevel[nLocalPlayer]))) / distance,
                             0, 184);
    fix16_t const desired = fix16_from_int(desiredHoriz);
    fix16_t const horizDelta = desired - pl.q16horiz;
    pl.q16horiz = abs(horizDelta) < F16(1) ? desired : pl.q16horiz + horizDelta / 16;
    nVertPan[nLocalPlayer] = pl.q16horiz;
}

// Which speech: transcribed from GetRamsesDialog() in the remaster's
// scripts/map/TOMB_script.txt (talk event N is SP_xxNN+1). The artifacts rank
// the player's progress through the game, and three of the stages split on a
// weapon; the two "alternate" speeches are for coming back to Ramses after he
// has already spoken once (script 3 re-arms him with bPlayAlternateDialog).
// TOMBEND has two: 14 with the transmitter assembled, 13 without.
static int s_kexRamsesSpokenBefore;

static int Kex_RamsesSpeech(void)
{
    char nameUp[BMAX_PATH];
    Bstrncpyz(nameUp, gUserMapFilename, sizeof(nameUp));
    Bstrupr(nameUp);
    if (Bstrstr(nameUp, "TOMBEND"))
        return KexInv_TransCount(0) >= KEX_TRANS_PIECES ? 14 : 13;

    int const alt = s_kexRamsesSpokenBefore;
    s_kexRamsesSpokenBefore = 1;
    uint16_t const w = nPlayerWeapons[0];
    if (KexInv_HasArtifact(0, kKexArtiFeather)) return alt ? 12 : 11;
    if (KexInv_HasArtifact(0, kKexArtiScepter)) return 10;
    if (KexInv_HasArtifact(0, kKexArtiAnklets)) return (w & (1 << kWeaponStaff))   ? 9 : 8;
    if (KexInv_HasArtifact(0, kKexArtiShawl))   return (w & (1 << kWeaponFlamer))  ? 7 : 6;
    if (KexInv_HasArtifact(0, kKexArtiMask))    return (w & (1 << kWeaponGrenade)) ? 5 : 4;
    if (KexInv_HasArtifact(0, kKexArtiSandals)) return 3;
    return alt ? 2 : 1;
}

static int Kex_RamsesDuration(int nSpeech)
{
    // GetRamsesDialog() and TOMBEND script 9. These deliberately outlive a
    // few of the wavs, so CDplaying() is not the event timer.
    static uint8_t const seconds[] = {
        0, 120, 20, 38, 34, 31, 28, 30, 24, 22, 28, 30, 30, 47, 43,
    };
    return (unsigned)nSpeech < ARRAY_SIZE(seconds) ? seconds[nSpeech] : 30;
}

static int Kex_RamsesTalkFrame(int frame)
{
    int talkFrame = 0;
    if (frame & 2) talkFrame += 6;
    if (frame & 1) talkFrame += 3;
    if (frame & 4) talkFrame += 1;
    else if (frame & 8) talkFrame += 2;
    return talkFrame;
}

static int Kex_RamsesStart(void)
{
    static char const * const langs[] = { "ENG", "FRE", "GER", "SPA" };
    char const *lang = langs[KexLanguage_VoiceIndex()];
    int const nSpeech = Kex_RamsesSpeech();

    char path[80];
    Bsprintf(path, "scripts/ramses/SP_%s%02d.txt", lang, nSpeech);
    int const fil = kopen4loadfrommod(path, 0);
    if (fil == -1)
        return 0;
    int const len = kfilelength(fil);
    char *buf = (char *)Xmalloc(len + 1);
    kread(fil, buf, len); buf[len] = 0; kclose(fil);

    // Parse only the array body. Scanning the declaration too consumes the
    // speech number in g_SP_ENG01 as sample zero and shifts every expression.
    char *body = Bstrchr(buf, '{');
    char *bodyEnd = Bstrrchr(buf, '}');
    if (!body || !bodyEnd || bodyEnd <= body)
    {
        Xfree(buf);
        return 0;
    }
    *bodyEnd = 0;

    // Keep all four authored facial bits for the native twelve-pose actor and
    // also derive mouth edges for the procedural fallback.
    int prev = 0, n = 0, idx = 0;
    s_kexRamsesTalkMask = 0;
    for (char *p = body + 1; *p; )
    {
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) break;
        int const v = (int)strtol(p, &p, 10);
        if ((unsigned)v > 15)
            continue;
        if (idx < (int)ARRAY_SIZE(sKexTalkData))
        {
            sKexTalkData[idx] = (uint8_t)v;
            s_kexRamsesTalkMask |= 1 << Kex_RamsesTalkFrame(v);
        }
        if (((v ^ prev) & 8) && n < (int)ARRAY_SIZE(sKexPup))
            sKexPup[n++] = (short)(idx << 2);
        prev = v;
        idx++;
    }
    Xfree(buf);
    sKexTalkCount = min(idx, (int)ARRAY_SIZE(sKexTalkData));
    if (sKexTalkCount == 0)
        return 0;

    Bsprintf(path, "ramses/SP_%s%02d.wav", lang, nSpeech);
    playMusicFile(path, false, false);

    pPupData = sKexPup;
    nPupData = (short)(n * 2);
    bSubTitles = kFalse;       // nothing to caption: the kpf ships no transcripts
    nKexSpeaking = 1;
    s_kexCameraMove = Kex_IsTomb() || Kex_IsTombEnd();
    s_kexRamsesTalkClock = (int)totalclock;
    s_kexRamsesEndClock = s_kexRamsesTalkClock + Kex_RamsesDuration(nSpeech) * 120;
    s_kexRamsesSkipClock = s_kexRamsesTalkClock + 4 * 120;
    if (Bgetenv("NNB_KEXRAMSES_VISUAL_TEST"))
        s_kexRamsesEndClock = min(s_kexRamsesEndClock, s_kexRamsesTalkClock + 3 * 120);
    OSD_Printf("Kex Ramses: speech %d, %d facial samples, %d scripted seconds\n",
               nSpeech, sKexTalkCount, Kex_RamsesDuration(nSpeech));
    return 1;
}

static void Kex_RamsesSetAnim(int which, int frame)
{
    if ((unsigned)nSpiritSprite >= (unsigned)kMaxSprites
        || (unsigned)which >= (unsigned)kKexRamsesAnimCount)
        return;
    KexRamsesAnim const &anim = s_kexRamsesAnim[which];
    if (anim.count <= 0)
        return;
    frame = clamp(frame, 0, anim.count - 1);
    auto &sp = sprite[nSpiritSprite];
    sp.picnum = (int16_t)(anim.first + frame);
    sp.xrepeat = sp.yrepeat = (uint8_t)anim.repeat;
    sp.shade = -96;
    sp.cstat |= 0x82;                 // translucent, centred on the actor origin
    sp.cstat &= (int16_t)~0x8000;
}

static void Kex_RamsesBeginAnim(int phase, int which)
{
    s_kexRamsesPhase = phase;
    s_kexRamsesSeenPhases |= 1 << phase;
    s_kexRamsesFrame = 0;
    s_kexRamsesPhaseClock = (int)totalclock;
    s_kexRamsesNextFrame = s_kexRamsesPhaseClock
                          + max(1, s_kexRamsesAnim[which].ticks);
    Kex_RamsesSetAnim(which, 0);
}

void KexRamses_Reset(void)
{
    nKexSpeaking = 0;
    s_kexCameraMove = 0;
    s_kexRamsesNative = 0;
    s_kexRamsesPhase = kKexRamsesNone;
    s_kexRamsesFrame = 0;
    s_kexRamsesNextFrame = 0;
    s_kexRamsesPhaseClock = 0;
    s_kexRamsesSpawnClock = 0;
    s_kexRamsesBodyMoveClock = 0;
    s_kexRamsesTalkClock = 0;
    s_kexRamsesEndClock = 0;
    s_kexRamsesSkipClock = 0;
    s_kexRamsesRearmClock = 0;
    s_kexRamsesPadSector = -1;
    Bmemset(s_kexRamsesPadSet, 0, sizeof(s_kexRamsesPadSet));
    s_kexRamsesPadCount = 0;
    s_kexRamsesBaseZ = 0;
    s_kexRamsesTalkMask = 0;
    s_kexRamsesBobTicks = 0;
    s_kexRamsesSeenPhases = 0;
    s_kexRamsesSawBody = 0;
    s_kexRamsesTestStarted = 0;
    s_kexRamsesSpokenBefore = 0;
    s_kexTombEndAftermathStartClock = 0;
    s_kexTombEndAftermathNextClock = 0;
    s_kexTombEndAftermathPulses = 0;
    s_kexTombEndAftermathTest = Bgetenv("NNB_KEX_TOMBEND_AFTER_TEST") != NULL;
    sKexTalkCount = 0;
}

static int Kex_RamsesNativeInit(void)
{
    if (!g_kexMode || (!Kex_IsTomb() && !Kex_IsTombEnd())
        || (unsigned)nSpiritSprite >= (unsigned)kMaxSprites || !Kex_RamsesLoadArt())
        return 0;

    int const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
    if ((unsigned)nPlayerSprite >= (unsigned)kMaxSprites)
        return 0;

    auto &sp = sprite[nSpiritSprite];
    nSpiritRepeatX = sp.xrepeat;
    nSpiritRepeatY = sp.yrepeat;
    s_kexRamsesBaseZ = sp.z;
    s_kexRamsesPadSector = sprite[nPlayerSprite].sectnum;
    Bmemset(s_kexRamsesPadSet, 0, sizeof(s_kexRamsesPadSet));
    s_kexRamsesPadCount = 0;
    for (int i = 0; i < numsectors; i++)
    {
        if (SectFlag[i] & 0x8000)
        {
            s_kexRamsesPadSet[i] = 1;
            s_kexRamsesPadCount++;
        }
    }
    if (!s_kexRamsesPadCount && (unsigned)s_kexRamsesPadSector < (unsigned)numsectors)
    {
        s_kexRamsesPadSet[s_kexRamsesPadSector] = 1;
        s_kexRamsesPadCount = 1;
    }
    s_kexRamsesRearmClock = 0;
    s_kexRamsesTalkMask = 0;
    s_kexRamsesBobTicks = 0;
    s_kexRamsesSeenPhases = 1 << kKexRamsesPrelude;
    s_kexRamsesSawBody = 0;
    s_kexRamsesNative = 1;
    s_kexRamsesPhase = kKexRamsesPrelude;
    s_kexRamsesPhaseClock = (int)totalclock;
    s_kexCameraMove = 1;
    nKexSpeaking = 0;
    nHeadStage = 0;

    StopCD();
    KexScripted_Trigger(0);            // TOMB opens the casket at once
    if (Kex_IsTombEnd())
    {
        // Body starts at ramsesOrigin.z + 512. Script 6 begins after 0.5s,
        // subtracting two KEX z units per 30 Hz restart until the 7s spawn.
        sp.z = s_kexRamsesBaseZ - 352 * 16 * 6;
        Kex_RamsesSetAnim(kKexRamsesBodyAnim, 0);
        s_kexRamsesSawBody = 1;
        s_kexRamsesBodyMoveClock = (int)totalclock + 60;
        s_kexRamsesSpawnClock = (int)totalclock + 7 * 120;
    }
    else
    {
        sp.cstat |= 0x8000;
        s_kexRamsesBodyMoveClock = 0;
        s_kexRamsesSpawnClock = (int)totalclock + 60; // delay(0.5f)
    }

    OSD_Printf("Kex Ramses: authored %s prelude started\n",
               Kex_IsTombEnd() ? "TOMBEND body" : "TOMB casket");
    return 1;
}

// The Build game ends the level when Ramses finishes -- his speech IS its
// intro cinematic. In the remaster's hub he vanishes and play resumes, except
// that TOMBEND immediately takes the incomplete-transmitter branch into the
// bad ending. This puts back everything InitSpiritHead() took: the other
// sprites, the player's freeze, the one-shot pad flag, and the level's music.
static void Kex_RamsesEnd(void)
{
    OSD_Printf("Kex Ramses: speech over, play resumes\n");
    nKexSpeaking = 0;
    s_kexCameraMove = 0;
    KexScript_RamsesEnd();      // TOMB's crypt door

    if (s_kexRamsesNative)
    {
        int const visualTest = Bgetenv("NNB_KEXRAMSES_VISUAL_TEST") != NULL;
        int const incompleteEnding = Kex_IsTombEnd()
                                  && KexInv_TransCount(0) < KEX_TRANS_PIECES
                                  && !visualTest;

        for (int i = 0; i < numsectors; i++)
            if (s_kexRamsesPadSet[i])
                SectFlag[i] &= 0x7FFF;
        nFreeze = 0;
        StopCD();
        StopAllSounds();
        StopLocalSound();

        // TOMBEND's incomplete branch calls EndGame before ChangeAnim.
        if (incompleteEnding)
        {
            sprite[nSpiritSprite].cstat |= 0x8000;
            sprite[nSpiritSprite].xrepeat = (uint8_t)nSpiritRepeatX;
            sprite[nSpiritSprite].yrepeat = (uint8_t)nSpiritRepeatY;
            sprite[nSpiritSprite].z = s_kexRamsesBaseZ;
            s_kexRamsesNative = 0;
            s_kexRamsesPhase = kKexRamsesNone;
            OSD_Printf("Kex Ramses: transmitter incomplete, starting bad ending\n");
            FinishLevel();
            return;
        }

        if (Kex_IsTombEnd())
        {
            KexScripted_Trigger(1);     // sink the bier on the good ending
            // EndRamsesEvent(): the immediate SetShakeTime(32) precedes
            // its one-second CallDelayed(2). $script 2 then enters the
            // recurring $script 3 earthquake below. This is deliberately
            // KEX-only rather than the DOS nQuake amplitude path.
            KexQuake(32);
            int const quake = KexSound_Get("k_quake");
            if (quake >= 0)
                PlayLocalSound(quake, 0);
            s_kexTombEndAftermathStartClock = (int)totalclock + 120;
            s_kexTombEndAftermathNextClock = 0;
            s_kexTombEndAftermathPulses = 0;
        }
        else
        {
            // The repeatable KEX event rearms four seconds later. Build's
            // touch flag must wait until the player has actually left the pad,
            // otherwise standing still starts another speech immediately.
            s_kexRamsesRearmClock = (int)totalclock + 4 * 120;
        }

        Kex_RamsesBeginAnim(kKexRamsesVanish, kKexRamsesVanishAnim);
        char const *track = Kex_MusicForMap(gUserMapFilename);
        if (track)
            playMusicFile(track, true);
        return;
    }

    // Procedural KEX fallback: retain the old all-sprite restoration path.
    // TOMBEND's EndRamsesEvent sinks the bier, on the good ending only.
    if (KexInv_TransCount(0) >= KEX_TRANS_PIECES)
        KexScripted_Trigger(1);

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
    if (Kex_IsTombEnd() && KexInv_TransCount(0) < KEX_TRANS_PIECES)
    {
        OSD_Printf("Kex Ramses: transmitter incomplete, starting bad ending\n");
        FinishLevel();
        return;
    }
    char const *track = Kex_MusicForMap(gUserMapFilename);
    if (track)
        playMusicFile(track, true);
}

static int Kex_RamsesAdvanceAnim(int which)
{
    KexRamsesAnim const &anim = s_kexRamsesAnim[which];
    int const ticks = max(1, anim.ticks);
    while ((int)totalclock >= s_kexRamsesNextFrame)
    {
        s_kexRamsesNextFrame += ticks;
        if (++s_kexRamsesFrame >= anim.count)
            return 1;
        Kex_RamsesSetAnim(which, s_kexRamsesFrame);
    }
    return 0;
}

static void Kex_RamsesFinishNative(void)
{
    int const wasTombEnd = Kex_IsTombEnd();
    auto &sp = sprite[nSpiritSprite];
    sp.cstat |= 0x8000;
    sp.xrepeat = (uint8_t)nSpiritRepeatX;
    sp.yrepeat = (uint8_t)nSpiritRepeatY;
    sp.z = s_kexRamsesBaseZ;
    s_kexRamsesPhase = kKexRamsesNone;
    s_kexRamsesNative = 0;

    if (Bgetenv("NNB_KEXRAMSES_VISUAL_TEST"))
    {
        int const expectedPhases = (1 << kKexRamsesPrelude) | (1 << kKexRamsesSpawn)
                                 | (1 << kKexRamsesTalk) | (1 << kKexRamsesVanish);
        int const bad = s_kexRamsesAnim[kKexRamsesSpawnAnim].count != 55
                     || s_kexRamsesAnim[kKexRamsesTalkAnim].count != 12
                     || s_kexRamsesAnim[kKexRamsesVanishAnim].count != 21
                     || s_kexRamsesAnim[kKexRamsesBodyAnim].count != 1
                     || !s_kexRamsesTalkMask || (s_kexRamsesTalkMask & ~0xFFF)
                     || sKexTalkCount <= 0
                     || s_kexRamsesSeenPhases != expectedPhases
                     || s_kexRamsesSawBody != wasTombEnd || nFreeze != 0;
        OSD_Printf("Kex Ramses visual test: %s (spawn %d, talk %d/mask %03x, "
                   "vanish %d, body %d/%d, samples %d)\n",
                   bad ? "FAIL" : "PASS",
                   s_kexRamsesAnim[kKexRamsesSpawnAnim].count,
                   s_kexRamsesAnim[kKexRamsesTalkAnim].count,
                   s_kexRamsesTalkMask,
                   s_kexRamsesAnim[kKexRamsesVanishAnim].count,
                   s_kexRamsesAnim[kKexRamsesBodyAnim].count,
                   s_kexRamsesSawBody, sKexTalkCount);
    }
}

int KexRamses_Tick(void)
{
    Kex_RamsesTombEndAftermathTick();

    // A repeatable KEX sector fires on entry, not every frame spent inside it.
    // Rearm only after both the authored four seconds and a real departure.
    if (g_kexMode && s_kexRamsesRearmClock
        && (int)totalclock >= s_kexRamsesRearmClock
        && s_kexRamsesPadCount > 0)
    {
        int const nPlayerSprite = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayerSprite < (unsigned)kMaxSprites
            && ((unsigned)sprite[nPlayerSprite].sectnum >= (unsigned)numsectors
                || !s_kexRamsesPadSet[sprite[nPlayerSprite].sectnum]))
        {
            for (int i = 0; i < numsectors; i++)
                if (s_kexRamsesPadSet[i])
                    SectFlag[i] |= 0x8000;
            s_kexRamsesRearmClock = 0;
            OSD_Printf("Kex Ramses: repeatable pad rearmed (%d sectors)\n",
                       s_kexRamsesPadCount);
        }
    }

    // Unattended presentation probe. It starts the real cinematic after the
    // map has settled; its dialogue timer is shortened in Kex_RamsesStart().
    if (g_kexMode && !s_kexRamsesTestStarted && Bgetenv("NNB_KEXRAMSES_VISUAL_TEST")
        && totalmoves >= 30 && !nFreeze && (Kex_IsTomb() || Kex_IsTombEnd())
        && (unsigned)nSpiritSprite < (unsigned)kMaxSprites)
    {
        s_kexRamsesTestStarted = 1;
        nFreeze = 1;
        StopAllSounds();
        StopLocalSound();
        InitSpiritHead();
        if (!s_kexRamsesNative)
            OSD_Printf("Kex Ramses visual test: FAIL (authored presentation unavailable)\n");
    }

    if (!g_kexMode || !s_kexRamsesNative)
        return 0;

    auto &sp = sprite[nSpiritSprite];
    switch (s_kexRamsesPhase)
    {
        case kKexRamsesPrelude:
            Kex_RamsesCameraTick();
            if (s_kexRamsesBodyMoveClock)
            {
                while ((int)totalclock >= s_kexRamsesBodyMoveClock
                       && s_kexRamsesBodyMoveClock < s_kexRamsesSpawnClock)
                {
                    sp.z += 2 * 16 * 6;
                    s_kexRamsesBodyMoveClock += 4;
                }
            }
            if ((int)totalclock >= s_kexRamsesSpawnClock)
            {
                sp.z = s_kexRamsesBaseZ;
                Kex_RamsesBeginAnim(kKexRamsesSpawn, kKexRamsesSpawnAnim);
                if (!Kex_RamsesStart())
                {
                    OSD_Printf("Kex Ramses: facial script unavailable, aborting presentation\n");
                    sp.cstat |= 0x8000;
                    sp.xrepeat = (uint8_t)nSpiritRepeatX;
                    sp.yrepeat = (uint8_t)nSpiritRepeatY;
                    s_kexRamsesNative = 0;
                    s_kexRamsesPhase = kKexRamsesNone;
                    s_kexCameraMove = 0;
                    nFreeze = 0;
                }
            }
            break;

        case kKexRamsesSpawn:
            Kex_RamsesCameraTick();
            if (Kex_RamsesAdvanceAnim(kKexRamsesSpawnAnim))
            {
                s_kexRamsesBobTicks = 0;
                Kex_RamsesBeginAnim(kKexRamsesTalk, kKexRamsesTalkAnim);
            }
            break;

        case kKexRamsesTalk:
        {
            Kex_RamsesCameraTick();
            int const sample = ((int)totalclock - s_kexRamsesTalkClock) / 4;
            int const talkFrame = (unsigned)sample < (unsigned)sKexTalkCount
                                ? Kex_RamsesTalkFrame(sKexTalkData[sample]) : 0;
            Kex_RamsesSetAnim(kKexRamsesTalkAnim, talkFrame);
            sp.z = s_kexRamsesBaseZ
                 - Blrintf(8.f * 16.f * 6.f * sinf(s_kexRamsesBobTicks * 0.025f));
            s_kexRamsesBobTicks++;

            int const buttons = sPlayerInput[nLocalPlayer].buttons;
            if ((int)totalclock >= s_kexRamsesEndClock
                || ((int)totalclock >= s_kexRamsesSkipClock
                    && (buttons & (kButtonOpen | kButtonFire | kButtonJump))))
                Kex_RamsesEnd();
            break;
        }

        case kKexRamsesVanish:
            if (Kex_RamsesAdvanceAnim(kKexRamsesVanishAnim))
                Kex_RamsesFinishNative();
            break;
    }
    return s_kexRamsesNative;
}

int KexRamses_ViewTile(int nSprite)
{
    if (!s_kexRamsesNative || nSprite != nSpiritSprite)
        return -1;

    int which = -1;
    if (s_kexRamsesPhase == kKexRamsesSpawn)
        which = kKexRamsesSpawnAnim;
    else if (s_kexRamsesPhase == kKexRamsesVanish)
        which = kKexRamsesVanishAnim;
    if (which < 0)
        return sprite[nSprite].picnum;

    KexRamsesAnim const &anim = s_kexRamsesAnim[which];
    int const frame = clamp(((int)totalclock - s_kexRamsesPhaseClock)
                            / max(1, anim.ticks), 0, anim.count - 1);
    return anim.first + frame;
}

void InitSpiritHead()
{
    char filename[20];

    nPixels = 0;

    nSpiritRepeatX = sprite[nSpiritSprite].xrepeat;
    nSpiritRepeatY = sprite[nSpiritSprite].yrepeat;

    if (Kex_RamsesNativeInit())
        return;

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

    if (g_kexMode)
        KexScripted_Trigger(0);
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

    if (s_kexRamsesNative)
        return 1;

    nVertPan[0] += (nDestVertPan[0] - nVertPan[0]) / 4;
    if (nKexSpeaking)
        Kex_RamsesCameraTick();

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
                // The remaster schedules this event from the level script;
                // several wavs are shorter than that authored duration.
                if ((int)totalclock >= s_kexRamsesEndClock)
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
