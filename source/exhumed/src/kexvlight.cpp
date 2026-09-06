// VibeBuild32: the remaster's baked light field, for per-vertex shading.
//
// Kex bakes its lighting into the tile vertices -- one sample at every
// 128-unit cell corner of every textured face -- and draws each face as a
// Gouraud mosaic of them: the fire pot's glow falls off across the wall
// behind it, the floor darkens toward the corners. Build has one shade per
// wall and one per floor, which the converter carries by cutting surfaces
// into pieces, and the result is a flat shade per piece with a seam between
// them. kexmap.py writes the samples beside each map as KEX_<NAME>.LIT, with
// their authored RGB values in Build units, and polymost asks here for the
// colour at every polygon vertex it draws in Kex mode.
//
// KEXLIT3 additionally records which source face each emitted Build surface
// represents. KEXLIT4 retains the source tile triangles too, so the query can
// use the remaster's barycentric RGB interpolation rather than estimating it
// from nearby samples. KEXLIT2 and older position-only sidecars retain their
// monochrome lookups.
#include "compat.h"
#include "build.h"
#include "polymost.h"
#include "exhumed.h"
#include "kexbreak.h"

struct KexVLSample { int32_t x, y, z, r, g, b, face; };
struct KexVLFace { int32_t face, first, count, triFirst, triCount; };
struct KexVLSurface { int32_t kind, ident, face; };
struct KexVLTriangle { int32_t face, a, b, c; };

static KexVLSample *s_samples;
static KexVLFace   *s_faces;
static KexVLSurface *s_surfaces;
static KexVLTriangle *s_triangles;
static int32_t     *s_order;       // sample indices sorted by cell
static int32_t     *s_cellStart;   // cell -> first index in s_order, ncells+1 entries
static int          s_n, s_nfaces, s_nsurfaces, s_ntriangles, s_v2;
static int32_t      s_minx, s_miny, s_cw, s_ch;
static char         s_loaded[BMAX_PATH];

#define KEXVL_CELL     (128 * 6) // the baked field's 128-Kex-unit cell, in Build units
#define KEXVL_RADIUS   1150.f    // one cell diagonal plus a small margin, in Build units
#define KEXVL_SOFT     (48.f*48.f)  // softening: a sample a few units away is not infinite
#define KEXVL_SNAP     72.f         // within this, a vertex IS the sample (12 Kex units)

void KexVL_Free(void)
{
    Xfree(s_samples); s_samples = NULL;
    Xfree(s_faces); s_faces = NULL;
    Xfree(s_surfaces); s_surfaces = NULL;
    Xfree(s_triangles); s_triangles = NULL;
    Xfree(s_order); s_order = NULL;
    Xfree(s_cellStart); s_cellStart = NULL;
    s_n = s_nfaces = s_nsurfaces = s_ntriangles = s_v2 = 0; s_loaded[0] = 0;
}

int KexVL_Ready(void) { return s_n > 0; }
static int KexVL_FindFace(int face);

void KexVL_Load(char const *mapfile)
{
    char path[BMAX_PATH];
    Bstrncpyz(path, mapfile, sizeof(path));
    char *dot = Bstrrchr(path, '.');
    if (dot && !Bstrchr(dot, '/')) *dot = 0;
    Bstrncat(path, ".LIT", sizeof(path) - Bstrlen(path) - 1);
    if (!Bstrcasecmp(path, s_loaded))
        return;
    KexVL_Free();

    buildvfs_kfd h = kopen4loadfrommod(path, 0);
    if (h == buildvfs_kfd_invalid)
        return;
    int32_t const len = kfilelength(h);
    char *buf = (char *)Xmalloc(len + 1);
    kread(h, buf, len); buf[len] = 0; kclose(h);

    char *p = buf;
    int const v4 = !Bstrncmp(p, "KEXLIT4", 7)
        && (p[7] == ' ' || p[7] == '\t' || p[7] == '\r' || p[7] == '\n');
    int const v3 = !Bstrncmp(p, "KEXLIT3", 7)
        && (p[7] == ' ' || p[7] == '\t' || p[7] == '\r' || p[7] == '\n');
    int const v2 = !Bstrncmp(p, "KEXLIT2", 7)
        && (p[7] == ' ' || p[7] == '\t' || p[7] == '\r' || p[7] == '\n');
    int const faceaware = v2 || v3 || v4;
    int nSurface = 0, nTriangle = 0;
    int n;
    if (faceaware)
    {
        p += 7;
        n = (int)strtol(p, &p, 10);
        nSurface = (int)strtol(p, &p, 10);
        if (v4)
            nTriangle = (int)strtol(p, &p, 10);
    }
    else
    {
        n = (int)strtol(p, &p, 10);
    }
    if (n <= 0 || (faceaware && (nSurface < 0 || nTriangle < 0))) { Xfree(buf); return; }
    s_samples = (KexVLSample *)Xmalloc(n * sizeof(KexVLSample));
    int cnt = 0;
    int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    while (cnt < n)
    {
        char *q;
        long face = -1;
        if (faceaware)
        {
            face = strtol(p, &q, 10); if (q == p) break; p = q;
        }
        long x = strtol(p, &q, 10); if (q == p) break; p = q;
        long y = strtol(p, &q, 10); if (q == p) break; p = q;
        long z = strtol(p, &q, 10); if (q == p) break; p = q;
        long r = strtol(p, &q, 10); if (q == p) break; p = q;
        long g = r, b = r;
        if (v3 || v4)
        {
            g = strtol(p, &q, 10); if (q == p) break; p = q;
            b = strtol(p, &q, 10); if (q == p) break; p = q;
        }
        KexVLSample &s = s_samples[cnt++];
        s.x = (int32_t)x; s.y = (int32_t)y; s.z = (int32_t)z;
        s.r = (int32_t)r; s.g = (int32_t)g; s.b = (int32_t)b;
        s.face = (int32_t)face;
        minx = min(minx, s.x); maxx = max(maxx, s.x); miny = min(miny, s.y); maxy = max(maxy, s.y);
    }
    if (!cnt) { Xfree(buf); KexVL_Free(); return; }
    s_n = cnt;
    if (faceaware)
    {
        // write_lit() orders samples by source face and surface rows by their
        // (kind, identity) lookup key. Preserve that compact representation:
        // even the largest retail face has only 238 sample corners.
        s_faces = (KexVLFace *)Xmalloc(cnt * sizeof(KexVLFace));
        for (int i = 0; i < cnt; )
        {
            int const first = i, face = s_samples[i].face;
            while (i < cnt && s_samples[i].face == face)
                i++;
            s_faces[s_nfaces++] = { face, first, i - first, 0, 0 };
        }
        s_surfaces = (KexVLSurface *)Xmalloc(nSurface * sizeof(KexVLSurface));
        while (s_nsurfaces < nSurface)
        {
            char *q;
            long kind = strtol(p, &q, 10); if (q == p) break; p = q;
            long ident = strtol(p, &q, 10); if (q == p) break; p = q;
            long face = strtol(p, &q, 10); if (q == p) break; p = q;
            s_surfaces[s_nsurfaces++] = { (int32_t)kind, (int32_t)ident, (int32_t)face };
        }
        if (v4)
        {
            if (nTriangle)
                s_triangles = (KexVLTriangle *)Xmalloc(nTriangle * sizeof(KexVLTriangle));
            while (s_ntriangles < nTriangle)
            {
                char *q;
                long face = strtol(p, &q, 10); if (q == p) break; p = q;
                long a = strtol(p, &q, 10); if (q == p) break; p = q;
                long b = strtol(p, &q, 10); if (q == p) break; p = q;
                long c = strtol(p, &q, 10); if (q == p) break; p = q;
                if (a < 0 || a >= s_n || b < 0 || b >= s_n || c < 0 || c >= s_n
                    || s_samples[a].face != face || s_samples[b].face != face
                    || s_samples[c].face != face)
                    break;
                s_triangles[s_ntriangles++] = { (int32_t)face, (int32_t)a, (int32_t)b, (int32_t)c };
            }
            for (int i = 0; i < s_ntriangles; )
            {
                int const first = i, face = s_triangles[i].face;
                while (i < s_ntriangles && s_triangles[i].face == face)
                    i++;
                int const entry = KexVL_FindFace(face);
                if (entry < 0)
                    break;
                s_faces[entry].triFirst = first;
                s_faces[entry].triCount = i - first;
            }
        }
        s_v2 = s_nsurfaces == nSurface && (!v4 || s_ntriangles == nTriangle);
        if (!s_v2)
        {
            KexVL_Free();
            Xfree(buf);
            return;
        }
        Xfree(buf);
        Bstrncpyz(s_loaded, path, sizeof(s_loaded));
        if (v4)
            OSD_Printf("Kex light field: %d face samples, %d surfaces, %d tile triangles from %s\n",
                       s_n, s_nsurfaces, s_ntriangles, path);
        else
            OSD_Printf("Kex light field: %d face samples, %d surfaces from %s\n",
                       s_n, s_nsurfaces, path);
        return;
    }
    Xfree(buf);
    s_minx = minx; s_miny = miny;
    s_cw = (maxx - minx) / KEXVL_CELL + 1;
    s_ch = (maxy - miny) / KEXVL_CELL + 1;
    int const ncells = s_cw * s_ch;
    s_cellStart = (int32_t *)Xcalloc(ncells + 1, sizeof(int32_t));
    for (int i = 0; i < s_n; i++)
    {
        int const c = ((s_samples[i].y - s_miny) / KEXVL_CELL) * s_cw + (s_samples[i].x - s_minx) / KEXVL_CELL;
        s_cellStart[c + 1]++;
    }
    for (int c = 0; c < ncells; c++) s_cellStart[c + 1] += s_cellStart[c];
    s_order = (int32_t *)Xmalloc(s_n * sizeof(int32_t));
    int32_t *fill = (int32_t *)Xcalloc(ncells, sizeof(int32_t));
    for (int i = 0; i < s_n; i++)
    {
        int const c = ((s_samples[i].y - s_miny) / KEXVL_CELL) * s_cw + (s_samples[i].x - s_minx) / KEXVL_CELL;
        s_order[s_cellStart[c] + fill[c]++] = i;
    }
    Xfree(fill);
    Bstrncpyz(s_loaded, path, sizeof(s_loaded));
    OSD_Printf("Kex light field: %d samples from %s\n", s_n, path);
}

// Kex multiplies the texture by the vertex colour, so a vertex at luminance
// L shows at L/255 of the texture -- and polymost's shade factor is
// (numshades - shade)/numshades, so shade = numshades * (1 - L/255) says the
// same. The converter's per-piece curve lifts the dim end with a square root
// (a piece is an average, and a flat piece crushed to black reads worse than
// one too light); a vertex does not need it, and the remaster is darker
// than that curve at the low end. VB_KEX_LIGHTCURVE=<gamma> raises L/255 to
// a power for comparison: 1 is Kex's own, 0.5 the converter's look.
static float s_gamma = -1.f;

static inline float KexVL_Factor(float value)
{
    if (s_gamma < 0.f)
    {
        char const *const env = Bgetenv("VB_KEX_LIGHTCURVE");
        s_gamma = env ? (float)Batof(env) : 1.f;
        if (s_gamma <= 0.f) s_gamma = 1.f;
    }
    float f = clamp(value, 0.f, 255.f) / 255.f;
    if (s_gamma != 1.f) f = powf(f, s_gamma);
    return f;
}

static inline int32_t KexVL_ShadeOfLum(float lum)
{
    float const f = KexVL_Factor(lum);
    int const ns = numshades > 0 ? numshades : 64;
    return clamp((int32_t)Blrintf(ns * (1.f - f)), -128, ns - 2);
}

static int KexVL_FindFace(int face)
{
    int lo = 0, hi = s_nfaces;
    while (lo < hi)
    {
        int const mid = (lo + hi) >> 1;
        if (s_faces[mid].face < face) lo = mid + 1;
        else hi = mid;
    }
    return lo < s_nfaces && s_faces[lo].face == face ? lo : -1;
}

static int KexVL_FindSurface(int kind, int ident)
{
    int lo = 0, hi = s_nsurfaces;
    while (lo < hi)
    {
        int const mid = (lo + hi) >> 1;
        KexVLSurface const &s = s_surfaces[mid];
        if (s.kind < kind || (s.kind == kind && s.ident < ident)) lo = mid + 1;
        else hi = mid;
    }
    return lo < s_nsurfaces && s_surfaces[lo].kind == kind
        && s_surfaces[lo].ident == ident ? lo : -1;
}

static int KexVL_TriangleColor(int face, float x, float y, float z, float color[3])
{
    int const entry = KexVL_FindFace(face);
    if (entry < 0 || s_faces[entry].triCount <= 0)
        return 0;

    KexVLFace const &f = s_faces[entry];
    for (int i = f.triFirst; i < f.triFirst + f.triCount; i++)
    {
        KexVLTriangle const &t = s_triangles[i];
        KexVLSample const &a = s_samples[t.a];
        KexVLSample const &b = s_samples[t.b];
        KexVLSample const &c = s_samples[t.c];

        float const abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
        float const acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
        float const nx = aby * acz - abz * acy;
        float const ny = abz * acx - abx * acz;
        float const nz = abx * acy - aby * acx;
        float const anx = fabsf(nx), any = fabsf(ny), anz = fabsf(nz);

        // Project onto the triangle's largest plane.  This works equally for
        // floors, vertical walls and the source's sloped cells, without
        // assuming the Build approximation lies exactly on that source plane.
        float ax, ay, bx, by, cx, cy, px, py;
        if (anx >= any && anx >= anz)
        {
            ax = a.y; ay = a.z; bx = b.y; by = b.z; cx = c.y; cy = c.z; px = y; py = z;
        }
        else if (any >= anz)
        {
            ax = a.x; ay = a.z; bx = b.x; by = b.z; cx = c.x; cy = c.z; px = x; py = z;
        }
        else
        {
            ax = a.x; ay = a.y; bx = b.x; by = b.y; cx = c.x; cy = c.y; px = x; py = y;
        }
        float const det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (fabsf(det) < 1e-6f)
            continue;
        float const u = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
        float const v = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
        float const w = 1.f - u - v;
        if (u < -.01f || v < -.01f || w < -.01f)
            continue;
        color[0] = u * a.r + v * b.r + w * c.r;
        color[1] = u * a.g + v * b.g + w * c.g;
        color[2] = u * a.b + v * b.b + w * c.b;
        return 1;
    }
    return 0;
}

static int KexVL_FaceColor(int face, float x, float y, float z, float color[3])
{
    int const entry = KexVL_FindFace(face);
    if (entry < 0)
        return 0;
    if (KexVL_TriangleColor(face, x, y, z, color))
        return 1;
    KexVLFace const &f = s_faces[entry];
    float wsum = 0.f, rsum = 0.f, gsum = 0.f, bsum = 0.f;
    float nearest2 = 1e30f; int nearest = -1;
    float const r2max = KEXVL_RADIUS * KEXVL_RADIUS;
    for (int i = f.first; i < f.first + f.count; i++)
    {
        KexVLSample const &s = s_samples[i];
        float const dx = s.x - x, dy = s.y - y, dz = (s.z - z) * (1.f / 16.f);
        float const d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2max) continue;
        if (d2 < nearest2) { nearest2 = d2; nearest = i; }
        float const w = 1.f / ((d2 + KEXVL_SOFT) * (d2 + KEXVL_SOFT));
        wsum += w; rsum += w * s.r; gsum += w * s.g; bsum += w * s.b;
    }
    if (wsum <= 0.f)
        return 0;
    if (nearest >= 0 && nearest2 < KEXVL_SNAP * KEXVL_SNAP)
    {
        KexVLSample const &s = s_samples[nearest];
        color[0] = (float)s.r; color[1] = (float)s.g; color[2] = (float)s.b;
    }
    else
    {
        color[0] = rsum / wsum; color[1] = gsum / wsum; color[2] = bsum / wsum;
    }
    return 1;
}

static int KexVL_FaceLum(int face, float x, float y, float z, float *lum)
{
    float color[3];
    if (!KexVL_FaceColor(face, x, y, z, color))
        return 0;
    *lum = color[0] * .30f + color[1] * .59f + color[2] * .11f;
    return 1;
}

static int KexVL_CurrentFace(void)
{
    polymost_kexsurf_t const &s = polymost_kexsurf;
    int kind, ident;
    if (s.kind == 2)
    {
        kind = 2;
        ident = s.sectnum * 2 + (s.isceil != 0);
    }
    else if (s.kind == 1 && (unsigned)s.wallnum < (unsigned)numwalls)
    {
        kind = 1;
        int const masked = wall[s.wallnum].overpicnum > 0
            && globalpicnum == wall[s.wallnum].overpicnum;
        ident = s.wallnum * 2 + masked;
    }
    else
        return -1;
    int const entry = KexVL_FindSurface(kind, ident);
    return entry >= 0 ? s_surfaces[entry].face : -1;
}

int KexVL_ColorAt(float x, float y, float z, float color[3])
{
    if (!s_v2)
        return 0;
    float raw[3];
    int const face = KexVL_CurrentFace();
    if (face < 0 || !KexVL_FaceColor(face, x, y, z, raw))
        return 0;
    color[0] = KexVL_Factor(raw[0]);
    color[1] = KexVL_Factor(raw[1]);
    color[2] = KexVL_Factor(raw[2]);
    return 1;
}

int32_t KexVL_ShadeAt(float x, float y, float z, int32_t fallback)
{
    if (!s_n)
        return fallback;
    if (s_v2)
    {
        float lum;
        int const face = KexVL_CurrentFace();
        if (face >= 0 && KexVL_FaceLum(face, x, y, z, &lum))
            return KexVL_ShadeOfLum(lum);
        return fallback;
    }
    int const cx = (int)((x - s_minx) / KEXVL_CELL), cy = (int)((y - s_miny) / KEXVL_CELL);
    float wsum = 0.f, lsum = 0.f, nearest2 = 1e30f; int nearest = -1;
    float const r2max = KEXVL_RADIUS * KEXVL_RADIUS;
    for (int j = cy - 1; j <= cy + 1; j++)
    {
        if (j < 0 || j >= s_ch) continue;
        for (int i = cx - 1; i <= cx + 1; i++)
        {
            if (i < 0 || i >= s_cw) continue;
            int const c = j * s_cw + i;
            for (int k = s_cellStart[c]; k < s_cellStart[c + 1]; k++)
            {
                KexVLSample const &s = s_samples[s_order[k]];
                float const dx = s.x - x, dy = s.y - y, dz = (s.z - z) * (1.f / 16.f);   // z units to world units
                float const d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > r2max) continue;
                if (d2 < nearest2) { nearest2 = d2; nearest = s_order[k]; }
                float const w = 1.f / ((d2 + KEXVL_SOFT) * (d2 + KEXVL_SOFT));   // steep: the nearest corners rule
                wsum += w; lsum += w * s.r;
            }
        }
    }
    if (wsum <= 0.f)
        return fallback;
    // A vertex sitting on a cell corner takes that corner's sample outright:
    // the remaster interpolates within a cell and never across one, and
    // blending the neighbours here flattened the peak of a fire's halo (the
    // wall behind TOMB's pot fell to 0.43 of its lit value six cells out
    // where the remaster falls to 0.35).
    if (nearest >= 0 && nearest2 < KEXVL_SNAP * KEXVL_SNAP)
        return KexVL_ShadeOfLum((float)s_samples[nearest].r);
    return KexVL_ShadeOfLum(lsum / wsum);
}
