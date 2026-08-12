// Shared Mapster32 quote-string VM cases used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_QSTRLEN:
            insptr++;
            {
                int32_t i=*insptr++, quote=*insptr++;
                const char *quotetext = GetMaybeInlineQuote(quote);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                M32EXEC_SETVAR(i, Bstrlen(quotetext));
                continue;
            }

        case CON_QSUBSTR:
            insptr++;
            {
                int32_t q1 = M32EXEC_GETVAR(*insptr++);
                int32_t q2 = *insptr++;
                const char *q2text = GetMaybeInlineQuote(q2);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(q1, apStrings);

                {
                    int32_t st = M32EXEC_GETVAR(*insptr++);
                    int32_t ln = M32EXEC_GETVAR(*insptr++);
                    char *s1 = apStrings[q1];
                    const char *s2 = q2text;

                    while (*s2 && st--) s2++;
                    while ((*s1 = *s2) && ln--)
                    {
                        s1++;
                        s2++;
                    }
                    *s1=0;
                }
                continue;
            }

        case CON_QSTRNCAT:
        case CON_QSTRCAT:
        case CON_QSTRCPY:
///        case CON_QGETSYSSTR:
            insptr++;
            {
                int32_t i = M32EXEC_GETVAR(*insptr++);
                int32_t j = *insptr++;

                const char *quotetext = GetMaybeInlineQuote(j);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(i, apStrings);

                switch (tw)
                {
                case CON_QSTRCAT:
                    Bstrncat(apStrings[i], quotetext, (MAXQUOTELEN-1)-Bstrlen(apStrings[i]));
                    break;
                case CON_QSTRNCAT:
                    Bstrncat(apStrings[i], quotetext, M32EXEC_GETVAR(*insptr++));
                    break;
                case CON_QSTRCPY:
                    Bstrcpy(apStrings[i], quotetext);
                    break;
                }
                continue;
            }

        case CON_QSPRINTF:
            insptr++;
            {
                int32_t dq=M32EXEC_GETVAR(*insptr++), sq=*insptr++;
                const char *sourcetext = GetMaybeInlineQuote(sq);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                X_ERROR_INVALIDQUOTE(dq, apStrings);

                {
                    int32_t arg[32], numvals=0, i=0, j=0, k=0;
                    int32_t len = Bstrlen(sourcetext);
                    char tmpbuf[MAXQUOTELEN<<1];

                    while (*insptr != -1 && numvals < 32)
                        arg[numvals++] = M32EXEC_GETVAR(*insptr++);

                    insptr++; // skip the NOP

                    i = 0;
                    do
                    {
                        while (k < len && j < MAXQUOTELEN && sourcetext[k] != '%')
                            tmpbuf[j++] = sourcetext[k++];

                        if (sourcetext[k] == '%')
                        {
                            k++;

                            if (i>=numvals) goto dodefault;

                            switch (sourcetext[k])
                            {
                            case 'l':
                                if (sourcetext[k+1] != 'd')
                                {
                                    // write the % and l
                                    tmpbuf[j++] = sourcetext[k-1];
                                    tmpbuf[j++] = sourcetext[k++];
                                    break;
                                }
                                k++;
                                fallthrough__;
                            case 'd':
                            {
                                char buf[16];
                                int32_t ii = 0;

                                Bsprintf(buf, "%d", arg[i++]);

                                ii = Bstrlen(buf);
                                Bmemcpy(&tmpbuf[j], buf, ii);
                                j += ii;
                                k++;
                            }
                            break;

                            case 'f':
                            {
                                char buf[64];
                                int32_t ii = 0;
                                union { int32_t ival; float fval; };
                                ival = arg[i++];

                                Bsprintf(buf, "%f", fval);

                                ii = Bstrlen(buf);
                                Bmemcpy(&tmpbuf[j], buf, ii);
                                j += ii;
                                k++;
                            }
                            break;

                            case 's':
                            {
                                if (arg[i]>=0 && arg[i]<MAXQUOTES && apStrings[arg[i]])
                                {
                                    int32_t ii = Bstrlen(apStrings[arg[i]]);
                                    Bmemcpy(&tmpbuf[j], apStrings[arg[i]], ii);
                                    j += ii;
                                }
                                k++;
                            }
                            break;

dodefault:
                            default:
                                tmpbuf[j++] = sourcetext[k-1];
                                break;
                            }
                        }
                    }
                    while (k < len && j < MAXQUOTELEN);

                    tmpbuf[j] = '\0';
                    Bmemcpy(apStrings[dq], tmpbuf, MAXQUOTELEN);
                    apStrings[dq][MAXQUOTELEN-1] = '\0';
                    continue;
                }
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
