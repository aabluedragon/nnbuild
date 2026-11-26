int32_t parsetilegroups(scriptfile *script)
{
    int32_t tokn;
    char *cmdtokptr;

    tokenlist tgtokens[] =
    {
        { "include",         T_INCLUDE          },
        { "#include",        T_INCLUDE          },
        { "define",          T_DEFINE           },
        { "#define",         T_DEFINE           },

        { "dynamicremap",    T_DUMMY            },

        { "tilegroup",       T_TILEGROUP        },
        { "spritehotkey",    T_HOTKEY           },
        { "alphabet",        T_ALPHABET         },
    };

    while (1)
    {
        tokn = getatoken(script,tgtokens,ARRAY_SIZE(tgtokens));
        cmdtokptr = script->ltextptr;
        switch (tokn)
        {
        case T_HOTKEY:
        {
            int32_t i, j;
            if (scriptfile_getsymbol(script,&i)) break;
            if (scriptfile_getsymbol(script,&j)) break;
            if (i < 0 || i > 9 || j < 0 || j >= MAXTILES) break;
            prefixtiles[i] = j;
            break;
        }
        case T_INCLUDE:
        {
            char *fn;
            if (!scriptfile_getstring(script,&fn))
            {
                scriptfile *included;

                included = scriptfile_fromfile(fn);
                if (!included)
                {
                    M32_TILEGROUP_WARN_INCLUDE(fn, script, cmdtokptr);
                }
                else
                {
                    parsetilegroups(included);
                    scriptfile_close(included);
                }
            }
            break;
        }
        case T_DEFINE:
        {
            char *name;
            int32_t number;

            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getsymbol(script,&number)) break;
            if (scriptfile_addsymbolvalue(name,number) < 0)
                M32_TILEGROUP_WARN_DEFINE(name, number, script, cmdtokptr);
            break;
        }
        case T_TILEGROUP:
        {
            char *end, *name;
            int32_t i;

            if (tile_groups >= MAX_TILE_GROUPS) break;
            if (scriptfile_getstring(script,&name)) break;
            if (scriptfile_getbraces(script,&end)) break;

            TileGroup *const tileGrp = &s_TileGroups[tile_groups];

            tileGrp->pIds = (int32_t *)Xcalloc(MAX_TILE_GROUP_ENTRIES, sizeof(int32_t));
            tileGrp->szText = Xstrdup(name);

            while (script->textptr < end)
            {
                static const tokenlist tgtokens2[] =
                {
                    { "tilegroup",  T_TILEGROUP   },
                    { "tile",       T_TILE        },
                    { "tilerange",  T_TILERANGE   },
                    { "hotkey",     T_HOTKEY      },
                    { "tiles",      T_TILES       },
                    { "colors",     T_COLORS      },
                };

                const int32_t token = getatoken(script,tgtokens2,ARRAY_SIZE(tgtokens2));

                switch (token)
                {
                case T_TILE:
                {
                    if (scriptfile_getsymbol(script,&i)) break;
                    if (i >= 0 && i < MAXUSERTILES && tileGrp->nIds < MAX_TILE_GROUP_ENTRIES)
                        tileGrp->pIds[tileGrp->nIds++] = i;
//                    OSD_Printf("added tile %d to group %d\n",i,g);
                    break;
                }
                case T_TILERANGE:
                {
                    int32_t j;
                    if (scriptfile_getsymbol(script,&i)) break;
                    if (scriptfile_getsymbol(script,&j)) break;
                    if (i < 0 || i >= MAXTILES || j < 0 || j >= MAXTILES) break;
                    while (tileGrp->nIds < MAX_TILE_GROUP_ENTRIES && i <= j)
                    {
                        tileGrp->pIds[tileGrp->nIds++] = i++;
//                        OSD_Printf("added tile %d to group %d\n",i,g);
                    }
                    break;
                }
                case T_COLORS:
                {
                    int32_t j;
                    if (scriptfile_getsymbol(script, &i)) break;
                    if (scriptfile_getsymbol(script, &j)) break;
                    if (i < 0 || i >= 256 || j < 0 || j >= 256) break;
                    tileGrp->color1 = i;
                    tileGrp->color2 = j;
                    break;
                }
                case T_HOTKEY:
                {
                    char *c;
                    if (scriptfile_getstring(script,&c)) break;
                    tileGrp->key1 = Btoupper(c[0]);
                    tileGrp->key2 = Btolower(c[0]);
                    break;
                }
                case T_TILES:
                {
                    char *end2;
                    if (scriptfile_getbraces(script,&end2)) break;
                    while (script->textptr < end2-1)
                    {
                        if (!scriptfile_getsymbol(script,&i))
                        {
                            if (i >= 0 && i < MAXTILES && tileGrp->nIds < MAX_TILE_GROUP_ENTRIES)
                                tileGrp->pIds[tileGrp->nIds++] = i;
//                            OSD_Printf("added tile %d to group %d\n",i,g);
                        }
                    }
                    break;
                }
                }
            }

            tileGrp->pIds = (int32_t *)Xrealloc(tileGrp->pIds, tileGrp->nIds*sizeof(int32_t));
            tile_groups++;
            break;
        }
        case T_ALPHABET:
        {
            char *end;
            int32_t i, j, k;

            if (numalphabets >= MAX_ALPHABETS)
            {
                OSD_Printf("Too many alphabet definitions (max: %d).\n", MAX_ALPHABETS);
                break;
            }

            if (scriptfile_getbraces(script,&end)) break;

            for (i=0; i<NUMPRINTABLES; i++)
            {
                alphabets[numalphabets].pic[i] = -1;
                alphabets[numalphabets].xofs[i] = 0;
                alphabets[numalphabets].yofs[i] = 0;
            }

            while (script->textptr < end)
            {
                tokenlist alphtokens2[] =
                {
                    { "map",        T_MAP         },
                    { "mapa",       T_MAPA        },
                    { "maprange",   T_MAPRANGE    },
                    { "maprangea",  T_MAPRANGEA   },
                    { "offset",     T_OFFSET      },
                    { "offseta",    T_OFFSETA     },
                };

                int32_t token = getatoken(script,alphtokens2,ARRAY_SIZE(alphtokens2));
                switch (token)
                {
                case T_MAP:  // map <ascii num> <start tilenum>, e.g. map 46 3002
                {
                    if (scriptfile_getnumber(script,&i)) break;
                    if (scriptfile_getsymbol(script,&j)) break;

                    if (i>=33 && i<=126 && j>= 0 && j<MAXTILES)
                        alphabets[numalphabets].pic[i-33] = j;

                    break;
                }
                case T_MAPA:  // mapa <ascii string> <start tilenum>, e.g. map ".,!?" 3002
                {
                    char *s;
                    if (scriptfile_getstring(script,&s)) break;
                    if (scriptfile_getsymbol(script,&i)) break;

                    for (; *s; s++, i++)
                    {
                        if (*s>=33 && *s<=126 && i>= 0 && i<MAXTILES)
                            alphabets[numalphabets].pic[(*s)-33] = i;
                    }
                    break;
                }
                // maprange <start ascii num> <end ascii num> <start tilenum>, e.g. map 33 126 STARTALPHANUM
                // maprangea <start char> <end char> <start tilenum>, e.g. map "!" "~" STARTALPHANUM
                case T_MAPRANGE:
                case T_MAPRANGEA:
                {
                    if (token==T_MAPRANGE)
                    {
                        if (scriptfile_getnumber(script,&i)) break;
                        if (scriptfile_getnumber(script,&j)) break;
                    }
                    else
                    {
                        char *c1, *c2;
                        if (scriptfile_getstring(script,&c1)) break;
                        if (scriptfile_getstring(script,&c2)) break;
                        i=*c1;
                        j=*c2;
                    }
                    if (scriptfile_getsymbol(script,&k)) break;

                    if (i>126 || j<33) break;
                    for (; i<=j && k<MAXTILES; i++, k++)
                    {
                        if (i>=33 && i<=126)
                            alphabets[numalphabets].pic[i-33] = k;
                    }
                    break;
                }
                case T_OFFSET:  // offset <ascii num> <xoffset> <yoffset>
                {
                    if (scriptfile_getnumber(script, &i)) break;
                    if (scriptfile_getnumber(script, &j)) break;
                    if (scriptfile_getnumber(script, &k)) break;

                    if (i >= 33 && i <= 126)
                    {
                        alphabets[numalphabets].xofs[i-33] = j;
                        alphabets[numalphabets].yofs[i-33] = k;
                    }
                    break;
                }
                case T_OFFSETA:  // offseta <ascii string> <xoffset> <yoffset>
                {
                    char *s;
                    if (scriptfile_getstring(script, &s)) break;
                    if (scriptfile_getnumber(script, &i)) break;
                    if (scriptfile_getnumber(script, &j)) break;

                    for (; *s; s++)
                        if (*s >= 33 && *s <= 126)
                        {
                            alphabets[numalphabets].xofs[(*s)-33] = i;
                            alphabets[numalphabets].yofs[(*s)-33] = j;
                        }
                    break;
                }
                }
            }
            numalphabets++;
            break;
        }
        case T_EOF:
            return 0;
        default:
            break;
        }
    }
    return 0;
}
