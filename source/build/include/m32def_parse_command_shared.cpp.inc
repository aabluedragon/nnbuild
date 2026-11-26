// Shared Mapster32 compiler command dispatcher used by Duke, Blood, and Redneck Rampage.
// Configure game-specific command availability with the same M32DEF_HAS_* feature
// macros used by m32def_compiler_helpers_shared.cpp.inc.

// returns:  0:keep going,  1:done
static int32_t C_ParseCommand(void)
{
    int32_t tw;
//    const char *temptextptr;

    if (g_numCompilerErrors >= ABORTERRCNT || (*textptr == '\0') || (*(textptr+1) == '\0'))
        return 1;

//    if (g_scriptDebug)
//        C_ReportError(-1);

///    if (cs.checkingSwitch > 0)
///        Bsprintf(g_szBuf,"PC(): '%.25s'",textptr); AddLog(g_szBuf);

    tw = C_GetNextKeyword();
    def_tw = tw;
    //    Bsprintf(tempbuf,"%s",keyw[tw]); AddLog(tempbuf);

    if (C_SkipComments())
        return 1;

    switch (tw)
    {
    default:
    case -1:
    case -2:
        return 0; //End

// *** basic commands
    case CON_NULLOP:
        return C_ParseNullopCommand();

    case CON_DEFINE:
        return C_ParseDefineCommand();

    case CON_INCLUDE:
#ifdef M32DEF_HAS_INCLUDEOPTIONAL
    case CON_INCLUDEOPTIONAL:
#endif
        return C_ParseIncludeCommand(tw);

    case CON_DEFSTATE:
        return C_ParseDefStateCommand();

    case CON_ENDS:
        return C_ParseEndStateCommand();


    case CON_COLLECTSECTORS:
    case CON_SORT:
    case CON_STATE:
        return C_ParseSortOrStateCommand(tw);

    case CON_ONEVENT:
        return C_ParseOneventCommand();

    case CON_ENDEVENT:
        return C_ParseEndeventCommand();

// *** control flow
    case CON_ELSE:
        return C_ParseElseBranch(tw);

    case CON_RETURN:
    case CON_BREAK:
        return C_ParseSimpleCommand(tw);

    case CON_SWITCH:
        return C_ParseSwitch();

    case CON_CASE:
        return C_ParseCase();

    case CON_DEFAULT:
        return C_ParseDefault();

    case CON_ENDSWITCH:
        return C_ParseEndSwitch();

    case CON_GETCURRADDRESS:
        return C_ParseSimpleCommand(tw);

    case CON_JUMP:
        return C_ParseSimpleCommand(tw);

    case CON_LEFTBRACE:
        return C_ParseLeftBrace();

    case CON_RIGHTBRACE:
        return C_ParseRightBrace();

// *** more basic commands

    case CON_GAMEVAR:
        return C_ParseGamevarCommand();

// *** arrays
    case CON_GAMEARRAY:
        return C_ParseGamearrayCommand();

    case CON_COPY:
    case CON_SETARRAY:
        return C_ParseArraySetOrCopyCommand(tw);

    case CON_GETARRAYSIZE:
    case CON_RESIZEARRAY:
        return C_ParseArraySizeCommand(tw);

// *** var ops
    case CON_RANDVAR:
    case CON_DISPLAYRANDVAR:
    case CON_SETVAR:
    case CON_ADDVAR:
    case CON_SUBVAR:
    case CON_MULVAR:
    case CON_DIVVAR:
    case CON_MODVAR:
    case CON_ANDVAR:
    case CON_ORVAR:
    case CON_XORVAR:
    case CON_SHIFTVARL:
    case CON_SHIFTVARR:
        return C_ParseVarConstCommand(tw);

// *** varvar ops
    case CON_RANDVARVAR:
    case CON_DISPLAYRANDVARVAR:
    case CON_SETVARVAR:
    case CON_ADDVARVAR:
    case CON_SUBVARVAR:
    case CON_MULVARVAR:
    case CON_DIVVARVAR:
    case CON_MODVARVAR:
    case CON_ANDVARVAR:
    case CON_ORVARVAR:
    case CON_XORVARVAR:
    case CON_SHIFTVARVARL:
    case CON_SHIFTVARVARR:
        return C_ParseVarVarCommand();

    case CON_SIN:
    case CON_COS:
        return C_ParseMathCommand(tw);

// *** random
    case CON_DISPLAYRAND:
        return C_ParseMathCommand(tw);

// *** float access: convert float to int and back
    case CON_ITOF:
    case CON_FTOI:
        return C_ParseMathCommand(tw);

// *** other math
    case CON_CLAMP:
    case CON_INV:
    case CON_SQRT:
    case CON_GETSPRITELINKTYPE:
    case CON_MULSCALE:
    case CON_DIVSCALE:
    case CON_SCALEVAR:
    case CON_DIST:
    case CON_LDIST:
    case CON_GETANGLE:
    case CON_GETINCANGLE:
    case CON_A2XY:
    case CON_AH2XYZ:
        return C_ParseMathCommand(tw);

    case CON_FOR:  // special-purpose iteration
        return C_ParseForCommand();

// *** if&while var&varvar
    case CON_IFVARL:
    case CON_IFVARLE:
    case CON_IFVARG:
    case CON_IFVARGE:
    case CON_IFVARE:
    case CON_IFVARN:
    case CON_IFVARAND:
    case CON_IFVAROR:
    case CON_IFVARXOR:
    case CON_IFVAREITHER:
    case CON_IFVARBOTH:
#ifdef M32DEF_HAS_WHILEVARE
    case CON_WHILEVARE:
#endif
    case CON_WHILEVARN:
    case CON_WHILEVARL:
// ---
    case CON_IFVARVARL:
    case CON_IFVARVARLE:
    case CON_IFVARVARG:
    case CON_IFVARVARGE:
    case CON_IFVARVARE:
    case CON_IFVARVARN:
    case CON_IFVARVARAND:
    case CON_IFVARVAROR:
    case CON_IFVARVARXOR:
    case CON_IFVARVAREITHER:
    case CON_IFVARVARBOTH:
#ifdef M32DEF_HAS_WHILEVARVARE
    case CON_WHILEVARVARE:
#endif
    case CON_WHILEVARVARN:
    case CON_WHILEVARVARL:
// ---
    case CON_IFHITKEY:
    case CON_IFHOLDKEY:
    case CON_IFRND:
// vvv if* using current sprite
    case CON_IFANGDIFFL:
    case CON_IFSPRITEPAL:
    case CON_IFHIGHLIGHTED:
    case CON_IFACTOR:
#ifdef M32DEF_HAS_IFSOUND
    case CON_IFSOUND:
#endif
    case CON_IFPDISTL:
    case CON_IFPDISTG:
///    case CON_IFGAPZL:
///    case CON_IFFLOORDISTL:
///    case CON_IFCEILINGDISTL:
// ---
    case CON_IFINSIDE:
// ---
    case CON_IFEITHERALT:
    case CON_IFEITHERCTRL:
    case CON_IFEITHERSHIFT:
    case CON_IFAWAYFROMWALL:
    case CON_IFCANSEE:
    case CON_IFONWATER:
    case CON_IFINWATER:
    case CON_IFOUTSIDE:
///    case CON_IFHITSPACE:
///    case CON_IFINSPACE:
///    case CON_IFINOUTERSPACE:
///    case CON_IFCANSEETARGET:
#ifdef M32DEF_HAS_IFNOSOUNDS
    case CON_IFNOSOUNDS:
#endif
    case CON_IFIN3DMODE:
#ifdef M32DEF_HAS_IFIN2D3DMODE
    case CON_IFIN2D3DMODE:
#endif
    case CON_IFAIMINGSPRITE:
    case CON_IFAIMINGWALL:
    case CON_IFAIMINGSECTOR:
    case CON_IFINTERACTIVE:
        return C_ParseIfWhileCommand(tw);

// *** BUILD functions
    case CON_TDUPSPRITE:
    case CON_RESETKEY:
    case CON_SETKEY:
    case CON_INSERTSPRITE:
    case CON_DUPSPRITE:
    case CON_DELETESPRITE:
    case CON_LASTWALL:
    case CON_UPDATECURSECTNUM:
    case CON_UPDATESECTOR:
    case CON_UPDATESECTORZ:
#ifdef M32DEF_HAS_UPDATESECTOR_NEIGHBOR
    case CON_UPDATESECTORNEIGHBOR:
    case CON_UPDATESECTORNEIGHBORZ:
#endif
        return C_ParseBuildCommand(tw);

    case CON_GETZRANGE:
    case CON_CALCHYPOTENUSE:
    case CON_CLIPMOVE:
    case CON_LINEINTERSECT:
    case CON_RAYINTERSECT:
    case CON_HITSCAN:
    case CON_CANSEE:
    case CON_CANSEESPR:
    case CON_NEARTAG:
    case CON_ROTATEPOINT:
    case CON_DRAGPOINT:
    case CON_GETFLORZOFSLOPE:
    case CON_GETCEILZOFSLOPE:
    case CON_ALIGNCEILSLOPE:
    case CON_ALIGNFLORSLOPE:
    case CON_BSETSPRITE:  // was CON_SETSPRITE
    case CON_SETFIRSTWALL:
    case CON_CHANGESPRITESTAT:
    case CON_CHANGESPRITESECT:
    case CON_HEADSPRITESTAT:
    case CON_PREVSPRITESTAT:
    case CON_NEXTSPRITESTAT:
    case CON_HEADSPRITESECT:
    case CON_PREVSPRITESECT:
    case CON_NEXTSPRITESECT:
    case CON_SECTOROFWALL:
    case CON_FIXREPEATS:
        return C_ParseGeometryCommand(tw);

    case CON_GETCLOSESTCOL:
        return C_ParseSimpleCommand(tw);

// *** stuff
#ifdef M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
    case CON_XFLIPHIGHLIGHTEDSECTORS:
    case CON_YFLIPHIGHLIGHTEDSECTORS:
    case CON_XMIRRORHIGHLIGHTEDSECTORS:
    case CON_YMIRRORHIGHLIGHTEDSECTORS:
        return C_ParseSimpleCommand(tw);
#endif

    case CON_SETHIGHLIGHT:
        return C_ParseSimpleCommand(tw);

    case CON_SETHIGHLIGHTSECTOR:
        return C_ParseSimpleCommand(tw);

    case CON_ADDLOGVAR:
        return C_ParseSimpleCommand(tw);

    case CON_UPDATEHIGHLIGHT:
    case CON_UPDATEHIGHLIGHTSECTOR:
    case CON_ADDLOG:
        return C_ParseSimpleCommand(tw);

    case CON_DEBUG:
        return C_ParseSimpleCommand(tw);

// *** strings
    case CON_DEFINEQUOTE:
    case CON_REDEFINEQUOTE:
        return C_ParseQuoteCommand(tw);

    case CON_PRINT:
    case CON_QUOTE:
    case CON_ERRORINS:
    case CON_PRINTMESSAGE16:
    case CON_PRINTMESSAGE256:
    case CON_PRINTEXT256:
    case CON_PRINTEXT16:
    case CON_DRAWLABEL:
    case CON_GETNUMBER16:
    case CON_GETNUMBER256:
    case CON_GETNUMBERFROMUSER:  // <<retvar>> "quote" <max> <flags>(1|2|4|8|(16))
    case CON_QSPRINTF:
    case CON_QSTRCAT:
    case CON_QSTRCPY:
    case CON_QSTRLEN:
    case CON_QSTRNCAT:
    case CON_QSUBSTR:
        return C_ParseTextCommand(tw);

// *** findnear*
    case CON_FINDNEARSPRITE:
    case CON_FINDNEARSPRITE3D:
    case CON_FINDNEARSPRITEZ:
    case CON_FINDNEARSPRITEVAR:
    case CON_FINDNEARSPRITE3DVAR:
    case CON_FINDNEARSPRITEZVAR:
        return C_ParseFindNearCommand(tw);


    case CON_GETTICKS:
        return C_ParseSimpleCommand(tw);

    case CON_GETTIMEDATE:
        return C_ParseSimpleCommand(tw);

    case CON_SETASPECT:
        return C_ParseSimpleCommand(tw);

    case CON_SPGETLOTAG:
    case CON_SPGETHITAG:
    case CON_SECTGETLOTAG:
    case CON_SECTGETHITAG:
    case CON_GETTEXTUREFLOOR:
    case CON_GETTEXTURECEILING:
#ifdef M32DEF_HAS_SOUND_VAR_COMMANDS
    case CON_STOPALLSOUNDS:
#endif
        return C_ParseSimpleCommand(tw);

    case CON_SETI:
        return C_ParseSimpleCommand(tw);

    case CON_SIZEAT:
        return C_ParseSimpleCommand(tw);

///    case CON_ANGOFF:

#ifdef M32DEF_HAS_SOUND_VAR_COMMANDS
    case CON_GETSOUNDFLAGS:
        return C_ParseSimpleCommand(tw);

    case CON_SOUNDVAR:
    case CON_STOPSOUNDVAR:
    case CON_SOUNDONCEVAR:
    case CON_GLOBALSOUNDVAR:
#endif
    case CON_CSTATOR:
    case CON_SPRITEPAL:
    case CON_CACTOR:
    case CON_CLIPDIST:
        return C_ParseSimpleCommand(tw);

    case CON_CSTAT:
        return C_ParseSimpleCommand(tw);

    case CON_DRAWLINE16:
    case CON_DRAWLINE16B:
    case CON_DRAWLINE16Z:
#ifdef M32DEF_HAS_DRAWLINE256
    case CON_DRAWLINE256:
#endif
    case CON_DRAWCIRCLE16:
    case CON_DRAWCIRCLE16B:
    case CON_DRAWCIRCLE16Z:
        return C_ParseDrawCommand(tw);

    case CON_ROTATESPRITE16:
    case CON_ROTATESPRITE:
    case CON_ROTATESPRITEA:
        return C_ParseDrawCommand(tw);

    case CON_SETGAMEPALETTE:
        return C_ParseSimpleCommand(tw);
    }
    return 0;
}

#undef M32DEF_HAS_INCLUDEOPTIONAL
#undef M32DEF_HAS_WHILEVARE
#undef M32DEF_HAS_WHILEVARVARE
#undef M32DEF_HAS_IFSOUND
#undef M32DEF_HAS_IFNOSOUNDS
#undef M32DEF_HAS_IFIN2D3DMODE
#undef M32DEF_HAS_UPDATESECTOR_NEIGHBOR
#undef M32DEF_HAS_HIGHLIGHTED_SECTOR_FLIPS
#undef M32DEF_HAS_SOUND_VAR_COMMANDS
#undef M32DEF_HAS_DRAWLINE256
