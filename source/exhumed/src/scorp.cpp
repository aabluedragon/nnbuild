//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 sirlemonhead, Nuke.YKT

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

#include "scorp.h"
#include "engine.h"
#include "runlist.h"
#include "exhumed.h"
#include "move.h"
#include "sequence.h"
#include "sound.h"
#include "random.h"
#include "trigdat.h"
#include "bullet.h"
#include "spider.h"
#include "save.h"
#include "anims.h"
#include "kexbreak.h" // NNBuild: Selkis death event
#include "kexinv.h"
#include "player.h"
#include <assert.h>

/*
    Selkis Boss AI code
*/

#define kMaxScorpions   5

struct Scorpion
{
    short nHealth;
    short nFrame;
    short nAction;
    short nSprite;
    short nTarget;
    short f;
    short g;
    int8_t h;
    int8_t i;
};

static actionSeq ActionSeq[] = {
    {0,  0},
    {8,  0},
    {29, 0},
    {19, 0},
    {45, 1},
    {46, 1},
    {47, 1},
    {48, 1},
    {50, 1},
    {53, 1},
    {37, 0}   // KEX selkis_pain; the classic controller never selected it
};

// Generated KEX SCORP.SEQ groups. The stock table above remains the exact
// classic/demo contract; these slots follow the retail Selkis animations.
static actionSeq KexSelkisActionSeq[] = {
    {0,  0},  // idle
    {9,  0},  // chase
    {27, 0},  // melee
    {18, 0},  // ranged
    {36, 0},  // pain
    {45, 1},  // wounded
    {46, 1},  // malfunction 1
    {47, 1},  // malfunction 2
    {48, 1},  // three-frame death loop
    {49, 1},  // persistent corpse
    {36, 0},  // compatibility pain state from an older KEX save
};

short ScorpCount = -1;

Scorpion scorpion[kMaxScorpions];
short ScorpChan[kMaxScorpions];

static int s_selkisAudit = -1;
static int s_selkisAuditSeen;
static int s_selkisTest = -1;
static int s_selkisTestPhase;
static int s_selkisTestTicks;
static short s_selkisTestScorp = -1;
static short s_selkisTestPlayer = -1;
static short s_selkisTestInvincibility;
static int s_selkisTestOkay;
static int s_selkisTestSpeed;
static int s_selkisTestMeleeMask;
static int s_selkisTestFireFrame;
static int s_selkisTestProjectileOkay;
static int s_selkisTestDeathCallbacks;
static int s_selkisTestSpawnedScorps;
static int s_selkisTestDeathTriggers;
static int s_selkisTestCreatureCounted;
static int s_selkisTestStatsOkay;

static bool KexSelkisSequenceExact()
{
    if (!g_kexMode || !seq_IsLoaded(kSeqScorp))
        return false;
    int const base = SeqOffsets[kSeqScorp];
    // The generated file has exact 1/8/6/9/25 action groups and the retail
    // three-frame explosion loop. The stock 64-slot file cannot match this.
    return SeqSize[base] == 1 && SeqSize[base + 9] == 8
        && SeqSize[base + 18] == 6 && SeqSize[base + 27] == 9
        && SeqSize[base + 36] == 25 && SeqSize[base + 45] == 40
        && SeqSize[base + 46] == 12 && SeqSize[base + 47] == 20
        && SeqSize[base + 48] == 3 && SeqSize[base + 49] == 1;
}

static actionSeq const &ScorpActionSequence(int nAction)
{
    return KexSelkisSequenceExact() ? KexSelkisActionSeq[nAction]
                                    : ActionSeq[nAction];
}

static void KexSelkisPlaySound(short nSprite, char const *name)
{
    int const sound = KexSound_Get(name);
    if (sound >= 0)
        D3PlayFX(sound, nSprite);
}

static void KexSelkisSetVelocity(short nSprite, short nHealth)
{
    if (!g_kexMode)
    {
        sprite[nSprite].xvel = Cos(sprite[nSprite].ang);
        sprite[nSprite].yvel = Sin(sprite[nSprite].ang);
        return;
    }

    // A_SelkisThink sets moveSpeed to (96 - health / 83) / 4.  Convert its
    // 12..24 KEX-unit range to Build's velocity storage. The map converter
    // uses six Build units per remaster XY unit and MoveCreature stores that
    // displacement as a 1/64th velocity vector.
    int const speed = clamp((96 - KexHealthPoints(nHealth) / 83) / 4, 12, 24);
    sprite[nSprite].xvel = (Cos(sprite[nSprite].ang) * (speed * 6)) >> 8;
    sprite[nSprite].yvel = (Sin(sprite[nSprite].ang) * (speed * 6)) >> 8;
}

static void KexSelkisEnterChase(short nScorp, short nSprite)
{
    scorpion[nScorp].nAction = 1;
    scorpion[nScorp].nFrame = 0;
    KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);
}

static bool KexSelkisThink(short nScorp, short nSprite)
{
    KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);

    // scripts/p_enemy.txt RandomDecision(ai, 30): only the matching one of
    // sixteen masked phases proceeds to the two independent byte rolls.
    if ((KexRandomLong() & 30) != (totalmoves & 30))
        return false;

    if (((uint8_t)RandomByte() & 7) == 4)
        KexSelkisPlaySound(nSprite, "scrp_icu");

    if (((uint8_t)RandomByte() & 7) != 3)
        return false;

    int const health = KexHealthPoints(scorpion[nScorp].nHealth);
    int action = -1;
    if (health < 1000)
        action = 7;
    else if (health < 2000)
        action = 6;
    else if (health < 3000)
        action = 5;
    if (action < 0)
        return false;

    scorpion[nScorp].nAction = action;
    scorpion[nScorp].nFrame = 0;
    sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
    return true;
}

static bool KexSelkisFacesOnFrame(int nAction, int nFrame)
{
    switch (nAction)
    {
        case 4: // selkis_pain: every reachable frame before the terminal goto
            return nFrame < 24;
        case 5: // selkis_wounded
            switch (nFrame)
            {
                case 0: case 3: case 5: case 8: case 10: case 14:
                case 18: case 20: case 22: case 23: case 25: case 27:
                case 29: case 31: case 33: case 35: case 38:
                    return true;
                default:
                    return false;
            }
        case 6: // selkis_malfunction1
            return nFrame == 0 || nFrame == 1 || nFrame == 4
                || nFrame == 6 || nFrame == 9;
        case 7: // selkis_malfunction2
            return nFrame < 19;
        default:
            return false;
    }
}

static int KexSelkisSignedRandom(int scale)
{
    return ((int16_t)KexRandomWord() * scale) / 32768;
}

static int KexSelkisUnsignedRandom(int scale)
{
    return ((uint32_t)KexRandomWord() * scale) / 65535;
}

static void KexSelkisSpawnScorpion(short nScorp, short nSprite)
{
    // A_SelkisCountDown draws these in script order: velocity first, then
    // spawn offset. KEX Z is positive-up, hence the signs in Build space.
    int const xvel = KexSelkisSignedRandom(10 * 6 * 64);
    int const yvel = KexSelkisSignedRandom(10 * 6 * 64);
    int const zvel = -(10 * 16 * 6 + KexSelkisUnsignedRandom(10 * 16 * 6));
    int const x = sprite[nSprite].x + KexSelkisSignedRandom(40 * 6);
    int const y = sprite[nSprite].y + KexSelkisSignedRandom(40 * 6);
    int const z = sprite[nSprite].z - KexSelkisUnsignedRandom(80 * 16 * 6);

    if (s_selkisTest >= 0 && nScorp == s_selkisTestScorp)
    {
        s_selkisTestSpawnedScorps++;
        return;
    }

    int const nSpider = BuildSpider(-1, x, y, z, sprite[nSprite].sectnum,
                                    sprite[nSprite].ang, 1);
    if (nSpider < 0)
        return;
    int const nSpiderSprite = GetSpiderSprite(nSpider);
    sprite[nSpiderSprite].xvel = xvel;
    sprite[nSpiderSprite].yvel = yvel;
    sprite[nSpiderSprite].zvel = zvel;
}

static void KexSelkisSpawnExplosion(short nScorp, short nSprite)
{
    // The chance byte is consumed on every non-terminal countdown callback.
    if (((uint8_t)RandomByte() & 1) == 0)
        return;
    int const x = sprite[nSprite].x + KexSelkisSignedRandom(128 * 6);
    int const y = sprite[nSprite].y + KexSelkisSignedRandom(128 * 6);
    int const z = sprite[nSprite].z
        - KexSelkisSignedRandom(128 * 16 * 6);
    if (s_selkisTest >= 0 && nScorp == s_selkisTestScorp)
        return;
    if (KexExplosion01_Spawn(x, y, z, sprite[nSprite].sectnum,
                             192, 0) == -1)
        BuildAnim(-1, 36, 0, x, y, z - 192 * 16 * 6,
                  sprite[nSprite].sectnum, 160, 4);
}

static void KexSelkisCountDown(short nScorp, short nSprite)
{
    if (s_selkisTest >= 0 && nScorp == s_selkisTestScorp)
        s_selkisTestDeathCallbacks++;

    if (--scorpion[nScorp].g <= 0)
    {
        KexSelkisPlaySound(nSprite, "scrp_cry");
        if (s_selkisTest >= 0 && nScorp == s_selkisTestScorp)
            s_selkisTestDeathTriggers++;
        else
        {
            // A_SelkisCountDown fires the arena exit when the final
            // explosion becomes the persistent Selkis corpse.
            if (g_kexMode)
                KexScript_BossEvent(nSprite, "Selkis death event");
            runlist_ChangeChannel(ScorpChan[nScorp], 1);
        }
        scorpion[nScorp].nAction = 9;
        scorpion[nScorp].nFrame = 0;
        return;
    }

    if (scorpion[nScorp].g & 1)
        KexSelkisSpawnScorpion(nScorp, nSprite);
    KexSelkisSpawnExplosion(nScorp, nSprite);
}

static void KexSelkisAudit(short nScorp)
{
    if (s_selkisAudit < 0)
        s_selkisAudit = g_kexMode && Bgetenv("NNB_KEX_SELKIS_AUDIT") ? 1 : 0;
    if (!s_selkisAudit || s_selkisAuditSeen)
        return;
    s_selkisAuditSeen = 1;
    short const nSprite = scorpion[nScorp].nSprite;
    OSD_Printf("Kex Selkis: sprite %d hp %d clip %d repeat %d speed %d exact %d\n",
               nSprite, KexHealthPoints(scorpion[nScorp].nHealth),
               (int)sprite[nSprite].clipdist, (int)sprite[nSprite].xrepeat,
               clamp((96 - KexHealthPoints(scorpion[nScorp].nHealth) / 83) / 4,
                     12, 24), KexSelkisSequenceExact() ? 1 : 0);
}


void InitScorp()
{
    ScorpCount = kMaxScorpions;
    s_selkisAudit = -1;
    s_selkisAuditSeen = 0;
    s_selkisTest = -1;
    s_selkisTestScorp = -1;
}

int BuildScorp(short nSprite, int x, int y, int z, short nSector, short nAngle, int nChannel)
{
    if (ScorpCount <= 0)
        return -1;
    short const nScorp = --ScorpCount;

    if (nSprite == -1)
    {
        nSprite = insertsprite(nSector, 122);
    }
    else
    {
        changespritestat(nSprite, 122);

        x = sprite[nSprite].x;
        y = sprite[nSprite].y;
        z = sector[sprite[nSprite].sectnum].floorz;
        nAngle = sprite[nSprite].ang;
    }

    assert(nSprite >= 0 && nSprite < kMaxSprites);

    sprite[nSprite].x = x;
    sprite[nSprite].y = y;
    sprite[nSprite].z = z;
    sprite[nSprite].cstat = 0x101;
    sprite[nSprite].clipdist = g_kexMode ? 120 : 70;
    sprite[nSprite].shade = -12;
    sprite[nSprite].xrepeat = g_kexMode ? 100 : 80;
    sprite[nSprite].yrepeat = g_kexMode ? 100 : 80;
    sprite[nSprite].picnum = 1;
    sprite[nSprite].pal = sector[sprite[nSprite].sectnum].ceilingpal;
    sprite[nSprite].xoffset = 0;
    sprite[nSprite].yoffset = 0;
    sprite[nSprite].ang = nAngle;
    sprite[nSprite].xvel = 0;
    sprite[nSprite].yvel = 0;
    sprite[nSprite].zvel = 0;
    sprite[nSprite].lotag = runlist_HeadRun() + 1;
    sprite[nSprite].extra = -1;
    sprite[nSprite].hitag = 0;

//	GrabTimeSlot(3);

    // The remaster's Selkis uses this native scorpion controller but has
    // 4000 health.  Keep the classic boss value for non-KEX levels.
    scorpion[nScorp].nHealth = g_kexMode ? KexHealthUnits(4000) : 20000;
    scorpion[nScorp].nFrame = 0;
    scorpion[nScorp].nAction = 0;
    scorpion[nScorp].nSprite = nSprite;
    scorpion[nScorp].nTarget = -1;
    scorpion[nScorp].g = 0;
    scorpion[nScorp].i = 1;

    ScorpChan[nScorp] = nChannel;

    sprite[nSprite].owner = runlist_AddRunRec(sprite[nSprite].lotag - 1, nScorp, kRunScorp);
    scorpion[nScorp].f = runlist_AddRunRec(NewRun, nScorp, kRunScorp);

    nCreaturesLeft++;
    KexSelkisAudit(nScorp);

    return nScorp;
}

bool KexSelkis_RemoveSprite(int nSprite)
{
    // SELBUROW_script.txt uses kexSlaveActor::Remove() when the player
    // returns holding the scepter.  This is not a kill: no countdown,
    // scorpion spawns, corpse, drops, or generic death trigger may run.
    for (int nScorp = ScorpCount; nScorp < kMaxScorpions; nScorp++)
    {
        if (scorpion[nScorp].nSprite != nSprite)
            continue;

        runlist_DoSubRunRec(sprite[nSprite].owner);
        runlist_FreeRun(sprite[nSprite].lotag - 1);
        runlist_SubRunRec(scorpion[nScorp].f);
        sprite[nSprite].cstat = 0x8000;
        mydeletesprite(nSprite);
        scorpion[nScorp].nSprite = -1;
        scorpion[nScorp].nHealth = 0;
        scorpion[nScorp].nTarget = -1;
        scorpion[nScorp].nAction = 9;
        if (nCreaturesLeft > 0)
            nCreaturesLeft--;
        return true;
    }
    return false;
}

static void KexSelkisTestFinish(short nSprite)
{
    if ((unsigned)s_selkisTestScorp < (unsigned)kMaxScorpions && nSprite >= 0)
    {
        scorpion[s_selkisTestScorp].nHealth = 0;
        scorpion[s_selkisTestScorp].nAction = 9;
        scorpion[s_selkisTestScorp].nFrame = 0;
        scorpion[s_selkisTestScorp].nTarget = -1;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        sprite[nSprite].cstat = 0x8000; // env-only inert hidden corpse/run record
        if (s_selkisTestCreatureCounted)
            nCreaturesLeft--;
    }
    if ((unsigned)s_selkisTestPlayer < (unsigned)nTotalPlayers)
        PlayerList[s_selkisTestPlayer].invincibility = s_selkisTestInvincibility;
    s_selkisTest = -2;
    s_selkisTestScorp = -1;
    s_selkisTestCreatureCounted = 0;
}

void KexSelkis_TestTick()
{
    if (s_selkisTest == -1)
        s_selkisTest = g_kexMode && Bgetenv("NNB_KEX_SELKIS_TEST") ? 0 : -2;
    if (s_selkisTest < 0)
        return;

    if (s_selkisTest == 0)
    {
        short const nPlayer = PlayerList[nLocalPlayer].nSprite;
        if ((unsigned)nPlayer >= (unsigned)kMaxSprites || sprite[nPlayer].sectnum < 0)
            return;
        short const nSector = sprite[nPlayer].sectnum;
        s_selkisTestScorp = (short)BuildScorp(-1, sprite[nPlayer].x + 700,
            sprite[nPlayer].y, sector[nSector].floorz, nSector,
            sprite[nPlayer].ang, 0);
        if (s_selkisTestScorp < 0)
        {
            OSD_Printf("Kex Selkis test: FAIL (no free controller)\n");
            s_selkisTest = -2;
            return;
        }
        short const nSprite = scorpion[s_selkisTestScorp].nSprite;
        scorpion[s_selkisTestScorp].nTarget = nPlayer;
        scorpion[s_selkisTestScorp].nAction = 2;
        scorpion[s_selkisTestScorp].nFrame = 0;
        KexSelkisSetVelocity(nSprite, scorpion[s_selkisTestScorp].nHealth);
        s_selkisTestSpeed = ksqrt((uint32_t)(sprite[nSprite].xvel * sprite[nSprite].xvel
                                             + sprite[nSprite].yvel * sprite[nSprite].yvel));
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_selkisTestPlayer = nLocalPlayer;
        s_selkisTestInvincibility = PlayerList[nLocalPlayer].invincibility;
        // The full scripted Selkis lifecycle is 183 native player ticks.
        // It deliberately attacks the test player, so a one-tick grant lets
        // a fixture restart the map before its death timeline can be checked.
        // This is test-only and KexSelkisTestFinish() restores the prior
        // value.
        if (PlayerList[nLocalPlayer].invincibility < 900)
            PlayerList[nLocalPlayer].invincibility = 900;
        s_selkisTestStatsOkay = scorpion[s_selkisTestScorp].nHealth == KexHealthUnits(4000)
            && sprite[nSprite].clipdist == 120
            && sprite[nSprite].xrepeat == 100 && sprite[nSprite].yrepeat == 100
            && s_selkisTestSpeed >= 4590 && s_selkisTestSpeed <= 4620;
        s_selkisTestOkay = KexSelkisSequenceExact();
        s_selkisTestMeleeMask = 0;
        s_selkisTestFireFrame = -1;
        s_selkisTestProjectileOkay = 0;
        s_selkisTestDeathCallbacks = 0;
        s_selkisTestSpawnedScorps = 0;
        s_selkisTestDeathTriggers = 0;
        s_selkisTestCreatureCounted = 1;
        s_selkisTestPhase = 1;
        s_selkisTestTicks = 0;
        s_selkisTest = 1;
        OSD_Printf("Kex Selkis test: actor %d spawned; exact lifecycle probe started\n",
                   nSprite);
        return;
    }

    if (s_selkisTestScorp < ScorpCount || s_selkisTestScorp >= kMaxScorpions)
    {
        OSD_Printf("Kex Selkis test: FAIL (controller disappeared)\n");
        KexSelkisTestFinish(-1);
        return;
    }
    short const nSprite = scorpion[s_selkisTestScorp].nSprite;
    if ((unsigned)nSprite >= (unsigned)kMaxSprites || sprite[nSprite].sectnum < 0)
    {
        OSD_Printf("Kex Selkis test: FAIL (actor disappeared)\n");
        KexSelkisTestFinish(-1);
        return;
    }

    s_selkisTestTicks++;
    if (s_selkisTestPhase == 1 && s_selkisTestTicks == 9)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 2
            && scorpion[s_selkisTestScorp].nFrame == 0
            && s_selkisTestMeleeMask == 3;
        scorpion[s_selkisTestScorp].nAction = 3;
        scorpion[s_selkisTestScorp].nFrame = 0;
        s_selkisTestPhase = 2; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 2 && s_selkisTestTicks == 6)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 1
            && scorpion[s_selkisTestScorp].nFrame == 0
            && s_selkisTestFireFrame == 3 && s_selkisTestProjectileOkay;
        scorpion[s_selkisTestScorp].nAction = 4;
        scorpion[s_selkisTestScorp].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_selkisTestPhase = 3; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 3 && s_selkisTestTicks == 25)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 1
            && scorpion[s_selkisTestScorp].nFrame == 0;
        scorpion[s_selkisTestScorp].nAction = 5;
        scorpion[s_selkisTestScorp].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_selkisTestPhase = 4; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 4 && s_selkisTestTicks == 40)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 1
            && scorpion[s_selkisTestScorp].nFrame == 0;
        scorpion[s_selkisTestScorp].nAction = 6;
        scorpion[s_selkisTestScorp].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_selkisTestPhase = 5; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 5 && s_selkisTestTicks == 12)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 1
            && scorpion[s_selkisTestScorp].nFrame == 0;
        scorpion[s_selkisTestScorp].nAction = 7;
        scorpion[s_selkisTestScorp].nFrame = 0;
        sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
        s_selkisTestPhase = 6; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 6 && s_selkisTestTicks == 20)
    {
        s_selkisTestOkay &= scorpion[s_selkisTestScorp].nAction == 1
            && scorpion[s_selkisTestScorp].nFrame == 0;
        scorpion[s_selkisTestScorp].nHealth = 0;
        scorpion[s_selkisTestScorp].nAction = 8;
        scorpion[s_selkisTestScorp].nFrame = 0;
        scorpion[s_selkisTestScorp].g = 24;
        sprite[nSprite].xvel = sprite[nSprite].yvel = sprite[nSprite].zvel = 0;
        sprite[nSprite].cstat &= ~0x101;
        nCreaturesLeft--;
        s_selkisTestCreatureCounted = 0;
        s_selkisTestPhase = 7; s_selkisTestTicks = 0;
    }
    else if (s_selkisTestPhase == 7 && s_selkisTestTicks == 71)
    {
        int const base = SeqOffsets[kSeqScorp];
        bool const cadence = KexSelkisSequenceExact()
            && SeqSize[base] == 1 && SeqSize[base + 9] == 8
            && SeqSize[base + 18] == 6 && SeqSize[base + 27] == 9
            && SeqSize[base + 36] == 25 && SeqSize[base + 45] == 40
            && SeqSize[base + 46] == 12 && SeqSize[base + 47] == 20
            && SeqSize[base + 48] == 3 && SeqSize[base + 49] == 1;
        bool const flags = (FrameFlag[SeqBase[base + 18] + 3] & 0x84) == 0x84
            && (FrameFlag[SeqBase[base + 27]] & 0x80)
            && (FrameFlag[SeqBase[base + 27] + 6] & 0x80);
        bool const death = scorpion[s_selkisTestScorp].nAction == 9
            && scorpion[s_selkisTestScorp].nFrame == 0
            && s_selkisTestDeathCallbacks == 24
            && s_selkisTestSpawnedScorps == 12
            && s_selkisTestDeathTriggers == 1;
        bool const okay = s_selkisTestOkay && s_selkisTestStatsOkay
            && cadence && flags && death;
        OSD_Printf("Kex Selkis test: %s (seq 1/8/6/9/25/40/12/20/3/1, melee %d, fire %d, projectile %d, death %d callbacks/%d scorps/%d ticks/%d trigger)\n",
                   okay ? "PASS" : "FAIL", s_selkisTestMeleeMask,
                   s_selkisTestFireFrame, s_selkisTestProjectileOkay,
                   s_selkisTestDeathCallbacks, s_selkisTestSpawnedScorps,
                   s_selkisTestTicks, s_selkisTestDeathTriggers);
        KexSelkisTestFinish(nSprite);
    }
}

void FuncScorp(int a, int nDamage, int nRun)
{
    short nScorp = RunData[nRun].nVal;
    assert(nScorp >= 0 && nScorp < kMaxScorpions);

    short nSprite = scorpion[nScorp].nSprite;
    short nAction = scorpion[nScorp].nAction;

    bool bVal = false;

    short nTarget = -1;

    int nMessage = a & kMessageMask;

    switch (nMessage)
    {
        default:
        {
            DebugOut("unknown msg %d for Scorp\n", nMessage);
            return;
        }

        case 0x90000:
        {
            actionSeq const &action = ScorpActionSequence(nAction);
            seq_PlotSequence(a & 0xFFFF, SeqOffsets[kSeqScorp] + action.a,
                             scorpion[nScorp].nFrame, action.b);
            return;
        }

        case 0xA0000:
        {
            nDamage = runlist_CheckRadialDamage(nSprite);
            if (!nDamage) {
                return;
            }
            // else fall through to case 0x80000
            fallthrough__;
        }

        case 0x80000:
        {
            if (scorpion[nScorp].nHealth <= 0) {
                return;
            }

            scorpion[nScorp].nHealth -= nDamage;

            if (scorpion[nScorp].nHealth <= 0)
            {
                scorpion[nScorp].nHealth = 0;
                scorpion[nScorp].nFrame = 0;

                if (g_kexMode && KexSelkisSequenceExact())
                {
                    // selkis_death is a zero-delay trampoline: frame zero
                    // immediately starts A_SelkisCountDown(1) and enters the
                    // three-frame explosion loop.
                    scorpion[nScorp].nAction = 8;
                    scorpion[nScorp].g = 24;
                }
                else
                {
                    scorpion[nScorp].nAction = 4;
                    scorpion[nScorp].g = 10;
                }

                sprite[nSprite].xvel = 0;
                sprite[nSprite].yvel = 0;
                sprite[nSprite].zvel = 0;
                sprite[nSprite].cstat &= 0xFEFE;

                nCreaturesLeft--;
                return;
            }
            else
            {
                nTarget = a & 0xFFFF;

                if (g_kexMode)
                {
                    // AI_Selkis has noInFighting: only a player can replace
                    // the current target. InflictDamage still consumes the
                    // authored pain byte for every non-lethal hit.
                    if (nTarget >= 0 && sprite[nTarget].statnum == 100)
                        scorpion[nScorp].nTarget = nTarget;

                    if (KexDifficulty_PainRoll(16) && nAction < 8)
                    {
                        scorpion[nScorp].nAction = KexSelkisSequenceExact() ? 4 : 10;
                        scorpion[nScorp].nFrame = 0;
                        sprite[nSprite].xvel = 0;
                        sprite[nSprite].yvel = 0;
                    }
                    return;
                }

                if (nTarget >= 0)
                {
                    if (sprite[nSprite].statnum == 100 || (sprite[nSprite].statnum < 199 && !RandomSize(5)))
                    {
                        scorpion[nScorp].nTarget = nTarget;
                    }
                }

                if (!RandomSize(5))
                {
                    scorpion[nScorp].nAction = RandomSize(2) + 4;
                    scorpion[nScorp].nFrame = 0;
                    return;
                }

                if (RandomSize(2)) {
                    return;
                }

                D3PlayFX(StaticSound[kSoundScorpionICU], nSprite);

                goto FS_Pink_A;
            }
        }

        case 0x20000:
        {
            if (scorpion[nScorp].nHealth) {
                Gravity(nSprite);
            }

            bool const bExactSelkis = KexSelkisSequenceExact();
            actionSeq const &action = ScorpActionSequence(nAction);
            int nSeq = SeqOffsets[kSeqScorp] + action.a;
            short const nRenderedFrame = scorpion[nScorp].nFrame;

            sprite[nSprite].picnum = seq_GetSeqPicnum2(nSeq, scorpion[nScorp].nFrame);
            seq_MoveSequence(nSprite, nSeq, scorpion[nScorp].nFrame);

            scorpion[nScorp].nFrame++;

            if (scorpion[nScorp].nFrame >= SeqSize[nSeq])
            {
                scorpion[nScorp].nFrame = 0;
                bVal = true;
            }

            int nFlag = FrameFlag[SeqBase[nSeq] + scorpion[nScorp].nFrame];
            nTarget = scorpion[nScorp].nTarget;

            switch (nAction)
            {
                default:
                    return;

                case 0:
                {
                    if (scorpion[nScorp].g > 0)
                    {
                        scorpion[nScorp].g--;
                        return;
                    }

                    if ((nScorp & 0x1F) == (totalmoves & 0x1F))
                    {
                        if (nTarget < 0)
                        {
                            nTarget = FindPlayer(nSprite, 500);

                            if (nTarget >= 0)
                            {
                                if (bExactSelkis)
                                    KexSelkisPlaySound(nSprite, "scrp_icu");
                                else
                                    D3PlayFX(StaticSound[kSoundScorpionICU], nSprite);

                                scorpion[nScorp].nFrame = 0;
                                KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);

                                scorpion[nScorp].nAction = 1;
                                scorpion[nScorp].nTarget = nTarget;
                            }
                        }
                    }

                    return;
                }

                case 1:
                {
                    if (bExactSelkis)
                    {
                        if (nTarget < 0 || (unsigned)nTarget >= (unsigned)kMaxSprites
                            || sprite[nTarget].sectnum < 0
                            || !(sprite[nTarget].cstat & 0x101))
                        {
                            scorpion[nScorp].nAction = 0;
                            scorpion[nScorp].nFrame = 0;
                            scorpion[nScorp].nTarget = -1;
                            scorpion[nScorp].g = 30;
                            sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                            return;
                        }

                        int const nDistance = PlotCourseToSprite(nSprite, nTarget);
                        if (KexSelkisThink(nScorp, nSprite))
                            return;

                        if (nDistance < 768)
                        {
                            scorpion[nScorp].nAction = 2;
                            scorpion[nScorp].nFrame = 0;
                            sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                            return;
                        }

                        int const nMov = MoveCreatureWithCaution(nSprite);
                        if ((nMov & 0xC000) == 0xC000
                            && (nMov & 0x3FFF) == nTarget)
                        {
                            scorpion[nScorp].nAction = 2;
                            scorpion[nScorp].nFrame = 0;
                            sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                            return;
                        }
                        if (nMov & 0xC000)
                        {
                            sprite[nSprite].ang = (sprite[nSprite].ang + 256) & kAngleMask;
                            KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);
                        }

                        if (scorpion[nScorp].g > 0)
                        {
                            scorpion[nScorp].g--;
                            return;
                        }
                        if (cansee(sprite[nSprite].x, sprite[nSprite].y,
                                   sprite[nSprite].z - GetSpriteHeight(nSprite),
                                   sprite[nSprite].sectnum,
                                   sprite[nTarget].x, sprite[nTarget].y,
                                   sprite[nTarget].z - GetSpriteHeight(nTarget),
                                   sprite[nTarget].sectnum))
                        {
                            sprite[nSprite].xvel = sprite[nSprite].yvel = 0;
                            sprite[nSprite].ang = GetMyAngle(
                                sprite[nTarget].x - sprite[nSprite].x,
                                sprite[nTarget].y - sprite[nSprite].y);
                            scorpion[nScorp].g = KexDifficulty_AICooldown();
                            scorpion[nScorp].nAction = 3;
                            scorpion[nScorp].nFrame = 0;
                        }
                        return;
                    }

                    scorpion[nScorp].i--;

                    if (scorpion[nScorp].i <= 0)
                    {
                        scorpion[nScorp].i = RandomSize(5);
                        // GOTO FS_Pink_A:
                        goto FS_Pink_A;
                    }
                    else
                    {
                        int nMov = MoveCreatureWithCaution(nSprite);
                        if ((nMov & 0xC000) == 0xC000)
                        {
                            if (nTarget == (nMov & 0x3FFF))
                            {
                                int nAngle = getangle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);
                                if (AngleDiff(sprite[nSprite].ang, nAngle) < 64)
                                {
                                    scorpion[nScorp].nAction = 2;
                                    scorpion[nScorp].nFrame = 0;
                                }

                                goto FS_Red;
                            }
                            else
                            {
                                goto FS_Pink_A;
                            }
                        }
                        else if ((nMov & 0xC000) == 0x8000)
                        {
                            goto FS_Pink_A;
                        }
                        else
                        {
                            goto FS_Pink_B;
                        }
                    }
                }

                case 2:
                {
                    if (bExactSelkis)
                    {
                        if (nTarget < 0 || (unsigned)nTarget >= (unsigned)kMaxSprites
                            || sprite[nTarget].sectnum < 0
                            || !(sprite[nTarget].cstat & 0x101))
                        {
                            scorpion[nScorp].nAction = 0;
                            scorpion[nScorp].nFrame = 0;
                            scorpion[nScorp].nTarget = -1;
                            return;
                        }

                        if (nRenderedFrame == 0 || nRenderedFrame == 6)
                        {
                            if (nScorp == s_selkisTestScorp)
                                s_selkisTestMeleeMask |= nRenderedFrame == 0 ? 1 : 2;
                            if (PlotCourseToSprite(nSprite, nTarget) < 768)
                                runlist_DamageEnemy(nTarget, nSprite, 20);
                        }
                        if (nRenderedFrame == 8
                            && PlotCourseToSprite(nSprite, nTarget) >= 768)
                            KexSelkisEnterChase(nScorp, nSprite);
                        return;
                    }

                    if (nTarget == -1)
                    {
                        scorpion[nScorp].nAction = 0;
                        scorpion[nScorp].g = 5;
                    }
                    else
                    {
                        if (PlotCourseToSprite(nSprite, nTarget) >= 768)
                        {
                            scorpion[nScorp].nAction = 1;
                        }
                        else if (nFlag & 0x80)
                        {
                            runlist_DamageEnemy(nTarget, nSprite, 7);
                        }
                    }

                    goto FS_Red;
                }

                case 3:
                {
                    if (bExactSelkis)
                    {
                        if (nRenderedFrame == 3 && nTarget >= 0
                            && (unsigned)nTarget < (unsigned)kMaxSprites
                            && sprite[nTarget].sectnum >= 0)
                        {
                            int const nBullet = BuildKexAIFireProjectile(
                                nSprite, 16, nTarget, 2, 144, 64, 0);
                            if (nScorp == s_selkisTestScorp)
                            {
                                s_selkisTestFireFrame = nRenderedFrame;
                                s_selkisTestProjectileOkay =
                                    KexBullet_CheckAIFireProjectile(
                                        nBullet, nSprite, nTarget,
                                        2, 144, 64, 0);
                            }
                        }
                        if (nRenderedFrame == 5)
                            KexSelkisEnterChase(nScorp, nSprite);
                        return;
                    }

                    if (bVal)
                    {
                        scorpion[nScorp].h--;
                        if (scorpion[nScorp].h <= 0)
                        {
                            scorpion[nScorp].nAction = 1;

                            KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);

                            scorpion[nScorp].nFrame = 0;
                            return;
                        }
                    }

                    if (!(nFlag & 0x80)) {
                        return;
                    }

                    if (g_kexMode)
                    {
                        // selkis_shoot.tsc: Proj_Cobra_Red_Selkis, with
                        // noHomingThrust. It is a real moving projectile, not
                        // the native type-16 hitscan shortcut.
                        BuildKexAIFireProjectile(nSprite, 16, nTarget,
                                                 2, 144, 64, 0);
                    }
                    else
                    {
                        int const nBullet = BuildBullet(nSprite, 16, 0, 0, -1,
                            sprite[nSprite].ang, nTarget + 10000, 1) & 0xFFFF;
                        int const nBulletSprite = GetBulletSprite(nBullet);
                        if (nBulletSprite > -1)
                            PlotCourseToSprite(nBulletSprite, nTarget);
                    }

                    return;
                }

                case 4:
                case 5:
                case 6:
                case 7:
                {
                    if (bExactSelkis)
                    {
                        if (nTarget >= 0 && (unsigned)nTarget < (unsigned)kMaxSprites
                            && sprite[nTarget].sectnum >= 0
                            && KexSelkisFacesOnFrame(nAction, nRenderedFrame))
                            PlotCourseToSprite(nSprite, nTarget);

                        int const nLastFrame = nAction == 4 ? 24
                            : nAction == 5 ? 39 : nAction == 6 ? 11 : 19;
                        if (nRenderedFrame == nLastFrame)
                            KexSelkisEnterChase(nScorp, nSprite);
                        return;
                    }

                    if (!bVal) {
                        return;
                    }

                    if (scorpion[nScorp].nHealth > 0)
                    {
                        scorpion[nScorp].nAction = 1;
                        scorpion[nScorp].nFrame = 0;
                        scorpion[nScorp].g = 0;
                        return;
                    }

                    scorpion[nScorp].g--;
                    if (scorpion[nScorp].g <= 0)
                    {
                        scorpion[nScorp].nAction = 8;
                    }
                    else
                    {
                        scorpion[nScorp].nAction = RandomBit() + 6;
                    }

                    if (g_kexMode && scorpion[nScorp].nAction == 8)
                        scorpion[nScorp].g = 24; // A_SelkisCountDown start

                    return;
                }

                case 8:
                {
                    if (bExactSelkis)
                    {
                        // A_SelkisCountDown(0) exists only on explosion frame
                        // one. Twenty-four callbacks therefore span 71
                        // rendered ticks before that frame enters the corpse.
                        if (nRenderedFrame == 1)
                            KexSelkisCountDown(nScorp, nSprite);
                        return;
                    }

                    if (g_kexMode)
                    {
                        // Legacy patched-demo fallback: decrement once per
                        // sequence loop. The exact generated sequence takes
                        // the frame-one callback path above instead.
                        if (scorpion[nScorp].g > 0)
                        {
                            if (scorpion[nScorp].g & 1)
                            {
                                int nSpider = BuildSpider(-1, sprite[nSprite].x, sprite[nSprite].y,
                                                          sprite[nSprite].z, sprite[nSprite].sectnum,
                                                          sprite[nSprite].ang, 1);
                                if (nSpider != -1)
                                {
                                    int nSpiderSprite = GetSpiderSprite(nSpider);
                                    sprite[nSpiderSprite].ang = RandomSize(11);
                                    int nVel = RandomSize(5) + 1;
                                    sprite[nSpiderSprite].xvel = (Cos(sprite[nSpiderSprite].ang) >> 8) * nVel;
                                    sprite[nSpiderSprite].yvel = (Sin(sprite[nSpiderSprite].ang) >> 8) * nVel;
                                    sprite[nSpiderSprite].zvel = (-(RandomSize(5) + 3)) << 8;
                                }
                            }
                            scorpion[nScorp].g--;
                        }

                        if (scorpion[nScorp].g <= 0 || bVal)
                        {
                            int const sound = KexSound_Get("scrp_cry");
                            if (sound >= 0)
                                PlayLocalSound(sound, 0);
                            runlist_ChangeChannel(ScorpChan[nScorp], 1);
                            scorpion[nScorp].nAction = 9;
                            scorpion[nScorp].nFrame = 0;
                        }
                        return;
                    }

                    if (bVal)
                    {
                        scorpion[nScorp].nAction++; // set to 9
                        scorpion[nScorp].nFrame = 0;

                        runlist_ChangeChannel(ScorpChan[nScorp], 1);
                        return;
                    }

                    int nSpider = BuildSpider(-1, sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z, sprite[nSprite].sectnum, sprite[nSprite].ang);
                    if (nSpider != -1)
                    {
                        int nSpiderSprite = GetSpiderSprite(nSpider);

                        sprite[nSpiderSprite].ang = RandomSize(11);

                        int nVel = RandomSize(5) + 1;

                        sprite[nSpiderSprite].xvel = (Cos(sprite[nSpiderSprite].ang) >> 8) * nVel;
                        sprite[nSpiderSprite].yvel = (Sin(sprite[nSpiderSprite].ang) >> 8) * nVel;
                        sprite[nSpiderSprite].zvel = (-(RandomSize(5) + 3)) << 8;
                    }

                    return;
                }

                case 9:
                {
                    sprite[nSprite].cstat &= 0xFEFE;

                    // AI_Selkis' corpse is persistent in the remaster.  Its
                    // exit event has already fired when action 8 completed.
                    if (g_kexMode)
                        return;

                    if (bVal)
                    {
                        runlist_SubRunRec(scorpion[nScorp].f);
                        runlist_DoSubRunRec(sprite[nSprite].owner);
                        runlist_FreeRun(sprite[nSprite].lotag - 1);

                        mydeletesprite(nSprite);
                    }

                    return;
                }

                case 10:
                {
                    if (bVal)
                    {
                        scorpion[nScorp].nAction = 1;
                        scorpion[nScorp].nFrame = 0;
                        KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);
                    }
                    return;
                }
            }

            break;
        }
    }

FS_Pink_A:
    PlotCourseToSprite(nSprite, nTarget);
    sprite[nSprite].ang += RandomSize(7) - 63;
    sprite[nSprite].ang &= kAngleMask;

    KexSelkisSetVelocity(nSprite, scorpion[nScorp].nHealth);

FS_Pink_B:
    if (g_kexMode)
    {
        if (scorpion[nScorp].g > 0)
        {
            scorpion[nScorp].g--;
        }
        else if (cansee(sprite[nSprite].x, sprite[nSprite].y,
                        sprite[nSprite].z - GetSpriteHeight(nSprite), sprite[nSprite].sectnum,
                        sprite[nTarget].x, sprite[nTarget].y,
                        sprite[nTarget].z - GetSpriteHeight(nTarget), sprite[nTarget].sectnum))
        {
            sprite[nSprite].xvel = 0;
            sprite[nSprite].yvel = 0;
            sprite[nSprite].ang = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x,
                                             sprite[nTarget].y - sprite[nSprite].y);
            scorpion[nScorp].h = 1;
            scorpion[nScorp].g = KexDifficulty_AICooldown();
            scorpion[nScorp].nAction = 3;
            scorpion[nScorp].nFrame = 0;
        }
    }
    else
    {
        if (scorpion[nScorp].g)
        {
            scorpion[nScorp].g--;
        }
        else
        {
            scorpion[nScorp].g = 45;

            if (cansee(sprite[nSprite].x, sprite[nSprite].y, sprite[nSprite].z - GetSpriteHeight(nSprite), sprite[nSprite].sectnum,
                sprite[nTarget].x, sprite[nTarget].y, sprite[nTarget].z - GetSpriteHeight(nTarget), sprite[nTarget].sectnum))
            {
                sprite[nSprite].xvel = 0;
                sprite[nSprite].yvel = 0;
                sprite[nSprite].ang = GetMyAngle(sprite[nTarget].x - sprite[nSprite].x, sprite[nTarget].y - sprite[nSprite].y);

                scorpion[nScorp].h = RandomSize(2) + RandomSize(3);

                if (!scorpion[nScorp].h) {
                    scorpion[nScorp].g = RandomSize(5);
                }
                else
                {
                    scorpion[nScorp].nAction = 3;
                    scorpion[nScorp].nFrame = 0;
                }
            }
        }
    }

FS_Red:
    if (!nAction || nTarget == -1) {
        return;
    }

    if (!(sprite[nTarget].cstat & 0x101))
    {
        scorpion[nScorp].nAction = 0;
        scorpion[nScorp].nFrame = 0;
        scorpion[nScorp].g = 30;
        scorpion[nScorp].nTarget = -1;

        sprite[nSprite].xvel = 0;
        sprite[nSprite].yvel = 0;
    }
}

class ScorpLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void ScorpLoadSave::Load()
{
    Read(&ScorpCount, sizeof(ScorpCount));
    Read(&scorpion[ScorpCount], sizeof(Scorpion) * (kMaxScorpions - ScorpCount));
    Read(ScorpChan, sizeof(ScorpChan));
}

void ScorpLoadSave::Save()
{
    Write(&ScorpCount, sizeof(ScorpCount));
    Write(&scorpion[ScorpCount], sizeof(Scorpion) * (kMaxScorpions - ScorpCount));
    Write(ScorpChan, sizeof(ScorpChan));
}

static ScorpLoadSave* myLoadSave;

void ScorpLoadSaveConstruct()
{
    myLoadSave = new ScorpLoadSave();
}
