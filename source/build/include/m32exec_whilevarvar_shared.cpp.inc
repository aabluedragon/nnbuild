// Shared Mapster32 while-var-var conditional VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

case CON_WHILEVARVARN:
        {
            int32_t j;
            instype *savedinsptr=insptr+2;
            do
            {
                insptr=savedinsptr;
                j = M32EXEC_GETVAR(*(insptr-1));
                j = (j != M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_WHILEVARVARL:
        {
            int32_t j;
            instype *savedinsptr=insptr+2;
            do
            {
                insptr=savedinsptr;
                j = M32EXEC_GETVAR(*(insptr-1));
                j = (j < M32EXEC_GETVAR(*insptr++));
                insptr--;
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

#undef M32EXEC_GETVAR
