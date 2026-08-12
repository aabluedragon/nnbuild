const char *SectorEffectorTagText(int32_t lotag)
{
    static char tempbuf[64];

    static const char *tags[] =
    {
        "ROTATED SECTOR",                // 0
        "ROTATION PIVOT",
        "EARTHQUAKE",
        "RANDOM LIGHTS AFTER SHOT OUT",
        "RANDOM LIGHTS",
        "(UNKNOWN)",                     // 5
        "SUBWAY",
        "TRANSPORT",
        "RISING DOOR LIGHTS",
        "LOWERING DOOR LIGHTS",
        "DOOR CLOSE DELAY",              // 10
        "SWING DOOR PIVOT (ST 23)",
        "LIGHT SWITCH",
        "EXPLOSIVE",
        "SUBWAY CAR",
        "SLIDE DOOR (ST 25)",            // 15
        "ROTATE REACTOR SECTOR",
        "ELEVATOR TRANSPORT (ST 15)",
        "INCREMENTAL SECTOR RISE/FALL",
        "CEILING FALL ON EXPLOSION",
        "BRIDGE (ST 27)",                // 20
        "DROP FLOOR (ST 28)",
        "TEETH DOOR (ST 29)",
        "1-WAY TRANSPORT DESTINATION",
        "CONVEYOR BELT",
        "ENGINE",                        // 25
        "(UNKNOWN)",
        "DEMO CAMERA",
        "LIGHTNING (4890) CONTROLLER",
        "FLOAT",
        "2 WAY TRAIN (ST 31)",           // 30
        "FLOOR Z",
        "CEILING Z",
        "EARTHQUAKE DEBRIS",
    };

    Bmemset(tempbuf,0,sizeof(tempbuf));

    if (lotag>=0 && lotag<(int32_t)ARRAY_SIZE(tags))
        Bsprintf(tempbuf, "%s", tags[lotag]);
    else
        switch (lotag)
        {
        case 36:
            Bsprintf(tempbuf,"SHOOTER");
            break;
        case 49:
            Bsprintf(tempbuf,"POINT LIGHT");
            break;
        case 50:
            Bsprintf(tempbuf,"SPOTLIGHT");
            break;
        default:
            Bsprintf(tempbuf,"%d: (UNKNOWN)",lotag);
            break;
        }

    return tempbuf;
}

const char *MusicAndSFXTagText(int32_t lotag)
{
    static char tempbuf[16];

    Bmemset(tempbuf, 0, sizeof(tempbuf));

    if (g_numsounds <= 0)
        return tempbuf;

    if (lotag>0 && lotag<999 && g_sounds[lotag].definedname)
        return g_sounds[lotag].definedname;

    if (lotag>=1000 && lotag<2000)
        Bsprintf(tempbuf, "REVERB");
    return tempbuf;
}

const char *SectorEffectorText(int32_t spritenum)
{
    static char tempbuf[64];

    Bmemset(tempbuf, 0, sizeof(tempbuf));
    Bmemset(lo, 0, sizeof(lo));

    Bstrcpy(lo, SectorEffectorTagText(sprite[spritenum].lotag));
    if (!lo[5]) // tags are 5 chars or less
        SpriteName(spritenum, tempbuf);
    else
    {
        if (cursprite == spritenum)
            Bsnprintf(tempbuf, sizeof(tempbuf), "SE %d %s", TrackerCast(sprite[spritenum].lotag), lo);
        else Bstrcpy(tempbuf, lo);
    }

    return tempbuf;
}
