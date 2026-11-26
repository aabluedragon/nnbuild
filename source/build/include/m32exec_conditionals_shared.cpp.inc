// Shared Mapster32 conditional VM cases used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

case CON_IFVARAND:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j & *insptr);
            }
            continue;

        case CON_IFVAROR:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j | *insptr);
            }
            continue;

        case CON_IFVARXOR:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j ^ *insptr);
            }
            continue;

        case CON_IFVAREITHER:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j || *insptr);
            }
            continue;

        case CON_IFVARBOTH:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j && *insptr);
            }
            continue;

        case CON_IFVARG:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j > *insptr);
            }
            continue;

        case CON_IFVARGE:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j >= *insptr);
            }
            continue;

        case CON_IFVARL:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j < *insptr);
            }
            continue;

        case CON_IFVARLE:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j <= *insptr);
            }
            continue;

        case CON_IFVARA:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional((uint32_t)j > (uint32_t)*insptr);
            }
            continue;

        case CON_IFVARAE:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional((uint32_t)j >= (uint32_t)*insptr);
            }
            continue;

        case CON_IFVARB:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional((uint32_t)j < (uint32_t)*insptr);
            }
            continue;

        case CON_IFVARBE:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional((uint32_t)j <= (uint32_t)*insptr);
            }
            continue;

        case CON_IFRND:
            VM_DoConditional(rnd(M32EXEC_GETVAR(*(++insptr))));
            continue;

        case CON_IFHITKEY:
        case CON_IFHOLDKEY:
        case CON_RESETKEY:
        case CON_SETKEY:
            insptr++;
            {
                int32_t key=M32EXEC_GETVAR(*insptr);
                if (key<0 || key >= (int32_t)ARRAY_SIZE(keystatus))
                {
                    M32_ERROR("Invalid key %d!", key);
                    continue;
                }

                if (tw == CON_IFHITKEY || tw == CON_IFHOLDKEY)
                    VM_DoConditional(keystatus[key]);
                else
                    insptr++;

                if (tw != CON_IFHOLDKEY)
                {
                    if (!(key==0 || key==KEYSC_ESC || key==KEYSC_TILDE || key==KEYSC_gENTER ||
                            key==KEYSC_LALT || key==KEYSC_RALT || key==KEYSC_LCTRL || key==KEYSC_RCTRL ||
                            key==KEYSC_LSHIFT || key==KEYSC_RSHIFT))
                        keystatus[key] = (tw==CON_SETKEY);
                }
            }
            continue;

        case CON_IFEITHERALT:
            VM_DoConditional(keystatus[KEYSC_LALT]||keystatus[KEYSC_RALT]);
            continue;

        case CON_IFEITHERCTRL:
            VM_DoConditional(keystatus[KEYSC_LCTRL]||keystatus[KEYSC_RCTRL]);
            continue;

        case CON_IFEITHERSHIFT:
            VM_DoConditional(keystatus[KEYSC_LSHIFT]||keystatus[KEYSC_RSHIFT]);
            continue;

#undef M32EXEC_GETVAR
