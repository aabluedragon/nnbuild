//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
//-------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "fx_man.h"
#include "music.h"
#include "duke3d.h"
#include "osd.h"
#include "sounds.h"
#include "al_midi.h"

#include "renderlayer.h"

#include <atomic>

int32_t g_numEnvSoundsPlaying,g_highestSoundIdx = 0;

static int32_t MusicIsWaveform = 0;
static char *MusicPtr = 0;
static int32_t MusicLen = 0;
static int32_t MusicVoice = -1;
static int32_t MusicPaused = 0;

static uint32_t freeSlotQueue[MAXVOICES];
static std::atomic<uint32_t> freeSlotQueueIndex, freeSlotsPendingCnt;

static inline void S_SetProperties(assvoice_t* snd, int const owner, int const voice, int const dist, int const clock)
{
    snd->owner = owner;
    snd->id = voice;
    snd->dist = dist;
    snd->clock = clock;
    snd->detached = 0;
    //snd->last_pos = { 0, 0, 0 };
    //snd->last_sect = -1;
}

void S_SoundStartup(void)
{
#ifdef _WIN32
    void* initdata = (void*)win_gethwnd(); // used for DirectSound
#else
    void* initdata = NULL;
#endif

    int status = FX_Init(ud.config.NumVoices, ud.config.NumChannels, ud.config.MixRate, initdata);

    if (status != FX_Ok)
        return;

    for (int i = 0; i <= g_highestSoundIdx; ++i)
    {
        for (auto& voice : g_sounds[i].voices)
        {
            g_sounds[i].num = 0;
            S_SetProperties(&voice, -1, 0, UINT16_MAX, 0);
        }

#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[i] = CACHE1D_UNLOCKED;
#endif
    }

    FX_SetVolume(ud.config.FXVolume);
    S_MusicVolume(ud.config.MusicVolume);

#ifdef ASS_REVERSESTEREO
    FX_SetReverseStereo(ud.config.ReverseStereo);
#endif
    FX_SetCallBack(S_Callback);
    FX_SetPrintf(OSD_Printf);
}

void S_SoundShutdown(void)
{
    if (MusicVoice >= 0)
        S_MusicShutdown();

    int status = FX_Shutdown();
    if (status != FX_Ok)
    {
        Bsprintf(tempbuf, "S_SoundShutdown(): error: %s", FX_ErrorString(status));
        G_GameExit(tempbuf);
    }
}

void S_MusicStartup(void)
{
    int status;
    if ((status = MUSIC_Init(ud.config.MusicDevice)) == MUSIC_Ok)
    {
        if (ud.config.MusicDevice == ASS_AutoDetect)
            ud.config.MusicDevice = MIDI_GetDevice();
    }
    else if ((status = MUSIC_Init(ASS_AutoDetect)) == MUSIC_Ok)
    {
        ud.config.MusicDevice = MIDI_GetDevice();
    }
    else
    {
        initprintf("S_MusicStartup(): failed initializing: %s\n", MUSIC_ErrorString(status));
        return;
    }

    MUSIC_SetVolume(ud.config.MusicVolume);

    auto const fil = kopen4load("d3dtimbr.tmb", 0);

    if (fil != buildvfs_kfd_invalid)
    {
        int l = kfilelength(fil);
        auto tmb = (uint8_t*)Xmalloc(l);
        kread(fil, tmb, l);
        AL_RegisterTimbreBank(tmb);
        Xfree(tmb);
        kclose(fil);
    }
}

void S_MusicShutdown(void)
{
    if (ud.config.MusicDevice < 0)
        return;

    S_StopMusic();

    if (MUSIC_Shutdown() != MUSIC_Ok)
        initprintf("%s", MUSIC_ErrorString(MUSIC_ErrorCode));
}

void S_PauseMusic(int32_t onf)
{
    if (MusicPaused == onf || (MusicIsWaveform && MusicVoice < 0))
        return;

    MusicPaused = onf;

    if (MusicIsWaveform)
    {
        FX_PauseVoice(MusicVoice, onf);
        return;
    }

    if (onf)
        MUSIC_Pause();
    else
        MUSIC_Continue();
}

void S_MusicVolume(int32_t volume)
{
    if (MusicIsWaveform && MusicVoice >= 0)
        FX_SetPan(MusicVoice, volume, volume, volume);

    MUSIC_SetVolume(volume);
}

void S_MenuSound(void)
{
    static int32_t SoundNum=0;
    int const menusnds[] =
    {
        LASERTRIP_EXPLODE,
        DUKE_GRUNT,
        DUKE_LAND_HURT,
        CHAINGUN_FIRE,
        SQUISHED,
        KICK_HIT,
        PISTOL_RICOCHET,
        PISTOL_BODYHIT,
        PISTOL_FIRE,
        SHOTGUN_FIRE,
        BOS1_WALK,
        RPG_EXPLODE,
        PIPEBOMB_BOUNCE,
        PIPEBOMB_EXPLODE,
        NITEVISION_ONOFF,
        RPG_SHOOT,
        SELECT_WEAPON
    };

    S_PlaySound(menusnds[SoundNum++ % ARRAY_SIZE(menusnds)]);
}

int32_t S_PlayMusic(const char *fn, const int32_t sel)
{
    char *ofn = const_cast<char *>(fn), *testfn, *extension;
    int32_t fp;
    const char *alt = 0;

    if (ud.config.MusicToggle == 0) return 0;
    if (ud.config.MusicDevice < 0) return 0;

    if (MapInfo[sel].alt_musicfn != NULL)
        alt = fn = MapInfo[sel].alt_musicfn;

    testfn = (char *) Xmalloc(strlen(fn) + 5);
    strcpy(testfn, fn);
    extension = strrchr(testfn, '.');

    do
    {
        if (extension && !Bstrcasecmp(extension, ".mid"))
        {
            // we've been asked to load a .mid file, but first let's see
            // if there's an ogg with the same base name lying around
            strcpy(extension, ".ogg");
            fp = kopen4loadfrommod(testfn, 0);
            if (fp >= 0)
            {
                Xfree(testfn);
                break;
            }
        }

        Xfree(testfn);

        // just use what we've been given
        fp = kopen4loadfrommod(fn, 0);

        if (alt && fp < 0)
            fp = kopen4loadfrommod(ofn, 0);
    }
    while (0);

    if (fp < 0)
    {
        OSD_Printf(OSD_ERROR "S_PlayMusic(): error: can't open \"%s\" for playback!\n",fn);
        return 0;
    }

    S_StopMusic();

    MusicPtr = (char *) Xmalloc((MusicLen = kfilelength(fp)));

    if ((g_musicSize = kread(fp, (char *)MusicPtr, MusicLen)) != MusicLen)
    {
        OSD_Printf(OSD_ERROR "S_PlayMusic(): error: read %d bytes from \"%s\", expected %d\n",g_musicSize, fn, MusicLen);
        kclose(fp);
        g_musicSize = 0;
        return 0;
    }

    kclose(fp);

    if (!Bmemcmp(MusicPtr, "MThd", 4))
    {
        MUSIC_PlaySong(MusicPtr, MusicLen, MUSIC_LoopSong, fn);
        MusicIsWaveform = 0;
    }
    else
    {
        if ((MusicVoice = FX_Play(MusicPtr, MusicLen, 0, 0, 0, ud.config.MusicVolume, ud.config.MusicVolume, ud.config.MusicVolume,
            FX_MUSIC_PRIORITY, fix16_one, MUSIC_ID)) > FX_Ok)

            MusicIsWaveform = 1;
    }
    return (alt != 0);
}

void S_StopMusic(void)
{
    MusicPaused = 0;

    if (MusicIsWaveform && MusicVoice >= 0)
    {
        FX_StopSound(MusicVoice);
        MusicVoice = -1;
        MusicIsWaveform = 0;
    }

    MUSIC_StopSong();

    if (MusicPtr)
    {
        Xfree(MusicPtr);
        MusicPtr = NULL;
        g_musicSize = MusicLen = 0;
    }
}

void S_Cleanup(void)
{
    static uint32_t ldpos = 0;

    while (freeSlotsPendingCnt)
    {
        --freeSlotsPendingCnt;

        uint32_t num = freeSlotQueue[++ldpos & (MAXVOICES - 1)];
#ifdef DEBUGGINGAIDS
        Bassert(num != 0xdeadbeef);
        freeSlotQueue[ldpos & (MAXVOICES - 1)] = 0xdeadbeef;
#endif
        // negative index is RTS playback
        if ((int32_t)num < 0)
        {
            int const rtsindex = klabs((int32_t)num);

            if (rts_lumplockbyte[rtsindex] >= CACHE1D_LOCKED)
                --rts_lumplockbyte[rtsindex];
            continue;
        }

        // num + (MAXSOUNDS*MAXSOUNDINSTANCES) is a sound played globally
        // for which there was no open slot to keep track of the voice
        if (num >= (MAXSOUNDS * MAXSOUNDINSTANCES))
        {
#ifdef CACHING_DOESNT_SUCK
            --g_soundlocks[num - (MAXSOUNDS * MAXSOUNDINSTANCES)];
#endif
            continue;
        }

        int const voiceindex = num & (MAXSOUNDINSTANCES - 1);

        num = (num - voiceindex) / MAXSOUNDINSTANCES;

        auto& snd = g_sounds[num];
        auto& voice = snd.voices[voiceindex];

        int const spriteNum = voice.owner;

#ifdef DEBUGGINGAIDS
        if (EDUKE32_PREDICT_FALSE(snd.num > MAXSOUNDINSTANCES))
            OSD_Printf(OSD_ERROR "S_Cleanup(): num exceeds MAXSOUNDINSTANCES! g_sounds[%d].num %d wtf?\n", num, snd.num);
        else
#endif
            if (snd.num > 0)
                --snd.num;

        // MUSICANDSFX uses t_data[0] to control restarting the sound
        // CLEAR_SOUND_T0
        if (spriteNum != -1 && S_IsAmbientSFX(spriteNum) && sector[SECT(spriteNum)].lotag < 3)  // ST_2_UNDERWATER
            actor[spriteNum].temp_data[0] = 0;

        S_SetProperties(&voice, -1, 0, UINT16_MAX, 0);

#ifdef CACHING_DOESNT_SUCK
        --g_soundlocks[num];
#endif
    }
}

// returns number of bytes read
int32_t S_LoadSound(uint32_t num)
{
    if ((unsigned)num > (unsigned)g_highestSoundIdx || EDUKE32_PREDICT_FALSE(g_sounds[num].filename == NULL))
        return 0;

    auto& snd = g_sounds[num];

    buildvfs_kfd fp = S_OpenAudio(snd.filename, g_loadFromGroupOnly, 0);

    if (EDUKE32_PREDICT_FALSE(fp == buildvfs_kfd_invalid))
    {
        OSD_Printf(OSDTEXT_RED "Sound %s(#%d) not found!\n", snd.filename, num);
        return 0;
    }

    int32_t l = kfilelength(fp);
    g_soundlocks[num] = CACHE1D_PERMANENT;
    snd.siz = l;
    g_cache.allocateBlock((intptr_t*)&snd.ptr, l, (char*)&g_soundlocks[num]);
    l = kread(fp, snd.ptr, l);
    kclose(fp);

    return l;
}

void S_PrecacheSounds(void)
{
    int i, j;

    if (ud.config.FXDevice < 0) return;
    j = 0;

    int precachestart = timerGetTicks();

    initprintf("Precaching sounds...\n");

    for (i = 0; i <= g_highestSoundIdx; i++)
    {
        if (g_sounds[i].ptr == 0)
        {
            j++;

            if ((j & 7) == 0)
                gameHandleEvents();

            S_LoadSound(i);
        }
    }

    initprintf("Sound precache finished in %dms\n", timerGetTicks() - precachestart);
}

static inline int S_GetPitch(int num)
{
    auto const& snd = g_sounds[num];
    int const   range = klabs(snd.pe - snd.ps);

    return (range == 0) ? snd.ps : min(snd.ps, snd.pe) + rand() % range;
}

static int S_TakeSlot(int soundNum)
{
    S_Cleanup();

    uint16_t dist = 0;
    uint16_t clock = 0;

    int bestslot = 0;
    int slot = 0;

    auto& snd = g_sounds[soundNum];

    do
    {
        auto& voice = snd.voices[slot];

        if (voice.dist > dist || (voice.dist == dist && voice.clock > clock))
        {
            clock = voice.clock;
            dist = voice.dist;

            bestslot = slot;
        }

        slot++;
    } while (slot < MAXSOUNDINSTANCES && snd.voices[slot].id > 0);

    if (slot != MAXSOUNDINSTANCES)
        return slot;

    if (FX_SoundActive(snd.voices[bestslot].id))
        FX_StopSound(snd.voices[bestslot].id);

    S_Cleanup();

    return bestslot;
}

static int S_GetSlot(int soundNum)
{
    int slot = 0;

    while (slot < MAXSOUNDINSTANCES && g_sounds[soundNum].voices[slot].id > 0)
        slot++;

    return slot == MAXSOUNDINSTANCES ? S_TakeSlot(soundNum) : slot;
}

static inline int S_GetAngle(int ang, const vec3_t* cam, const vec3_t* pos)
{
    return (2048 + ang - getangle(cam->x - pos->x, cam->y - pos->y)) & 2047;
}

static int S_CalcDistAndAng(int spriteNum, int soundNum, int camSect, int camAng,
    const vec3_t* camPos, const vec3_t* soundPos, int soundSect, int* distPtr, int* angPtr)
{
    int sndang = 0, sndist = 0, explosion = 0;

    int valid_sprite = ((unsigned)spriteNum < MAXSPRITES);
    int picnum = valid_sprite ? sprite[spriteNum].picnum : -1;
    int from_self = valid_sprite ? (picnum == APLAYER && sprite[spriteNum].yvel == screenpeek) : 0;

    if(!from_self)
    {
        sndang = S_GetAngle(camAng, camPos, soundPos);
        sndist = FindDistance3D(camPos->x - soundPos->x, camPos->y - soundPos->y, (camPos->z - soundPos->z));

        if (valid_sprite && (g_sounds[soundNum].m & (SF_GLOBAL | SF_DTAG)) != SF_GLOBAL && S_IsAmbientSFX(spriteNum) && (sector[soundSect].lotag & 0xff) < 9)  // ST_9_SLIDING_ST_DOOR
            sndist = divscale14(sndist, sprite[spriteNum].hitag + 1);
    }

    sndist += g_sounds[soundNum].vo;
    if (sndist < 0)
        sndist = 0;

    if (camSect > -1 && sndist && picnum != MUSICANDSFX
        && !cansee(camPos->x, camPos->y, camPos->z - (24 << 8), camSect, soundPos->x, soundPos->y, soundPos->z - (24 << 8), soundSect))
        sndist += sndist >> 5;

    if ((g_sounds[soundNum].m & (SF_GLOBAL | SF_DTAG)) == (SF_GLOBAL | SF_DTAG) || soundNum == PIPEBOMB_EXPLODE || soundNum == LASERTRIP_EXPLODE || soundNum == RPG_EXPLODE)
    {
        int const sdist = g_sounds[soundNum].vo > 0 ? g_sounds[soundNum].vo : 6144;

        explosion = true;

        if (sndist > sdist)
            sndist = sdist;
    }

    if ((g_sounds[soundNum].m & (SF_GLOBAL | SF_DTAG)) == SF_GLOBAL || sndist < ((255 - LOUDESTVOLUME) << 6))
        sndist = ((255 - LOUDESTVOLUME) << 6);

    *distPtr = sndist;
    *angPtr = sndang;

    return explosion;
}

static bool S_PredictionCheck(int spriteNum)
{
    if (numplayers > 1)
    {
        // Do not predict during correction phase, lest we annihilate our ears.
        if (oldnet_predicting == PREDICTSTATE_CORRECT)
            return true;

        // Predictable, but we're not predicting, so block.
        if (sounds_predictable && !oldnet_predicting)
        {
            if (spriteNum < 0)
                return true;

            if (sprite[spriteNum].picnum == APLAYER && sprite[spriteNum].yvel == myconnectindex)
                return true;
        }
    }

    return false;
}

int32_t S_PlaySoundXYZ(int32_t num,int32_t i,int32_t x,int32_t y,int32_t z)
{
    vec3_t pos;
    pos.x = x;
    pos.y = y;
    pos.z = z;

    return S_PlaySound3D(num, i, &pos);
}

int S_PlaySound3D(int num, int spriteNum, const vec3_t* pos)
{
    if (S_PredictionCheck(spriteNum))
        return -1;

    int32_t j = X_OnEventWithReturn(EVENT_SOUND, spriteNum, screenpeek, num);

    if ((j == -1 && num != -1) || !ud.config.SoundToggle) // check that the user returned -1, but only if -1 wasn't playing already (in which case, warn)
        return -1;

    int const sndNum = j;
    sound_t& snd = g_sounds[sndNum];

    if (EDUKE32_PREDICT_FALSE((unsigned)sndNum > (unsigned)g_highestSoundIdx || snd.filename == NULL || snd.ptr == NULL))
    {
        OSD_Printf("WARNING: invalid sound #%d\n", num);
        return -1;
    }

    auto const pPlayer = g_player[myconnectindex].ps;

    if (((snd.m & SF_ADULT) && ud.lockout) || (unsigned)spriteNum >= MAXSPRITES || (pPlayer->gm & MODE_MENU) || !FX_VoiceAvailable(snd.pr)
        || (pPlayer->timebeforeexit > 0 && pPlayer->timebeforeexit <= GAMETICSPERSEC * 3))
        return -1;

    // Duke talk
    if (snd.m & SF_TALK)
    {
        if ((ud.multimode > 1) && sprite[spriteNum].picnum == APLAYER && sprite[spriteNum].yvel != screenpeek) // other player sound
        {
            if ((ud.config.VoiceToggle & 4) != 4)
                return -1;
        }
        else if ((ud.config.VoiceToggle & 1) != 1)
            return -1;

        // don't play if any Duke talk sounds are already playing
        for (j = 0; j <= g_highestSoundIdx; ++j)
        {
            if ((g_sounds[j].m & SF_TALK) && g_sounds[j].num > 0)
            {
                for (int voice = 0; voice < MAXSOUNDINSTANCES; voice++)
                {
                    // [JM] Only cancel if it's coming from the current player or a non-player object.
                    if ((g_sounds[j].voices[voice].owner == spriteNum) || sprite[spriteNum].picnum != APLAYER)
                        return -1;
                }
            }
        }
    }
    else if ((snd.m & (SF_DTAG | SF_GLOBAL)) == SF_DTAG)  // Duke-Tag sound
    {
        int const voice = S_PlaySound(sndNum);

        if (voice <= FX_Ok)
            return -1;

        j = 0;
        while (j < MAXSOUNDINSTANCES && snd.voices[j].id != voice)
            j++;

#ifdef DEBUGGINGAIDS
        if (EDUKE32_PREDICT_FALSE(j >= MAXSOUNDINSTANCES))
        {
            OSD_Printf(OSD_ERROR "%s %d: WTF?\n", __FILE__, __LINE__);
            return -1;
        }
#endif

        snd.voices[j].owner = spriteNum;

        return voice;
    }

    int32_t    sndist, sndang;
    int const  explosionp = S_CalcDistAndAng(spriteNum, sndNum, CAMERA(sect), fix16_to_int(CAMERA(q16ang)), &CAMERA(pos), pos, sprite[spriteNum].sectnum, &sndist, &sndang);
    int        pitch = S_GetPitch(sndNum);
    auto const pOther = g_player[screenpeek].ps;

    if (pOther->sound_pitch)
        pitch += pOther->sound_pitch;

    if (explosionp)
    {
        if (pOther->cursectnum > -1 && sector[pOther->cursectnum].lotag == ST_2_UNDERWATER)
            pitch -= 1024;
    }
    else
    {
        if (sndist > 32767 && PN(spriteNum) != MUSICANDSFX && (snd.m & (SF_LOOP | SF_MSFX)) == 0)
            return -1;

        if (pOther->cursectnum > -1 && sector[pOther->cursectnum].lotag == ST_2_UNDERWATER
            && (snd.m & SF_TALK) == 0)
            pitch = -768;
    }

    if (snd.num > 0 && PN(spriteNum) != MUSICANDSFX)
        S_StopEnvSound(sndNum, spriteNum);

#ifdef CACHING_DOESNT_SUCK
    if (++g_soundlocks[sndNum] < CACHE1D_LOCKED)
        g_soundlocks[sndNum] = CACHE1D_LOCKED;
#endif

    int const sndSlot = S_GetSlot(sndNum);

    if (sndSlot >= MAXSOUNDINSTANCES)
    {
#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[sndNum]--;
#endif
        return -1;
    }

    int const repeatp = (snd.m & SF_LOOP);

    if (repeatp && (snd.m & SF_ONEINST_INTERNAL) && snd.num > 0)
    {
#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[sndNum]--;
#endif
        return -1;
    }

    int const voice = FX_Play3D(snd.ptr, snd.siz, repeatp ? FX_LOOP : FX_ONESHOT, pitch, sndang >> 4, sndist >> 6,
        snd.pr, snd.volume, (sndNum * MAXSOUNDINSTANCES) + sndSlot);

    if (voice <= FX_Ok)
    {
#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[sndNum]--;
#endif
        return -1;
    }

    snd.num++;

    S_SetProperties(&snd.voices[sndSlot], spriteNum, voice, sndist >> 6, 0);

    return voice;
}

int S_PlaySound(int num)
{
    if (S_PredictionCheck(-1))
        return -1;

    int sndnum = X_OnEventWithReturn(EVENT_SOUND, g_player[screenpeek].ps->i, screenpeek, num);

    if ((sndnum == -1 && num != -1) || !ud.config.SoundToggle) // check that the user returned -1, but only if -1 wasn't playing already (in which case, warn)
        return -1;

    num = sndnum;

    sound_t& snd = g_sounds[num];

    if (EDUKE32_PREDICT_FALSE((unsigned)num > (unsigned)g_highestSoundIdx || snd.filename == NULL || snd.ptr == NULL))
    {
        OSD_Printf("WARNING: invalid sound #%d\n", num);
        return -1;
    }

    if ((!(ud.config.VoiceToggle & 1) && (snd.m & SF_TALK)) || ((snd.m & SF_ADULT) && ud.lockout) || !FX_VoiceAvailable(snd.pr))
        return -1;

    int const pitch = S_GetPitch(num);

#ifdef CACHING_DOESNT_SUCK
    if (++g_soundlocks[num] < CACHE1D_LOCKED)
        g_soundlocks[num] = CACHE1D_LOCKED;
#endif

    sndnum = S_GetSlot(num);

    if (sndnum >= MAXSOUNDINSTANCES)
    {
#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[num]--;
#endif
        return -1;
    }

    int const voice = (snd.m & SF_LOOP) ? FX_Play(snd.ptr, snd.siz, 0, -1, pitch, LOUDESTVOLUME, LOUDESTVOLUME,
        LOUDESTVOLUME, snd.siz, snd.volume, (num * MAXSOUNDINSTANCES) + sndnum)
        : FX_Play3D(snd.ptr, snd.siz, FX_ONESHOT, pitch, 0, 255 - LOUDESTVOLUME, snd.pr, snd.volume,
            (num * MAXSOUNDINSTANCES) + sndnum);

    if (voice <= FX_Ok)
    {
#ifdef CACHING_DOESNT_SUCK
        g_soundlocks[num]--;
#endif
        return -1;
    }

    snd.num++;
    S_SetProperties(&snd.voices[sndnum], -1, voice, 255 - LOUDESTVOLUME, 0);

    return voice;
}

int A_PlaySound(int soundNum, int spriteNum)
{
    if (EDUKE32_PREDICT_FALSE((unsigned)soundNum > (unsigned)g_highestSoundIdx))
        return -1;

    return (unsigned)spriteNum >= MAXSPRITES ? S_PlaySound(soundNum) :
        S_PlaySound3D(soundNum, spriteNum, &sprite[spriteNum].pos);
}

void S_StopEnvSound(int sndNum, int sprNum)
{
    if (S_PredictionCheck(sprNum))
        return;

    if (EDUKE32_PREDICT_FALSE((unsigned)sndNum > (unsigned)g_highestSoundIdx) || g_sounds[sndNum].num <= 0)
        return;

    int j;

    do
    {
        for (j = 0; j < MAXSOUNDINSTANCES; ++j)
        {
            S_Cleanup();

            auto& voice = g_sounds[sndNum].voices[j];

            if ((sprNum == -1 && voice.id > FX_Ok) || (sprNum != -1 && voice.owner == sprNum))
            {
#ifdef DEBUGGINGAIDS
                if (EDUKE32_PREDICT_FALSE(sprNum >= 0 && voice.id <= FX_Ok))
                    initprintf(OSD_ERROR "S_StopEnvSound(): bad voice %d for sound ID %d index %d!\n", voice.id, sndNum, j);
                else
#endif
                    if (voice.id > FX_Ok)
                    {
                        if (FX_SoundActive(voice.id))
                            FX_StopSound(voice.id);
                        break;
                    }
            }
        }
    } while (j < MAXSOUNDINSTANCES);
}

void S_ChangeSoundPitch(int soundNum, int spriteNum, int pitchoffset)
{
    if (S_PredictionCheck(spriteNum))
        return;

    if ((unsigned)soundNum > (unsigned)g_highestSoundIdx || g_sounds[soundNum].num <= 0)
        return;

    for (auto& voice : g_sounds[soundNum].voices)
    {
        if ((spriteNum == -1 && voice.id > FX_Ok) || (spriteNum != -1 && voice.owner == spriteNum))
        {
            if (EDUKE32_PREDICT_FALSE(spriteNum >= 0 && voice.id <= FX_Ok))
                initprintf(OSD_ERROR "S_ChangeSoundPitch(): bad voice %d for sound ID %d!\n", voice.id, soundNum);
            else if (voice.id > FX_Ok && FX_SoundActive(voice.id))
                FX_SetPitch(voice.id, pitchoffset);
            break;
        }
    }
}

void S_Update(void)
{
    if ((g_player[myconnectindex].ps->gm & (MODE_GAME | MODE_DEMO)) == 0)
        return;

    g_numEnvSoundsPlaying = 0;

    const vec3_t* c;
    int32_t ca, cs;

    if (ud.camerasprite == -1)
    {
        if (ud.overhead_on != 2)
        {
            c = &CAMERA(pos);
            cs = CAMERA(sect);
            ca = fix16_to_int(CAMERA(q16ang));
        }
        else
        {
            auto pPlayer = g_player[screenpeek].ps;
            c = &pPlayer->pos;
            cs = pPlayer->cursectnum;
            ca = fix16_to_int(pPlayer->q16ang);
        }
    }
    else
    {
        c = &sprite[ud.camerasprite].pos;
        cs = sprite[ud.camerasprite].sectnum;
        ca = sprite[ud.camerasprite].ang;
    }

    int       sndnum = 0;
    int const highest = g_highestSoundIdx;

    do
    {
        if (g_sounds[sndnum].num == 0)
            continue;

        S_Cleanup();

        for (auto& voice : g_sounds[sndnum].voices)
        {
            int const spriteNum = voice.owner;
            int sndist, sndang;

            if ((unsigned)spriteNum >= MAXSPRITES || voice.id <= FX_Ok || !FX_SoundActive(voice.id))
                continue;

            if(voice.detached)
                S_CalcDistAndAng(-1, sndnum, cs, ca, c, &voice.last_pos, voice.last_sect, &sndist, &sndang);
            else
                S_CalcDistAndAng(spriteNum, sndnum, cs, ca, c, &sprite[spriteNum].pos, sprite[spriteNum].sectnum, &sndist, &sndang);

            if (S_IsAmbientSFX(spriteNum))
                g_numEnvSoundsPlaying++;

            // AMBIENT_SOUND
            FX_Pan3D(voice.id, sndang >> 4, sndist >> 6);
            voice.dist = sndist >> 6;
            voice.clock++;
        }
    } while (++sndnum <= highest);
}

// when playing back a new sound needs an existing sound to be stopped first
// S_Callback() can be called from either the audio thread when a sound ends, or the main thread
// this is essentially a multiple producer single consumer queue
void S_Callback(intptr_t num)
{
    if ((int32_t)num == MUSIC_ID)
        return;

    freeSlotQueue[++freeSlotQueueIndex & (MAXVOICES - 1)] = (uint32_t)num;
    freeSlotsPendingCnt++;
}

void S_ClearSoundLocks(void)
{
#ifdef CACHING_DOESNT_SUCK
    int32_t i;
    int32_t const msp = g_highestSoundIdx;

    for (native_t i = 0; i < 11; ++i)
        if (rts_lumplockbyte[i] >= CACHE1D_LOCKED)
            rts_lumplockbyte[i] = CACHE1D_UNLOCKED;

    int32_t const msp = g_highestSoundIdx;

    for (native_t i = 0; i <= msp; ++i)
        if (g_soundlocks[i] >= CACHE1D_LOCKED)
            g_soundlocks[i] = CACHE1D_UNLOCKED;
#endif
}

int A_CheckSoundPlaying(int spriteNum, int soundNum)
{
    if (EDUKE32_PREDICT_FALSE((unsigned)soundNum > (unsigned)g_highestSoundIdx)) return 0;

    if (g_sounds[soundNum].num > 0 && spriteNum >= 0)
    {
        for (auto& voice : g_sounds[soundNum].voices)
            if (voice.owner == spriteNum)
                return 1;
    }

    return (spriteNum == -1) ? (g_sounds[soundNum].num != 0) : 0;
}

int S_CheckSoundPlaying(int soundNum)
{
    if (EDUKE32_PREDICT_FALSE((unsigned)soundNum > (unsigned)g_highestSoundIdx)) return false;
    return (g_sounds[soundNum].num != 0);
}

void S_DetachSoundsFromSprite(int spriteNum)
{
    if (oldnet_predicting) return;

    for (int i = 0; i <= g_highestSoundIdx; ++i)
    {
        if (g_sounds[i].num == 0)
            continue;

        for (int voice = 0; voice < MAXSOUNDINSTANCES; voice++)
        {
            if (g_sounds[i].voices[voice].detached)
                continue;

            if (g_sounds[i].voices[voice].owner == spriteNum)
            {
                g_sounds[i].voices[voice].detached = 1;
                g_sounds[i].voices[voice].last_sect = sprite[spriteNum].sectnum;
                g_sounds[i].voices[voice].last_pos = sprite[spriteNum].pos;
            }
        }
    }
}