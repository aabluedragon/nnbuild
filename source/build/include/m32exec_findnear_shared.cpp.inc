// Shared Mapster32 findnear* VM cases used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_SETVAR
# define M32EXEC_SETVAR Gv_SetVar
#endif

case CON_FINDNEARSPRITE:
        case CON_FINDNEARSPRITE3D:
        case CON_FINDNEARSPRITEVAR:
        case CON_FINDNEARSPRITE3DVAR:
            insptr++;
            {
                // syntax findnearactor(var) <type> <maxdist(var)> <getvar>
                // gets the sprite ID of the nearest actor within max dist
                // that is of <type> into <getvar>
                // -1 for none found
                // <type> <maxdist(varid)> <varid>
                int32_t lType=*insptr++;
                int32_t lMaxDist = (tw==CON_FINDNEARSPRITE || tw==CON_FINDNEARSPRITE3D)?
                                   *insptr++ : M32EXEC_GETVAR(*insptr++);
                int32_t lVarID=*insptr++;
                int32_t lFound=-1, j, k = MAXSTATUS-1;

                X_ERROR_INVALIDCI();
                do
                {
                    j=headspritestat[k];    // all sprites
                    if (tw==CON_FINDNEARSPRITE3D || tw==CON_FINDNEARSPRITE3DVAR)
                    {
                        while (j>=0)
                        {
                            if (sprite[j].picnum == lType && j != vm.spriteNum && dist(&sprite[vm.spriteNum], &sprite[j]) < lMaxDist)
                            {
                                lFound=j;
                                j = MAXSPRITES;
                                break;
                            }
                            j = nextspritestat[j];
                        }
                        if (j == MAXSPRITES)
                            break;
                        continue;
                    }

                    while (j>=0)
                    {
                        if (sprite[j].picnum == lType && j != vm.spriteNum && ldist(&sprite[vm.spriteNum], &sprite[j]) < lMaxDist)
                        {
                            lFound=j;
                            j = MAXSPRITES;
                            break;
                        }
                        j = nextspritestat[j];
                    }

                    if (j == MAXSPRITES)
                        break;
                }
                while (k--);
                M32EXEC_SETVAR(lVarID, lFound);
                continue;
            }

        case CON_FINDNEARSPRITEZVAR:
        case CON_FINDNEARSPRITEZ:
            insptr++;
            {
                // syntax findnearactor(var) <type> <maxdist(var)> <getvar>
                // gets the sprite ID of the nearest actor within max dist
                // that is of <type> into <getvar>
                // -1 for none found
                // <type> <maxdist(varid)> <varid>
                int32_t lType=*insptr++;
                int32_t lMaxDist = (tw==CON_FINDNEARSPRITEZVAR) ? M32EXEC_GETVAR(*insptr++) : *insptr++;
                int32_t lMaxZDist = (tw==CON_FINDNEARSPRITEZVAR) ? M32EXEC_GETVAR(*insptr++) : *insptr++;
                int32_t lVarID=*insptr++;
                int32_t lFound=-1, lTemp, lTemp2, j, k=MAXSTATUS-1;

                X_ERROR_INVALIDCI();
                do
                {
                    j=headspritestat[k];    // all sprites
                    if (j == -1) continue;
                    do
                    {
                        if (sprite[j].picnum == lType && j != vm.spriteNum)
                        {
                            lTemp=ldist(&sprite[vm.spriteNum], &sprite[j]);
                            if (lTemp < lMaxDist)
                            {
                                lTemp2=klabs(sprite[vm.spriteNum].z-sprite[j].z);
                                if (lTemp2 < lMaxZDist)
                                {
                                    lFound=j;
                                    j = MAXSPRITES;
                                    break;
                                }
                            }
                        }
                        j = nextspritestat[j];
                    }
                    while (j>=0);
                    if (j == MAXSPRITES)
                        break;
                }
                while (k--);
                M32EXEC_SETVAR(lVarID, lFound);

                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_SETVAR
