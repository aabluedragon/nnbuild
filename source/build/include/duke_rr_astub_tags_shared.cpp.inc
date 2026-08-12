// Shared Mapster32 helper tables/tag-label/sector-caption code for Duke3D and Redneck Rampage.
#define M32_NUM_SPRITE_MODES (signed)ARRAY_SIZE(SpriteMode)
static const char *SpriteMode[]=
{
    "NONE",
    "SECTORS",
    "WALLS",
    "SPRITES",
    "ALL",
    "ITEMS ONLY",
    "CURRENT SPRITE ONLY",
    "ONLY SECTOREFFECTORS AND SECTORS",
    "NO SECTOREFFECTORS OR SECTORS"
};

#define MAXSKILL 5
static const char *SKILLMODE[MAXSKILL]=
{
    "Actor skill display: PIECE OF CAKE",
    "Actor skill display: LET'S ROCK",
    "Actor skill display: COME GET SOME",
    "Actor skill display: DAMN I'M GOOD",
    "Actor skill display: ALL SKILL LEVELS"
};

#define MAXNOSPRITES 4
static const char *SPRDSPMODE[MAXNOSPRITES]=
{
    "Sprite display: DISPLAY ALL SPRITES",
    "Sprite display: NO EFFECTORS",
    "Sprite display: NO ACTORS",
    "Sprite display: NO EFFECTORS OR ACTORS"
};

#define MAXHELP3D (signed)ARRAY_SIZE(Help3d)
static const char *Help3d[]=
{
    "Mapster32 3D mode help",
    " ",
    " F2 = TOGGLE CLIPBOARD",
    " F3 = TOGGLE MOUSELOOK",
    " F4 = TOGGLE AMBIENT SOUNDS",
    " F6 = AUTOMATIC SECTOREFFECTOR HELP",
    " F7 = AUTOMATIC SECTOR TAG HELP",
    "",
    " ' A = TOGGLE AUTOSAVE",
    " ' D = CYCLE SPRITE SKILL DISPLAY",
    " ' R = TOGGLE FRAMERATE DISPLAY",
    " ' W = TOGGLE SPRITE DISPLAY",
    " ' X = MAP SHADE PREVIEW",
    " ' I = TOGGLE INVISIBLE SPRITES",
    "",
    " ' T = CHANGE LOTAG",
    " ' H = CHANGE HITAG",
    " ' S = CHANGE SHADE",
    " ' M = CHANGE EXTRA",
    " ' V = CHANGE VISIBILITY",
    " ' L = CHANGE OBJECT COORDINATES",
    " ' C = CHANGE GLOBAL SHADE",
    "",
    " ' ENTER = PASTE GRAPHIC ONLY",
    " ' P & ; P = PASTE PALETTE TO ALL SELECTED SECTORS",
    " ; V = SET VISIBILITY ON ALL SELECTED SECTORS",
    " ' DEL = CSTAT=0",
    " CTRL-S = SAVE BOARD",
    " HOME = PGUP/PGDN MODIFIER (256 UNITS)",
    " END = PGUP/PGDN MODIFIER (512 UNITS)",
};

const char *ExtGetVer(void)
{
    return s_buildRev;
}

void ExtSetupMapFilename(const char *mapname)
{
    UNREFERENCED_PARAMETER(mapname);
}

void ExtLoadMap(const char *mapname)
{
    UNREFERENCED_PARAMETER(mapname);

    getmessageleng = 0;
    getmessagetimeoff = 0;

    // Old-fashioned multi-psky handling setup.
    G_SetupGlobalPsky();

    parallaxtype = 0;

    //////////
#if M32_UNDO
    map_undoredo_free();
#endif
}

void ExtSaveMap(const char *mapname)
{
    UNREFERENCED_PARAMETER(mapname);
}


////////// tag labeling system //////////

#define TLCHAR "+"
#define TLCHR(Cond) ((Cond)?TLCHAR:"")
static uint64_t taglab_nolink_SEs = (1ull<<10)|(1ull<<27)|(1ull<<28)|(1ull<<29)|
    (1ull<<31)|(1ull<<32)|(1ull<<49)|(1ull<<50);

//// Case lists of switch picnums. Pretty much CODEDUP from sector.c.

// List of switches that function like dip (combination lock) switches.
#define DIPSWITCH_LIKE_CASES \
         DIPSWITCH: \
    case TECHSWITCH: \
    case ALIENSWITCH

// List of access switches.
#define ACCESS_SWITCH_CASES \
         ACCESSSWITCH: \
    case ACCESSSWITCH2

// List of switches that don't fit the two preceding categories, and are not
// the MULTISWITCH. 13 cases.
#define REST_SWITCH_CASES \
         DIPSWITCH2: \
    case DIPSWITCH3: \
    case FRANKENSTINESWITCH: \
    case HANDSWITCH: \
    case LIGHTSWITCH2: \
    case LIGHTSWITCH: \
    case LOCKSWITCH1: \
    case POWERSWITCH1: \
    case POWERSWITCH2: \
    case PULLSWITCH: \
    case SLOTDOOR: \
    case SPACEDOORSWITCH: \
    case SPACELIGHTSWITCH

// Whether the individual tags have linking semantics. Based on
//  http://infosuite.duke4.net/index.php?page=references_special_textures
// The return value is an OR of the following:
//  1: lotag has linking semantics
//  2: hitag
//  4: extra
//  8: xvel
//  16: yvel
//  32: zvel
//  64: owner
// This function is only supposed to say something about the potential of a tag:
// it will also 'say yes' if a particular tag is zero.
int32_t taglab_linktags(int32_t spritep, int32_t num)
{
    int32_t picnum;
    int32_t l, link = 0;

    if (spritep)
        picnum = sprite[num].picnum;
    else
        picnum = wall[num].picnum;

    if (spritep)
    {
        switch (picnum)
        {
        case SECTOREFFECTOR:
            // SEs potentially link by their hitag
            l = sprite[num].lotag;
            if (l>=0 && l<=63 && (taglab_nolink_SEs&(1ull<<l)))
                break;
            link = 2;
            break;

            // various lotag-linkers
            // NOTE: switch picnums are handled together with walls below.
        case ACTIVATOR: case TOUCHPLATE: case ACTIVATORLOCKED: case MASTERSWITCH:
        case RESPAWN:  // ---
        case MULTISWITCH:  // *
        case TARGET: case DUCK:
        case REACTOR:
        case CAMERA1:
            link = 1;
            break;

            // various hitag-linkers
        case VIEWSCREEN2: case VIEWSCREEN:
        case CRACK1: case CRACK2: case CRACK3: case CRACK4: case FIREEXT:
        case FEM1: case FEM2: case FEM3: case FEM4: case FEM5: case FEM6:
        case FEM7: case FEM8: case FEM9: case FEM10: case PODFEM1: case NAKED1: //case STATUE: //?
        case SEENINE: case OOZFILTER:
        case CRANEPOLE: case CRANE:
        case NATURALLIGHTNING:
            link = 2;
            break;
        }
    }
    else  // walls
    {
#ifdef YAX_ENABLE
        // XXX: only for non-VX map versions.
        if (yax_getnextwall(num, YAX_CEILING) < 0)
#endif
        switch (picnum)
        {
        case TECHLIGHT2: case TECHLIGHT4: case WALLLIGHT4:
        case WALLLIGHT3: case WALLLIGHT1: case WALLLIGHT2:
        case BIGFORCE: case W_FORCEFIELD:
            link = 1;
            break;
        }
    }

    if (!link)
#ifdef YAX_ENABLE
    if (spritep || yax_getnextwall(num, YAX_CEILING) < 0)
#endif
    {
        // try a few that work both as sprites and as walls
        switch (picnum)
        {
        case ACCESS_SWITCH_CASES:
        case DIPSWITCH_LIKE_CASES:
        case REST_SWITCH_CASES:
        case DOORTILE5: case DOORTILE6: case DOORTILE1: case DOORTILE2: case DOORTILE3:
        case DOORTILE4: case DOORTILE7: case DOORTILE8: case DOORTILE9: case DOORTILE10:
        case DOORTILE22: case DOORTILE18: case DOORTILE19: case DOORTILE20:
        case DOORTILE14: case DOORTILE16: case DOORTILE15: case DOORTILE21:
        case DOORTILE17: case DOORTILE11: case DOORTILE12: case DOORTILE23:  // ---
            link = 1;
            break;
        }

        // handle 'on' positions of non-access switches
        switch (picnum - 1)
        {
        case DIPSWITCH_LIKE_CASES:
        case REST_SWITCH_CASES:
            link = 1;
            break;
        }

        // handle all positions of the multiswitch
        if (picnum >= MULTISWITCH && picnum <= MULTISWITCH+3)
            link = 1;
    }

    g_iReturnVar = link;
    VM_OnEvent(EVENT_LINKTAGS, spritep?num:-1);
    link = g_iReturnVar;

    return link;
}

// <duetoptr>: if non-NULL, a value will be written denoting the object with
// the currently greatest tag:
//  32768 + spritenum, or
//  wallnum, or
//  -1 (the return value i.e. no more tags left OR there are no tagged objects)
int32_t taglab_getnextfreetag(int32_t *duetoptr)
{
    int32_t i, nextfreetag=1;
    int32_t obj = -1;

    for (i=0; i<MAXSPRITES; i++)
    {
        int32_t tag;

        if (sprite[i].statnum == MAXSTATUS)
            continue;

        if (sprite[i].picnum==MULTISWITCH)
        {
            // MULTISWITCH needs special care
            int32_t endtag = sprite[i].lotag+3;

            if (nextfreetag <= endtag)
            {
                nextfreetag = endtag+1;
                obj = 32768 + i;
            }

            continue;
        }

        tag = select_sprite_tag(i);

        if (tag != INT32_MIN && nextfreetag <= tag)
        {
            nextfreetag = tag+1;
            obj = 32768 + i;
        }
    }

    for (i=0; i<numwalls; i++)
    {
        int32_t lt = taglab_linktags(0, i);

        if ((lt&1) && nextfreetag <= wall[i].lotag)
            nextfreetag = wall[i].lotag+1, obj = i;
        if ((lt&2) && nextfreetag <= wall[i].hitag)
            nextfreetag = wall[i].hitag+1, obj = i;
    }

    if (duetoptr != NULL)
        *duetoptr = obj;

    if (nextfreetag < 32768)
        return nextfreetag;

    return 0;
}


static void taglab_handle1(int32_t linktagp, int32_t tagnum, char *buf)
{
    char const * const label = (linktagp && showtags==2) ? taglab_getlabel(tagnum) : NULL;

    if (label)
        Bsprintf(buf, "%d<%s>", tagnum, label);
    else
        Bsprintf(buf, "%d%s", tagnum, TLCHR(linktagp));
}
////////// end tag labeling system //////////


static int32_t getTileGroup(const char *groupName)
{
    int32_t temp;
    for (temp = 0; temp < MAX_TILE_GROUPS; temp++)
    {
        if (s_TileGroups[temp].szText == NULL)
            return -1;

        if (!Bstrcmp(s_TileGroups[temp].szText, groupName))
            return temp;
    }
    return -1;
}

static int32_t tileInGroup(int32_t group, int32_t tilenum)
{
    // @todo Make a bitmap instead of doing this slow search..
    int32_t temp;
    if (group < 0 || group >= MAX_TILE_GROUPS || s_TileGroups[group].szText == NULL)
    {
        // group isn't valid.
        return 0;
    }
    for (temp=0; temp<s_TileGroups[group].nIds; temp++)
    {
        if (tilenum == s_TileGroups[group].pIds[temp])
            return 1;
    }
    return 0;
}

const char *ExtGetSectorType(int32_t lotag)
{
    switch (lotag)
    {
    case 1: return "WATER";
    case 2: return "UNDERWATER";
    case 9: return "STAR TREK DOORS";
    case 15: return "ELEVATOR TRANSPORT (SE 17)";
    case 16: return "ELEVATOR PLATFORM DOWN";
    case 17: return "ELEVATOR PLATFORM UP";
    case 18: return "ELEVATOR DOWN";
    case 19: return "ELEVATOR UP";
    case 20: return "CEILING DOOR";
    case 21: return "FLOOR DOOR";
    case 22: return "SPLIT DOOR";
    case 23: return "SWING DOOR (SE 11)";
    case 25: return "SLIDE DOOR (SE 15)";
    case 26: return "SPLIT STAR TREK DOOR";
    case 27: return "BRIDGE (SE 20)";
    case 28: return "DROP FLOOR (SE 21)";
    case 29: return "TEETH DOOR (SE 22)";
    case 30: return "ROTATE RISE BRIDGE";
    case 31: return "2 WAY TRAIN (SE=30)";
    case 32767: return "SECRET AREA";
    case -1: return "END OF LEVEL";
    default:
        if (lotag > 10000 && lotag < 32767)
            return "1 TIME SOUND";
//        else Bsprintf(tempbuf,"%hu",lotag);
    }

    return "";
}

const char *ExtGetSectorCaption(int16_t sectnum)
{
    static char tempbuf[64];

    Bmemset(tempbuf, 0, sizeof(tempbuf));

    if (!in3dmode() && ((onnames!=1 && onnames!=4 && onnames!=7) || onnames==8))
        return tempbuf;

    if (in3dmode() || (sector[sectnum].lotag|sector[sectnum].hitag))
    {
        Bstrcpy(lo, ExtGetSectorType(sector[sectnum].lotag));
        if (!in3dmode())
            Bsnprintf(tempbuf, sizeof(tempbuf), "%hu,%hu %s", TrackerCast(sector[sectnum].hitag), TrackerCast(sector[sectnum].lotag), lo);
        else
            Bsnprintf(tempbuf, sizeof(tempbuf), "%hu %s", TrackerCast(sector[sectnum].lotag), lo);
    }
    return tempbuf;
}
