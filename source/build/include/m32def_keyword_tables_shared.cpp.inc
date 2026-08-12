// Shared Mapster32 compiler keyword tables used by Duke, RR, and Blood editors.
//
// Keep this table in the exact same order as ScriptKeywords_t in m32def_shared.h.
// Individual games expose only the keywords backed by their enabled enum entries.

const tokenmap_t altkeyw[] =
{
    { "#define", CON_DEFINE },
    { "al", CON_ADDLOGVAR },
    { "var", CON_GAMEVAR },
    { "array", CON_GAMEARRAY },
    { "shiftl", CON_SHIFTVARVARL },
    { "shiftr", CON_SHIFTVARVARR },
    { "rand", CON_RANDVARVAR },
    { "set", CON_SETVARVAR },
    { "add", CON_ADDVARVAR },
    { "sub", CON_SUBVARVAR },
    { "mul", CON_MULVARVAR },
    { "div", CON_DIVVARVAR },
    { "mod", CON_MODVARVAR },
    { "and", CON_ANDVARVAR },
    { "or", CON_ORVARVAR },
    { "xor", CON_XORVARVAR },
    { "ifa", CON_IFVARVARA },
    { "ifae", CON_IFVARVARAE },
    { "ifb", CON_IFVARVARB },
    { "ifbe", CON_IFVARVARBE },
    { "ifl", CON_IFVARVARL },
    { "ifle", CON_IFVARVARLE },
    { "ifg", CON_IFVARVARG },
    { "ifge", CON_IFVARVARGE },
    { "ife", CON_IFVARVARE },
    { "ifn", CON_IFVARVARN },
    { "ifand", CON_IFVARVARAND },
    { "ifor", CON_IFVARVAROR },
    { "ifxor", CON_IFVARVARXOR },
    { "ifeither", CON_IFVARVAREITHER },
    { "ifboth", CON_IFVARVARBOTH },
#ifdef M32DEF_HAS_WHILEVARVARE
    { "whilee", CON_WHILEVARVARE },
#endif
    { "whilen", CON_WHILEVARVARN },
    { "whilel", CON_WHILEVARVARL },
};

const char *keyw[] =
{
    "nullop",
    "define",
    "include",
#ifdef M32DEF_HAS_INCLUDEOPTIONAL
    "includeoptional",
#endif
    "defstate",  // *
    "ends",
    "state",
    "onevent",
    "endevent",
    "gamevar",

    "else",
    "return",
    "break",
    "switch",
    "case",
    "default",
    "endswitch",
    "getcurraddress",
    "jump",
    "{",
    "}",
    "gamearray",
    "setarray",
    "getarraysize",
    "resizearray",
    "copy",

    "randvar",
    "displayrandvar",
    "setvar",
    "addvar",
    "subvar",
    "mulvar",
    "divvar",
    "modvar",
    "andvar",
    "orvar",
    "xorvar",
    "shiftvarl",
    "shiftvarr",

    "randvarvar",
    "displayrandvarvar",
    "setvarvar",  // *
    "addvarvar",
    "subvarvar",
    "mulvarvar",
    "divvarvar",
    "modvarvar",
    "andvarvar",
    "orvarvar",
    "xorvarvar",
    "shiftvarvarl",
    "shiftvarvarr",
    "sin",
    "cos",

    "displayrand",

    "itof",
    "ftoi",
    "clamp",
    "inv",  //  inversion function.. not internal
    "sqrt",
    "mulscale",
    "divscale",
    "scalevar",
    "dist",
    "ldist",
    "calchypotenuse",
    "getangle",
    "getincangle",
    "a2xy",
    "ah2xyz",

    "collectsectors",
    "sort",
    "for",  // *

    "ifvarl",
    "ifvarle",
    "ifvarg",
    "ifvarge",
    "ifvara",
    "ifvarae",
    "ifvarb",
    "ifvarbe",
    "ifvare",
    "ifvarn",
    "ifvarand",
    "ifvaror",
    "ifvarxor",
    "ifvareither",
    "ifvarboth",
#ifdef M32DEF_HAS_WHILEVARE
    "whilevare",
#endif
    "whilevarn",
    "whilevarl",

    "ifvarvarl",
    "ifvarvarle",
    "ifvarvarg",
    "ifvarvarge",
    "ifvarvara",
    "ifvarvarae",
    "ifvarvarb",
    "ifvarvarbe",
    "ifvarvare",
    "ifvarvarn",
    "ifvarvarand",
    "ifvarvaror",
    "ifvarvarxor",
    "ifvarvareither",
    "ifvarvarboth",
#ifdef M32DEF_HAS_WHILEVARVARE
    "whilevarvare",
#endif
    "whilevarvarn",
    "whilevarvarl",

    "ifhitkey",
    "ifholdkey",
    "ifrnd",
    "ifangdiffl",
    "ifspritepal",
    "ifhighlighted",
    "ifactor",
#ifdef M32DEF_HAS_IFSOUND
    "ifsound",
#endif
    "ifpdistl",
    "ifpdistg",

    "ifinside",

    "ifeitheralt",
    "ifeitherctrl",
    "ifeithershift",
    "ifawayfromwall",
    "ifcansee",
    "ifonwater",
    "ifinwater",
    "ifoutside",
#ifdef M32DEF_HAS_IFNOSOUNDS
    "ifnosounds",
#endif
    "ifin3dmode",
#ifdef M32DEF_HAS_IFIN2D3DMODE
    "ifin2d3dmode",
#endif
    "ifaimingsprite",
    "ifaimingwall",
    "ifaimingsector",
    "ifinteractive",

// Mostly BUILD functions
    "resetkey",
    "setkey",
    "insertsprite",
    "dupsprite",
    "tdupsprite",
    "deletesprite",
    "getspritelinktype",  // not BUILD
    "lastwall",
    "updatecursectnum",
    "updatesector",
    "updatesectorz",
#ifdef M32DEF_HAS_UPDATESECTOR_NEIGHBOR
    "updatesectorneighbor",
    "updatesectorneighborz",
#endif
    "getzrange",
    "clipmove",
    "lineintersect",
    "rayintersect",
    "hitscan",
    "cansee",
    "canseespr",
    "neartag",
    "rotatepoint",
    "dragpoint",
    "getceilzofslope",
    "getflorzofslope",
    "alignceilslope",
    "alignflorslope",
    "bsetsprite",  // *
    "setfirstwall",
    "changespritestat",
    "changespritesect",
    "headspritestat",
    "prevspritestat",
    "nextspritestat",
    "headspritesect",
    "prevspritesect",
    "nextspritesect",
    "sectorofwall",
    "fixrepeats",
    "getclosestcol",

    "updatehighlight",
    "updatehighlightsector",
    "sethighlight",
    "sethighlightsector",
#ifdef M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
    "xfliphighlightedsectors",
    "yfliphighlightedsectors",
    "xmirrorhighlightedsectors",
    "ymirrorhighlightedsectors",
#endif
    "addlogvar",
    "addlog",
    "debug",

    "definequote",
    "redefinequote",
    "print",
    "quote",
    "error",
    "printmessage16",
    "printmessage256",
    "printext256",
    "printext16",
    "drawlabel",
    "getnumber16",
    "getnumber256",
    "getnumberfromuser",
    "qsprintf",
    "qstrcat",
    "qstrcpy",
    "qstrlen",
//    "qgetsysstr",
    "qstrncat",
    "qsubstr",

    "findnearsprite",
    "findnearspritevar",
    "findnearsprite3d",
    "findnearsprite3dvar",
    "findnearspritez",
    "findnearspritezvar",

    "getticks",
    "gettimedate",
    "setaspect",

    "seti",
    "sizeat",
    "cstat",
    "cstator",
    "clipdist",
    "spritepal",
    "cactor",
    "spgetlotag",
    "spgethitag",
    "sectgetlotag",
    "sectgethitag",
    "gettexturefloor",
    "gettextureceiling",

#ifdef M32DEF_HAS_SOUND_VAR_COMMANDS
    "sound", //var
    "soundonce", //var
    "stopallsounds",
    "stopsound", //var
    "globalsound", //var
    "getsoundflags",
#endif

///    "killit",

    "drawline16",
    "drawline16b",
    "drawline16z",
#ifdef M32DEF_HAS_DRAWLINE256
    "drawline256",
#endif
    "drawcircle16",
    "drawcircle16b",
    "drawcircle16z",
    "rotatespritea",
    "rotatesprite16",
    "rotatesprite",
    "setgamepalette",

    "<null>"
};

const tokenmap_t iter_tokens[] =
{
    { "allsprites", ITER_ALLSPRITES },
    { "allsectors", ITER_ALLSECTORS },
    { "allwalls", ITER_ALLWALLS },
    { "activelights", ITER_ACTIVELIGHTS },
    { "selsprites", ITER_SELSPRITES },
    { "selsectors", ITER_SELSECTORS },
    { "selwalls", ITER_SELWALLS },
    { "drawnsprites", ITER_DRAWNSPRITES },
    { "spritesofsector", ITER_SPRITESOFSECTOR },
    { "loopofwall", ITER_LOOPOFWALL },
    { "wallsofsector", ITER_WALLSOFSECTOR },
    { "range", ITER_RANGE },
// vvv alternatives go here vvv
    { "selspr", ITER_SELSPRITES },
    { "selsec", ITER_SELSECTORS },
    { "lights", ITER_ACTIVELIGHTS },
    { "sprofsec", ITER_SPRITESOFSECTOR },
    { "walofsec", ITER_WALLSOFSECTOR },
    { "", -1 }     // END OF LIST
};
