// Shared Mapster32 sound VM cases used by games that expose M32 sound helpers.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_IFSOUND:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr);
                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    insptr++;
                    continue;
                }
                VM_DoConditional(S_CheckSoundPlaying(vm.spriteNum,j));
            }
            continue;

        case CON_IFNOSOUNDS:
            VM_DoConditional(S_SoundsPlaying(vm.spriteNum) < 0);
        continue;

        case CON_GETSOUNDFLAGS:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++), var=*insptr++;
                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    insptr++;
                    continue;
                }

                M32EXEC_SETVAR(var, S_SoundFlags(j));
            }
            continue;

        case CON_SOUNDVAR:
        case CON_STOPSOUNDVAR:
        case CON_SOUNDONCEVAR:
        case CON_GLOBALSOUNDVAR:
            insptr++;
            {
                int32_t j=M32EXEC_GETVAR(*insptr++);

                if (S_InvalidSound(j))
                {
                    M32_ERROR("Invalid sound %d", j);
                    continue;
                }

                switch (tw)
                {
                case CON_SOUNDONCEVAR:
                    if (!S_CheckSoundPlaying(vm.spriteNum,j))
                        A_PlaySound((int16_t)j,vm.spriteNum);
                    break;
                case CON_GLOBALSOUNDVAR:
                    A_PlaySound((int16_t)j,-1);
                    break;
                case CON_STOPSOUNDVAR:
                    if (S_CheckSoundPlaying(vm.spriteNum,j))
                        S_StopSound((int16_t)j);
                    break;
                case CON_SOUNDVAR:
                    A_PlaySound((int16_t)j,vm.spriteNum);
                    break;
                }
            }
            continue;

        case CON_STOPALLSOUNDS:
            insptr++;
            S_StopAllSounds();
            continue;

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
