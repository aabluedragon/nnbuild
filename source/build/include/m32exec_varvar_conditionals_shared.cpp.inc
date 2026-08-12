// Shared Mapster32 var-vs-var conditional VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

case CON_IFVARVARAND:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j &= M32EXEC_GETVAR(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVAROR:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j |= M32EXEC_GETVAR(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARXOR:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j ^= M32EXEC_GETVAR(*insptr++);
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVAREITHER:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                int32_t l = M32EXEC_GETVAR(*insptr++);
                insptr--;
                VM_DoConditional(j || l);
            }
            continue;

        case CON_IFVARVARBOTH:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                int32_t l = M32EXEC_GETVAR(*insptr++);
                insptr--;
                VM_DoConditional(j && l);
            }
            continue;

        case CON_IFVARVARN:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j != M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARE:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j == M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARG:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j > M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARGE:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j >= M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARL:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j < M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARLE:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = (j <= M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARA:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = ((uint32_t)j > (uint32_t)M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARAE:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = ((uint32_t)j >= (uint32_t)M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARB:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = ((uint32_t)j < (uint32_t)M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARVARBE:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                j = ((uint32_t)j <= (uint32_t)M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            continue;

        case CON_IFVARE:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j == *insptr);
            }
            continue;

        case CON_IFVARN:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);
                VM_DoConditional(j != *insptr);
            }
            continue;

#undef M32EXEC_GETVAR
