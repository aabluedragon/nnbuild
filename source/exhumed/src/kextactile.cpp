//-------------------------------------------------------------------------
/*
Copyright (C) 2026 NNBuild contributors

This file is part of PCExhumed.

PCExhumed is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

PCExhumed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

#include "build.h"
#include "baselayer.h"
#include "control.h"

#include "exhumed.h"
#include "player.h"
#include "kextactile.h"

// KEX's tactileBNVIBPlayer reads a compact little-endian stream.  The retail
// files currently use either version 4 (one shot) or version 12 (a looped
// range), with two uint16 motor strengths sampled at 200 Hz.  Keep this small
// cache local to the bridge: the largest shipped curve is quake (235 frames).
enum
{
    kKexTactileFiles = 32,
    kKexTactileFrames = 256,
    kKexTactileChannels = 3,
    kKexTactileWeapon = 0,
    kKexTactileWorld = 1,
    kKexTactileDamage = 2,
    kKexTactileTickRate = 30,
};

struct KexTactileData
{
    char name[20];
    uint16_t rate;
    uint16_t frames;
    uint16_t loopFirst;
    uint16_t loopLast;
    uint8_t loop;
    uint16_t motor[kKexTactileFrames][2];
};

struct KexTactilePlay
{
    KexTactileData const *data;
    int sourceSprite;
    uint32_t tick;
};

static KexTactileData s_tactile[kKexTactileFiles];
static int s_tactileCount;
static KexTactilePlay s_play[kKexTactileChannels];

int kex_vibration = 1;
int kex_vibration_volume = 50;

static uint16_t KexTactile_ReadLE16(uint8_t const *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t KexTactile_ReadLE32(uint8_t const *p)
{
    return (uint32_t)KexTactile_ReadLE16(p)
        | ((uint32_t)KexTactile_ReadLE16(p + 2) << 16);
}

static KexTactileData const *KexTactile_Load(char const *name)
{
    for (int i = 0; i < s_tactileCount; i++)
        if (!Bstrcasecmp(s_tactile[i].name, name))
            return &s_tactile[i];

    if (s_tactileCount >= kKexTactileFiles)
        return NULL;

    char path[BMAX_PATH];
    Bsnprintf(path, sizeof(path), "tactile/%s.bnvib", name);
    int const fil = kopen4loadfrommod(path, 0);
    if (fil == -1)
        return NULL;

    int const length = kfilelength(fil);
    uint8_t raw[20 + kKexTactileFrames * 4];
    if (length < 12 || length > (int)sizeof(raw) || kread(fil, raw, length) != length)
    {
        kclose(fil);
        return NULL;
    }
    kclose(fil);

    uint32_t const version = KexTactile_ReadLE32(raw);
    uint16_t const motors = KexTactile_ReadLE16(raw + 4);
    uint16_t const rate = KexTactile_ReadLE16(raw + 6);
    int header = 0;
    uint32_t bytes = 0, loopFirst = 0, loopLast = 0;
    if (version == 4)
    {
        header = 12;
        bytes = KexTactile_ReadLE32(raw + 8);
    }
    else if (version == 12 && length >= 20)
    {
        header = 20;
        loopFirst = KexTactile_ReadLE32(raw + 8);
        loopLast = KexTactile_ReadLE32(raw + 12);
        bytes = KexTactile_ReadLE32(raw + 16);
    }
    else
        return NULL;

    uint32_t const frames = bytes / 4;
    if (motors != 3 || !rate || bytes != (uint32_t)(length - header)
        || bytes & 3 || !frames || frames > kKexTactileFrames
        || (version == 12 && (loopFirst > loopLast || loopLast >= frames)))
        return NULL;

    KexTactileData &data = s_tactile[s_tactileCount++];
    Bstrncpyz(data.name, name, sizeof(data.name));
    data.rate = rate;
    data.frames = (uint16_t)frames;
    data.loop = version == 12;
    data.loopFirst = (uint16_t)loopFirst;
    data.loopLast = (uint16_t)loopLast;
    for (uint32_t i = 0; i < frames; i++)
    {
        data.motor[i][0] = KexTactile_ReadLE16(raw + header + i * 4);
        data.motor[i][1] = KexTactile_ReadLE16(raw + header + i * 4 + 2);
    }
    return &data;
}

static void KexTactile_Play(char const *name, int channel, int sourceSprite)
{
    if (!g_kexMode || (unsigned)channel >= kKexTactileChannels)
        return;
    KexTactileData const *data = KexTactile_Load(name);
    if (!data)
        return;
    s_play[channel].data = data;
    s_play[channel].sourceSprite = sourceSprite;
    s_play[channel].tick = 0;
}

static void KexTactile_StopChannel(int channel)
{
    if ((unsigned)channel < kKexTactileChannels)
        s_play[channel].data = NULL;
}

void KexTactile_Reset(void)
{
    Bmemset(s_play, 0, sizeof(s_play));
}

void KexTactile_StopAll(void)
{
    KexTactile_Reset();
}

void KexTactile_StopSprite(short nSprite)
{
    for (int i = 0; i < kKexTactileChannels; i++)
        if (s_play[i].data && s_play[i].sourceSprite == nSprite)
            s_play[i].data = NULL;
}

// Each table row mirrors an authored action in sprites/weapons/*.txt, except
// the three player-facing effects, which are owned by kexPuppet's native
// damage and quake paths.  "stop" is KEX's matching StopLoopingTactile call.
struct KexTactileAlias
{
    char const *sound;
    char const *curve;
    uint8_t channel;
    uint8_t stop;
};

static KexTactileAlias const s_aliases[] =
{
    { "grn_toss",      "bombthrow",    kKexTactileWeapon, 0 },
    { "co_fires",      "cstafffire",   kKexTactileWeapon, 0 },
    { "ft_fires",      "fthrowfire",   kKexTactileWeapon, 0 },
    { "ft_off2",       NULL,             kKexTactileWeapon, 1 },
    { "mg_pow",        "m90fire",      kKexTactileWeapon, 0 },
    { "sw_swipe",      "machfire",     kKexTactileWeapon, 0 },
    { "sw_hit",        "machflesh",    kKexTactileWeapon, 0 },
    { "sw_slash",      "machhit",      kKexTactileWeapon, 0 },
    { "p_fire",        "pistolfire",   kKexTactileWeapon, 0 },
    { "brc_chrg",      "shockold",     kKexTactileWeapon, 0 },
    { "brc_fire",      "braceletfire", kKexTactileWeapon, 1 },
    { "ra_on",         "rafire",       kKexTactileWeapon, 0 },
    { "jon_hit1",      "ppain01",      kKexTactileDamage, 0 },
    { "jon_hit2",      "ppain02",      kKexTactileDamage, 0 },
    { "jon_hit3",      "ppain03",      kKexTactileDamage, 0 },
    { "k_whit1",       "pwpain01",     kKexTactileDamage, 0 },
    { "k_whit2",       "pwpain02",     kKexTactileDamage, 0 },
    { "k_whit3",       "pwpain03",     kKexTactileDamage, 0 },
    { "k_whit4",       "pwpain04",     kKexTactileDamage, 0 },
    { "k_whit5",       "pwpain05",     kKexTactileDamage, 0 },
    { "k_electrocute", "pshock",       kKexTactileDamage, 0 },
};

void KexTactile_OnSound(char const *name, short nSprite)
{
    if (!g_kexMode || !name || (unsigned)nLocalPlayer >= (unsigned)kMaxPlayers
        || PlayerList[nLocalPlayer].nSprite != (nSprite & 0x0fff))
        return;

    for (auto const &alias : s_aliases)
    {
        if (Bstrcasecmp(name, alias.sound))
            continue;
        if (alias.stop)
            KexTactile_StopChannel(alias.channel);
        if (alias.curve)
            KexTactile_Play(alias.curve, alias.channel, nSprite & 0x0fff);
        return;
    }
}

void KexTactile_Quake(void)
{
    if ((unsigned)nLocalPlayer < (unsigned)kMaxPlayers)
        KexTactile_Play("quake", kKexTactileWorld, PlayerList[nLocalPlayer].nSprite);
}

static int KexTactile_Frame(KexTactileData const &data, int sample)
{
    if (sample < data.frames)
        return sample;
    if (!data.loop)
        return -1;
    int const length = data.loopLast - data.loopFirst + 1;
    return data.loopFirst + (sample - data.loopFirst) % length;
}

void KexTactile_Tick(void)
{
    if (!g_kexMode || !kex_vibration || !CONTROL_JoystickEnabled || !joystick.hasRumble)
        return;

    uint16_t low = 0, high = 0, duration = 0;
    bool playing = false;
    for (int channel = 0; channel < kKexTactileChannels; channel++)
    {
        KexTactilePlay &play = s_play[channel];
        KexTactileData const *data = play.data;
        if (!data)
            continue;

        int const first = (int)(((uint64_t)play.tick * data->rate
                                 + kKexTactileTickRate - 1) / kKexTactileTickRate);
        int const last = (int)(((uint64_t)(play.tick + 1) * data->rate
                                + kKexTactileTickRate - 1) / kKexTactileTickRate);
        uint32_t sumLow = 0, sumHigh = 0;
        int samples = 0;
        for (int sample = first; sample < last; sample++)
        {
            int const frame = KexTactile_Frame(*data, sample);
            if (frame < 0)
                break;
            sumLow += data->motor[frame][0];
            sumHigh += data->motor[frame][1];
            samples++;
        }
        if (!samples)
        {
            play.data = NULL;
            continue;
        }

        // The native loop is 30 Hz while KEX's haptic asset is 200 Hz.
        // Average each exact 1/30-second source interval, then let SDL hold
        // that value until the next bridge tick.  This preserves duration and
        // motor balance without an extra polling thread.
        int const scaledLow = (int)((sumLow / (uint32_t)samples)
                                    * (uint32_t)clamp(kex_vibration_volume, 0, 100) / 100);
        int const scaledHigh = (int)((sumHigh / (uint32_t)samples)
                                     * (uint32_t)clamp(kex_vibration_volume, 0, 100) / 100);
        low = max(low, (uint16_t)scaledLow);
        high = max(high, (uint16_t)scaledHigh);
        duration = max<uint16_t>(duration, (uint16_t)((samples * 1000 + data->rate - 1) / data->rate));
        play.tick++;
        playing = true;
    }

    if (playing)
    {
        joystick.rumbleLow = max(joystick.rumbleLow, low);
        joystick.rumbleHigh = max(joystick.rumbleHigh, high);
        joystick.rumbleTime = max(joystick.rumbleTime, duration);
    }
}
