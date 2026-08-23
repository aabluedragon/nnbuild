//-------------------------------------------------------------------------
/*
Copyright (C) 2024 VibeBuild32 contributors

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

#ifndef __kextable_h__
#define __kextable_h__

// The tables kexmap.py writes alongside the converted levels -- kexbreak.txt,
// kexlight.txt, kexfont.txt, kexworld.txt -- are all the same shape: one record
// per line, "#" starting a comment line, fields separated by spaces. This reads
// any of them, and _write_table() in kexmap.py writes what it expects.
//
// `parse` is handed one line, NUL-terminated with the trailing \r already gone,
// and the index of the record it would fill; it returns true when it took one.
// Blank and comment lines never reach it. Reading stops after nMax records --
// pass INT32_MAX for a table that indexes itself rather than filling an array.
// A missing file is not an error, it just means this install was never
// converted, and reads as zero records.
static inline int KexTable_Load(char const *fn, int nMax, bool (*parse)(char *line, int nRec))
{
    int const fil = kopen4loadfrommod(fn, 0);
    if (fil == -1)
        return 0;

    int const len = kfilelength(fil);
    char *buf = (char *)Xmalloc(len + 1);
    kread(fil, buf, len);
    buf[len] = 0;
    kclose(fil);

    int nRecs = 0;
    for (char *p = buf; *p && nRecs < nMax; )
    {
        char *eol = Bstrchr(p, '\n');
        if (eol)
            *eol = 0;
        for (char *cr = p + Bstrlen(p); cr > p && cr[-1] == '\r'; cr--)
            cr[-1] = 0;

        if (*p && *p != '#' && parse(p, nRecs))
            nRecs++;

        if (!eol)
            break;
        p = eol + 1;
    }

    Xfree(buf);
    return nRecs;
}

// Both of the tables keyed on a tile keep it in their first field, so one
// lookup serves them. Returns the table index, or -1 when the tile is not in it.
template <typename T>
static inline int KexTable_FindTile(T const *tab, int nRecs, int picnum)
{
    for (int i = 0; i < nRecs; i++)
        if (tab[i].tile == picnum)
            return i;
    return -1;
}

#endif
