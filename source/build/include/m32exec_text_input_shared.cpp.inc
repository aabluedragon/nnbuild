// Shared Mapster32 quote, text input, and text rendering VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif
#ifndef M32EXEC_PRINTEXT256_ALLOWED
# define M32EXEC_PRINTEXT256_ALLOWED() (1)
#endif
#ifndef M32EXEC_PRINTEXT16_COL
# define M32EXEC_PRINTEXT16_COL(C) ((C) >= 0 ? editorcolors[(C)&255] : ((-(C))&255))
#endif

case CON_REDEFINEQUOTE:
            insptr++;
            {
                int32_t q = *insptr++, i = *insptr++;
                X_ERROR_INVALIDQUOTE(q, apStrings);
                X_ERROR_INVALIDQUOTE(i, apXStrings);
                Bstrcpy(apStrings[q],apXStrings[i]);
                continue;
            }

        case CON_GETNUMBER16:  /* deprecated */
        case CON_GETNUMBER256:  /* deprecated */
        case CON_GETNUMBERFROMUSER:
            insptr++;
            {
                int32_t var=*insptr++, quote=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(quote);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                {
                    int32_t max=M32EXEC_GETVAR(*insptr++);
                    int32_t sign = (tw==CON_GETNUMBERFROMUSER) ? M32EXEC_GETVAR(*insptr++) : (max<=0);
                    char buf[64];  // buffers in getnumber* are 80 bytes long

                    Bstrncpyz(buf, quotetext, sizeof(buf));

                    if (max==0)
                        max = INT32_MAX;
                    else
                        max = klabs(max);

//OSD_Printf("max:%d, sign:%d\n", max, sign);
                    if (tw==CON_GETNUMBERFROMUSER)
                    {
                        M32EXEC_SETVAR(var, in3dmode() ?
                                   getnumber256(quotetext, M32EXEC_GETVAR(var), max, sign) :
                                   getnumber16(quotetext, M32EXEC_GETVAR(var), max, sign));
                    }
                    else if (tw==CON_GETNUMBER16)
                        M32EXEC_SETVAR(var, getnumber16(quotetext, M32EXEC_GETVAR(var), max, sign));
                    else
                        M32EXEC_SETVAR(var, getnumber256(quotetext, M32EXEC_GETVAR(var), max, sign));
                }
            }
            continue;

        case CON_PRINT:
        case CON_QUOTE:
        case CON_ERRORINS:
        case CON_PRINTMESSAGE16:
        case CON_PRINTMESSAGE256:
        case CON_PRINTEXT256:
        case CON_PRINTEXT16:
        case CON_DRAWLABEL:
            insptr++;
            {
                int32_t i=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(i);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                {
                    int32_t x=(tw>=CON_PRINTMESSAGE256)?M32EXEC_GETVAR(*insptr++):0;
                    int32_t y=(tw>=CON_PRINTMESSAGE256)?M32EXEC_GETVAR(*insptr++):0;

                    int32_t col=(tw>=CON_PRINTEXT256)?M32EXEC_GETVAR(*insptr++):0;
                    int32_t backcol=(tw>=CON_PRINTEXT256)?M32EXEC_GETVAR(*insptr++):0;
                    int32_t fontsize=(tw>=CON_PRINTEXT256)?M32EXEC_GETVAR(*insptr++):0;

                    if (tw==CON_PRINT || tw==CON_ERRORINS)
                    {
                        OSD_Printf("%s\n", quotetext);
                        if (tw==CON_ERRORINS)
                            vm.flags |= VMFLAG_ERROR;
                    }
                    else if (tw==CON_QUOTE)
                    {
                        message("%s", quotetext);
                    }
                    else if (tw==CON_PRINTMESSAGE16)
                    {
                        if (!in3dmode())
                            printmessage16("%s", quotetext);
                    }
                    else if (tw==CON_PRINTMESSAGE256)
                    {
                        if (in3dmode())
                            printmessage256(x, y, quotetext);
                    }
                    else if (tw==CON_PRINTEXT256)
                    {
                        if (M32EXEC_PRINTEXT256_ALLOWED())
                        {
                            if (col>=256)
                                col=0;
                            else if (col < 0 && col >= -255)
                                col = editorcolors[-col];

                            if (backcol<0 || backcol>=256)
                                backcol=-1;

                            printext256(x, y, col, backcol, quotetext, fontsize);
                        }
                    }
                    else if (tw==CON_PRINTEXT16)
                    {
                        if (!in3dmode())
                            printext16(x, y, M32EXEC_PRINTEXT16_COL(col), backcol<0 ? -1 : editorcolors[backcol&255],
                                       quotetext, fontsize);
                    }
                    else if (tw==CON_DRAWLABEL)
                    {
                        if (!in3dmode())
                        {
                            drawsmallabel(quotetext,
                                          editorcolors[backcol&255],  // col
                                          fontsize < 0 ? -1 : editorcolors[fontsize&255], editorcolors[fontsize&255] - 3, // backcol
                                          x, y, col);  // x y z
                        }
                    }
                }
            }
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
#undef M32EXEC_PRINTEXT256_ALLOWED
#undef M32EXEC_PRINTEXT16_COL
