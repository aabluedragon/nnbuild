// Shared Mapster32 control-flow and array VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_GETCURRADDRESS:
            insptr++;
            {
                int32_t j=*insptr++;
                M32EXEC_SETVAR(j, insptr-apScript);
            }
            continue;

        case CON_JUMP:
            insptr++;
            {
                int32_t j = M32EXEC_GETVAR(*insptr++);
                if (j<0 || j>=(g_scriptPtr-apScript))
                {
                    M32_ERROR("script index out of bounds (%d)",  j);
                    continue;
                }
                insptr = (instype *)(j+apScript);
            }
            continue;

        case CON_RIGHTBRACE:
            insptr++;
            return 1;
        case CON_LEFTBRACE:
            insptr++;
            VM_Execute(0);
            continue;

// *** arrays
        case CON_SETARRAY:
            insptr++;
            {
                const int32_t j=*insptr++;
                const int32_t index = M32EXEC_GETVAR(*insptr++);
                const int32_t value = M32EXEC_GETVAR(*insptr++);

                CheckArray(j);

                if (aGameArrays[j].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Tried to set on read-only array `%s'", aGameArrays[j].szLabel);

                if (!(index >= 0 && index < aGameArrays[j].size))
                    M32_ERROR("Array index %d out of bounds", index);

                if (vm.flags&VMFLAG_ERROR)
                    continue;

                // NOTE: Other array types not implemented, since they're read-only.
                ((int32_t *)aGameArrays[j].vals)[index] = value;
                continue;
            }

        case CON_GETARRAYSIZE:
            insptr++;
            {
                const int32_t j=*insptr++;

                if (CheckArray(j))
                    continue;

                M32EXEC_SETVAR(*insptr++, Gv_GetArraySize(j));
            }
            continue;

        case CON_RESIZEARRAY:
            insptr++;
            {
                const int32_t j=*insptr++;
                const int32_t asize = M32EXEC_GETVAR(*insptr++);

                CheckArray(j);

                if (aGameArrays[j].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Tried to resize read-only array `%s'", aGameArrays[j].szLabel);

                if (!(asize >= 1 && asize <= 65536))
                    M32_ERROR("Invalid array size %d (must be between 1 and 65536)", asize);

                if (vm.flags&VMFLAG_ERROR)
                    continue;

//                OSD_Printf(OSDTEXT_GREEN "CON_RESIZEARRAY: resizing array %s from %d to %d\n", aGameArrays[j].szLabel, aGameArrays[j].size, asize);
                aGameArrays[j].vals = Xrealloc(aGameArrays[j].vals, sizeof(int32_t) * asize);
                aGameArrays[j].size = asize;

                continue;
            }

        case CON_COPY:
            insptr++;
            {
                const int32_t si=*insptr++;
                int32_t sidx = M32EXEC_GETVAR(*insptr++);
                const int32_t di=*insptr++;
                int32_t didx = M32EXEC_GETVAR(*insptr++);
                int32_t numelts = M32EXEC_GETVAR(*insptr++);

                CheckArray(si);
                CheckArray(di);

                if (aGameArrays[di].dwFlags & GAMEARRAY_READONLY)
                    M32_ERROR("Array %d is read-only!", di);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                const int32_t ssiz = Gv_GetArraySize(si);
                const int32_t dsiz = Gv_GetArraySize(di);

                if ((uint32_t)sidx >= (uint32_t)ssiz)
                    M32_ERROR("Invalid source index %d", sidx);
                if ((uint32_t)didx >= (uint32_t)dsiz)
                    M32_ERROR("Invalid destination index %d", didx);
                if (vm.flags&VMFLAG_ERROR)
                    continue;

                if (numelts > ssiz-sidx)
                    numelts = ssiz-sidx;
                if (numelts > dsiz-didx)
                    numelts = dsiz-didx;

                const gamearray_t *const sar = &aGameArrays[si];
                gamearray_t *const dar = &aGameArrays[di];

                switch (sar->dwFlags & GAMEARRAY_TYPE_MASK)
                {
                case 0:
                case GAMEARRAY_INT32:
                    if (sar->dwFlags & GAMEARRAY_STRIDE2)
                    {
                        for (; numelts>0; numelts--, sidx += 2)
                            ((int32_t *)dar->vals)[didx++] = ((int32_t *)sar->vals)[sidx];
                    }
                    else
                    {
                        Bmemcpy((int32_t *)dar->vals + didx, (int32_t *)sar->vals + sidx,
                                numelts * sizeof(int32_t));
                    }
                    break;
                case GAMEARRAY_INT16:
                    for (; numelts>0; numelts--)
                        ((int32_t *)dar->vals)[didx++] = ((int16_t *)sar->vals)[sidx++];
                    break;
                case GAMEARRAY_UINT8:
                    for (; numelts>0; numelts--)
                        ((int32_t *)dar->vals)[didx++] = ((uint8_t *)sar->vals)[sidx++];
                    break;
                }
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
