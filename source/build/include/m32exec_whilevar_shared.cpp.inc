// Shared Mapster32 while-var immediate conditional VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

case CON_WHILEVARN:
        {
            instype *savedinsptr=insptr+2;
            int32_t j;
            do
            {
                insptr=savedinsptr;
                j = (M32EXEC_GETVAR(*(insptr-1)) != *insptr);
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

        case CON_WHILEVARL:
        {
            instype *savedinsptr=insptr+2;
            int32_t j;
            do
            {
                insptr=savedinsptr;
                j = (M32EXEC_GETVAR(*(insptr-1)) < *insptr);
                VM_DoConditional(j);
            }
            while (j && !vm.flags);
            vm.flags &= ~VMFLAG_BREAK;
            continue;
        }

#undef M32EXEC_GETVAR
