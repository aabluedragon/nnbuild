// Shared Mapster32 struct member label tables used by Duke, RR, and Blood editors.

#ifndef M32DEF_SPRITE_SECT_STAT_READONLY
# define M32DEF_SPRITE_SECT_STAT_READONLY 0
# define M32DEF_UNDEF_SPRITE_SECT_STAT_READONLY
#endif

const memberlabel_t SectorLabels[]=
{
    { "wallptr", SECTOR_WALLPTR, 1, 0, 0 },
    { "wallnum", SECTOR_WALLNUM, 1, 0, 0 },
    { "ceilingz", SECTOR_CEILINGZ, 0, 0, 0 },
    { "floorz", SECTOR_FLOORZ, 0, 0, 0 },
    { "ceilingstat", SECTOR_CEILINGSTAT, 0, 0, 0 },
    { "floorstat", SECTOR_FLOORSTAT, 0, 0, 0 },
    { "ceilingpicnum", SECTOR_CEILINGPICNUM, 0, 0, MAXTILES-1 },
    { "ceilingslope", SECTOR_CEILINGSLOPE, 0, 0, 0},
    { "ceilingshade", SECTOR_CEILINGSHADE, 0, 0, 0 },
    { "ceilingpal", SECTOR_CEILINGPAL, 0, 0, 0 },
    { "ceilingxpanning", SECTOR_CEILINGXPANNING, 0, 0, 0 },
    { "ceilingypanning", SECTOR_CEILINGYPANNING, 0, 0, 0 },
    { "floorpicnum", SECTOR_FLOORPICNUM, 0, 0, MAXTILES-1 },
    { "floorslope", SECTOR_FLOORSLOPE, 0, 0, 0 },
    { "floorshade", SECTOR_FLOORSHADE, 0, 0, 0 },
    { "floorpal", SECTOR_FLOORPAL, 0, 0, 0 },
    { "floorxpanning", SECTOR_FLOORXPANNING, 0, 0, 0 },
    { "floorypanning", SECTOR_FLOORYPANNING, 0, 0, 0 },
    { "visibility", SECTOR_VISIBILITY, 0, 0, 0 },
    { "fogpal", SECTOR_FOGPAL, 0, 0, 0 }, // formerly filler
    { "lotag", SECTOR_LOTAG, 0, 0, 0 },
    { "hitag", SECTOR_HITAG, 0, 0, 0 },
    { "extra", SECTOR_EXTRA, 0, 0, 0 },
// aliases
    { "alignto", SECTOR_FOGPAL, 0, 0, 0 }, // formerly filler
    { "ceilingheinum", SECTOR_CEILINGSLOPE, 0, 0, 0},
    { "floorheinum", SECTOR_FLOORSLOPE, 0, 0, 0},

    { "", -1, 0, 0, 0  }     // END OF LIST
};

const memberlabel_t WallLabels[]=
{
    { "x", WALL_X, 0, -BXY_MAX, BXY_MAX },
    { "y", WALL_Y, 0, -BXY_MAX, BXY_MAX },
    { "point2", WALL_POINT2, 1, 0, 0 },
    { "nextwall", WALL_NEXTWALL, 1, 0, 0 },
    { "nextsector", WALL_NEXTSECTOR, 1, 0, 0 },
    { "cstat", WALL_CSTAT, 0, 0, 0 },
    { "picnum", WALL_PICNUM, 0, 0, MAXTILES-1 },
    { "overpicnum", WALL_OVERPICNUM, 0, 0, MAXTILES-1 },
    { "shade", WALL_SHADE, 0, 0, 0 },
    { "pal", WALL_PAL, 0, 0, 0 },
    { "xrepeat", WALL_XREPEAT, 0, 0, 0 },
    { "yrepeat", WALL_YREPEAT, 0, 0, 0 },
    { "xpanning", WALL_XPANNING, 0, 0, 0 },
    { "ypanning", WALL_YPANNING, 0, 0, 0 },
    { "lotag", WALL_LOTAG, 0, 0, 0 },
    { "hitag", WALL_HITAG, 0, 0, 0 },
    { "extra", WALL_EXTRA, 0, 0, 0 },
    { "", -1, 0, 0, 0  }     // END OF LIST
};

const memberlabel_t SpriteLabels[]=
{
    { "x", SPRITE_X, 0, -BXY_MAX, BXY_MAX },
    { "y", SPRITE_Y, 0, -BXY_MAX, BXY_MAX },
    { "z", SPRITE_Z, 0, 0, 0 },
    { "cstat", SPRITE_CSTAT, 0, 0, 0 },
    { "picnum", SPRITE_PICNUM, 0, 0, MAXTILES-1 },
    { "shade", SPRITE_SHADE, 0, 0, 0 },
    { "pal", SPRITE_PAL, 0, 0, 0 },
    { "clipdist", SPRITE_CLIPDIST, 0, 0, 0 },
    { "blend", SPRITE_BLEND, 0, 0, 0 },
    { "xrepeat", SPRITE_XREPEAT, 0, 0, 0 },
    { "yrepeat", SPRITE_YREPEAT, 0, 0, 0 },
    { "xoffset", SPRITE_XOFFSET, 0, 0, 0 },
    { "yoffset", SPRITE_YOFFSET, 0, 0, 0 },
    { "sectnum", SPRITE_SECTNUM, M32DEF_SPRITE_SECT_STAT_READONLY, 0, 0 },
    { "statnum", SPRITE_STATNUM, M32DEF_SPRITE_SECT_STAT_READONLY, 0, 0 },
    { "ang", SPRITE_ANG, 0, 0, 0 },
    { "owner", SPRITE_OWNER, 0, 0, 0 },
    { "xvel", SPRITE_XVEL, 0, 0, 0 },
    { "yvel", SPRITE_YVEL, 0, 0, 0 },
    { "zvel", SPRITE_ZVEL, 0, 0, 0 },
    { "lotag", SPRITE_LOTAG, 0, 0, 0 },
    { "hitag", SPRITE_HITAG, 0, 0, 0 },
    { "extra", SPRITE_EXTRA, 0, 0, 0 },
    { "", -1, 0, 0, 0 }     // END OF LIST
};

#ifndef POLYMER
# define PR_MAXLIGHTPRIORITY 6
#endif

const memberlabel_t LightLabels[]=
{
    { "x", LIGHT_X, 0, -BXY_MAX, BXY_MAX },
    { "y", LIGHT_Y, 0, -BXY_MAX, BXY_MAX },
    { "z", LIGHT_Z, 0, 0, 0 },
    { "horiz", LIGHT_HORIZ, 0, 0, 0 },
    { "range", LIGHT_RANGE, 0, 0, 0 },
    { "angle", LIGHT_ANGLE, 0, 0, 0 },
    { "faderadius", LIGHT_FADERADIUS, 0, 0, 0 },
    { "radius", LIGHT_RADIUS, 0, 0, 0 },
    { "sector", LIGHT_SECTOR, 0, 0, 0 },
    { "r", LIGHT_R, 0, 0, 255 },
    { "g", LIGHT_G, 0, 0, 255 },
    { "b", LIGHT_B, 0, 0, 255 },
    { "priority", LIGHT_PRIORITY, 0, 0, PR_MAXLIGHTPRIORITY-1 },
    { "tilenum", LIGHT_TILENUM, 0, 0, MAXTILES-1 },
    { "minshade", LIGHT_MINSHADE, 0, -128, 127 },
    { "maxshade", LIGHT_MAXSHADE, 0, -128, 127 },
//
    { "active", LIGHT_ACTIVE, 0, 0, 1 },
    { "", -1, 0, 0, 0 }     // END OF LIST
};

#ifdef M32DEF_UNDEF_SPRITE_SECT_STAT_READONLY
# undef M32DEF_UNDEF_SPRITE_SECT_STAT_READONLY
# undef M32DEF_SPRITE_SECT_STAT_READONLY
#endif
