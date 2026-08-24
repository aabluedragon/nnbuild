//-------------------------------------------------------------------------
/*
Copyright (C) 2026 VibeBuild32 contributors

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

// The remaster's movies. Nightdive ships fifteen Ogg Theora files next to
// the .kpf -- the NightDive/Kex/Throwback logos, the intro and both endings,
// each in four languages -- and this plays them: libogg splits the container,
// libtheoradec decodes the video, and the movie's own Vorbis stream goes to
// the engine's mixer untouched (its pages, concatenated, are already a valid
// standalone .ogg, and audiolib plays Vorbis natively). Frames are converted
// to RGB in software and drawn as one textured quad, the way animvpx draws
// the Duke cutscenes; timing follows the stream's own frame rate.
//
// Built only when Theora is available (HAVE_THEORA from the makefile) and
// under OpenGL; Kex_PlayTheora() returns 0 otherwise and the caller falls
// back to whatever it played before.

#include "compat.h"
#include "baselayer.h"
#include "build.h"
#include "exhumed.h"
#include "keyboard.h"
#include "fx_man.h"
#include "sound.h"
#include "config.h"

#if defined USE_OPENGL && defined HAVE_THEORA

#include "glad/glad.h"
#include "glbuild.h"
#include "polymost.h"
#include "timer.h"

#include <ogg/ogg.h>
#include <theora/theoradec.h>

// One packet, copied out of libogg's transient buffers.
struct KexPkt
{
    uint8_t *data;
    long len;
    int64_t granule;
};

static void Kex_DrawFrameGL(GLuint tex, uint8_t const *rgb, int w, int h,
                            int firstFrame)
{
    // Everything here is per-frame on purpose: videoNextPage() runs polymost's
    // 2D flush, which re-binds its palette program and its matrices between
    // our frames, and anything set once before the loop is gone by the second
    // frame.
    glUseProgram(0);
    glViewport(0, 0, xdim, ydim);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glLoadIdentity();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    if (firstFrame)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, rgb);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB,
                        GL_UNSIGNED_BYTE, rgb);

    float const vid = (float)w / h;
    float const scr = (float)xdim / ydim;
    float x = 1.f, y = 1.f;
    if (vid < scr) x = vid / scr;      // pillarbox
    else if (vid > scr) y = scr / vid; // letterbox

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glColor3f(1.f, 1.f, 1.f);
    glTexCoord2f(0.f, 1.f); glVertex3f(-x, -y, 0.f);
    glTexCoord2f(0.f, 0.f); glVertex3f(-x,  y, 0.f);
    glTexCoord2f(1.f, 0.f); glVertex3f( x,  y, 0.f);
    glTexCoord2f(1.f, 1.f); glVertex3f( x, -y, 0.f);
    glEnd();
}

// BT.601 integer conversion of one decoded picture into packed RGB. The
// chroma planes are subsampled per ti.pixel_fmt: halved both ways for 4:2:0,
// only horizontally for 4:2:2, not at all for 4:4:4.
static void Kex_YuvToRgb(th_ycbcr_buffer yb, th_info const &ti, uint8_t *rgb)
{
    int const w = ti.pic_width, h = ti.pic_height;
    int const ox = ti.pic_x, oy = ti.pic_y;
    int const hs = (ti.pixel_fmt == TH_PF_444) ? 0 : 1;
    int const vs = (ti.pixel_fmt == TH_PF_420) ? 1 : 0;
    for (int y = 0; y < h; y++)
    {
        uint8_t const *yp = yb[0].data + (y + oy) * yb[0].stride + ox;
        uint8_t const *up = yb[1].data + ((y + oy) >> vs) * yb[1].stride + (ox >> hs);
        uint8_t const *vp = yb[2].data + ((y + oy) >> vs) * yb[2].stride + (ox >> hs);
        uint8_t *out = rgb + y * w * 3;
        for (int x = 0; x < w; x++)
        {
            int const Y = 298 * (yp[x] - 16);
            int const U = up[x >> hs] - 128;
            int const V = vp[x >> hs] - 128;
            int r = (Y + 409 * V + 128) >> 8;
            int g = (Y - 100 * U - 208 * V + 128) >> 8;
            int b = (Y + 516 * U + 128) >> 8;
            out[0] = (uint8_t)clamp(r, 0, 255);
            out[1] = (uint8_t)clamp(g, 0, 255);
            out[2] = (uint8_t)clamp(b, 0, 255);
            out += 3;
        }
    }
}

int Kex_PlayTheora(char const *fileName)
{
    if (videoGetRenderMode() == REND_CLASSIC)
        return 0;

    buildvfs_kfd hFile = kopen4loadfrommod(fileName, 0);
    if (hFile == buildvfs_kfd_invalid)
        return 0;
    int const fileSize = kfilelength(hFile);
    uint8_t *const file = (uint8_t *)Xmalloc(fileSize);
    if (kread(hFile, file, fileSize) != fileSize)
    {
        kclose(hFile); Xfree(file); return 0;
    }
    kclose(hFile);

    // ---- pass 1: split the container. The Theora stream's packets are
    // copied out; the Vorbis stream's raw pages are appended verbatim into a
    // standalone in-memory .ogg for the mixer.
    ogg_sync_state oy;
    ogg_sync_init(&oy);
    {
        char *buf = ogg_sync_buffer(&oy, fileSize);
        Bmemcpy(buf, file, fileSize);
        ogg_sync_wrote(&oy, fileSize);
    }

    ogg_stream_state vstream, astream;
    int haveV = 0, haveA = 0;
    uint8_t *abuf = (uint8_t *)Xmalloc(fileSize);
    int alen = 0;

    KexPkt *pkts = NULL;
    int npkts = 0, cappkts = 0;

    ogg_page pg;
    while (ogg_sync_pageout(&oy, &pg) == 1)
    {
        int const serial = ogg_page_serialno(&pg);
        if (ogg_page_bos(&pg))
        {
            ogg_stream_state probe;
            ogg_stream_init(&probe, serial);
            ogg_stream_pagein(&probe, &pg);
            ogg_packet op;
            int kind = 0;
            if (ogg_stream_packetpeek(&probe, &op) == 1 && op.bytes >= 7)
            {
                if (!Bmemcmp(op.packet, "\x80theora", 7)) kind = 1;
                else if (!Bmemcmp(op.packet, "\x01vorbis", 7)) kind = 2;
            }
            if (kind == 1 && !haveV) { vstream = probe; haveV = 1; }
            else if (kind == 2 && !haveA) { astream = probe; haveA = 1; }
            else { ogg_stream_clear(&probe); if (kind != 2) continue; }
            if (kind == 2)
            {
                Bmemcpy(abuf + alen, pg.header, pg.header_len); alen += (int)pg.header_len;
                Bmemcpy(abuf + alen, pg.body, pg.body_len); alen += (int)pg.body_len;
            }
            if (kind == 1)
            {
                ogg_packet op2;
                while (ogg_stream_packetout(&vstream, &op2) == 1)
                {
                    if (npkts == cappkts)
                    {
                        cappkts = cappkts ? cappkts * 2 : 512;
                        pkts = (KexPkt *)Xrealloc(pkts, cappkts * sizeof(KexPkt));
                    }
                    pkts[npkts].data = (uint8_t *)Xmalloc(op2.bytes);
                    Bmemcpy(pkts[npkts].data, op2.packet, op2.bytes);
                    pkts[npkts].len = op2.bytes;
                    pkts[npkts].granule = op2.granulepos;
                    npkts++;
                }
            }
            continue;
        }
        if (haveA && serial == astream.serialno)
        {
            Bmemcpy(abuf + alen, pg.header, pg.header_len); alen += (int)pg.header_len;
            Bmemcpy(abuf + alen, pg.body, pg.body_len); alen += (int)pg.body_len;
        }
        else if (haveV && serial == vstream.serialno)
        {
            ogg_stream_pagein(&vstream, &pg);
            ogg_packet op;
            while (ogg_stream_packetout(&vstream, &op) == 1)
            {
                if (npkts == cappkts)
                {
                    cappkts = cappkts ? cappkts * 2 : 512;
                    pkts = (KexPkt *)Xrealloc(pkts, cappkts * sizeof(KexPkt));
                }
                pkts[npkts].data = (uint8_t *)Xmalloc(op.bytes);
                Bmemcpy(pkts[npkts].data, op.packet, op.bytes);
                pkts[npkts].len = op.bytes;
                pkts[npkts].granule = op.granulepos;
                npkts++;
            }
        }
    }
    Xfree(file);
    ogg_sync_clear(&oy);

    int played = 0;
    th_info ti; th_comment tc; th_setup_info *ts = NULL;
    th_dec_ctx *td = NULL;
    uint8_t *rgb = NULL;
    int hFx = -1;

    do
    {
        if (!haveV || npkts < 4)
            break;

        // ---- Theora headers: the stream's first three packets.
        th_info_init(&ti);
        th_comment_init(&tc);
        int hdr = 0, i = 0;
        while (i < npkts && hdr >= 0 && hdr < 3)
        {
            ogg_packet op = {};
            op.packet = pkts[i].data; op.bytes = pkts[i].len;
            op.b_o_s = (i == 0);
            if (th_decode_headerin(&ti, &tc, &ts, &op) > 0)
                hdr++, i++;
            else
                hdr = -1;
        }
        if (hdr != 3 || (td = th_decode_alloc(&ti, ts)) == NULL)
            break;

        OSD_Printf("Kex movie: %s %dx%d (frame %dx%d +%d+%d) fmt %d, %d/%d fps%s\n",
                   fileName, ti.pic_width, ti.pic_height,
                   ti.frame_width, ti.frame_height, ti.pic_x, ti.pic_y,
                   (int)ti.pixel_fmt, ti.fps_numerator, ti.fps_denominator,
                   haveA ? ", with audio" : "");
        rgb = (uint8_t *)Xmalloc(ti.pic_width * ti.pic_height * 3);

        GLuint tex = 0;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glClearColor(0.f, 0.f, 0.f, 1.f);

        if (haveA && alen > 0)
            hFx = FX_Play((char *)abuf, alen, -1, 0, 0, MusicVolume,
                          MusicVolume, MusicVolume, FX_MUSIC_PRIORITY,
                          fix16_one, MUSIC_ID + 1);

        double const msPerFrame =
            1000.0 * ti.fps_denominator / max<double>(1.0, ti.fps_numerator);
        uint32_t const t0 = timerGetTicks();
        int frame = 0, firstDraw = 1, skipped = 0;

        KB_FlushKeyboardQueue();
        KB_ClearKeysDown();

        for (; i < npkts && !skipped; i++)
        {
            ogg_packet op = {};
            op.packet = pkts[i].data; op.bytes = pkts[i].len;
            op.granulepos = pkts[i].granule;
            if (th_decode_packetin(td, &op, NULL) != 0)
                continue;

            th_ycbcr_buffer yb;
            if (th_decode_ycbcr_out(td, yb) != 0)
                continue;
            Kex_YuvToRgb(yb, ti, rgb);

            // hold to the stream's own pace; the first dozen frames ignore
            // the keyboard, because window activation at startup leaves
            // phantom events that killed the logos the moment they began
            int const canSkip = frame > 12;
            while ((int32_t)(timerGetTicks() - t0) < (int32_t)(frame * msPerFrame))
            {
                handleevents();
                timerUpdateClock();
                if (!canSkip)
                    KB_FlushKeyboardQueue();
                else if (KB_KeyWaiting() || quitevent)
                    { skipped = 1; break; }
                if (quitevent)
                    { skipped = 1; break; }
            }
            frame++;

            Kex_DrawFrameGL(tex, rgb, ti.pic_width, ti.pic_height, firstDraw);
            firstDraw = 0;
            videoNextPage();
            Exhumed_CaptureFrame();

            handleevents();
            if (!canSkip)
                KB_FlushKeyboardQueue();
            else if (KB_KeyWaiting() || quitevent)
                skipped = 1;
        }

        glDeleteTextures(1, &tex);
        polymost_resetProgram();
        played = 1;
    } while (0);

    if (hFx > 0)
        FX_StopSound(hFx);
    if (td) th_decode_free(td);
    if (ts) th_setup_free(ts);
    th_comment_clear(&tc);
    th_info_clear(&ti);
    if (haveV) ogg_stream_clear(&vstream);
    if (haveA) ogg_stream_clear(&astream);
    for (int k = 0; k < npkts; k++)
        Xfree(pkts[k].data);
    Xfree(pkts);
    Xfree(abuf);
    Xfree(rgb);

    KB_FlushKeyboardQueue();
    KB_ClearKeysDown();
    return played;
}

#else  // !USE_OPENGL || !HAVE_THEORA

int Kex_PlayTheora(char const *fileName)
{
    UNREFERENCED_PARAMETER(fileName);
    return 0;
}

#endif
