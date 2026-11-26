// Shared Mapster32 ROTATESPRITE* and SETGAMEPALETTE VM cases.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif

case CON_ROTATESPRITEA:
        case CON_ROTATESPRITE16:
        case CON_ROTATESPRITE:
            insptr++;
            {
                int32_t x=M32EXEC_GETVAR(*insptr++),   y=M32EXEC_GETVAR(*insptr++),           z=M32EXEC_GETVAR(*insptr++);
                int32_t a=M32EXEC_GETVAR(*insptr++),   tilenum=M32EXEC_GETVAR(*insptr++),     shade=M32EXEC_GETVAR(*insptr++);
                int32_t pal=M32EXEC_GETVAR(*insptr++), orientation=M32EXEC_GETVAR(*insptr++);
                int32_t alpha = (tw == CON_ROTATESPRITEA) ? M32EXEC_GETVAR(*insptr++) : 0;
                int32_t x1=M32EXEC_GETVAR(*insptr++),  y1=M32EXEC_GETVAR(*insptr++);
                int32_t x2=M32EXEC_GETVAR(*insptr++),  y2=M32EXEC_GETVAR(*insptr++);

                if (tw != CON_ROTATESPRITE16 && !(orientation&ROTATESPRITE_FULL16))
                {
                    x<<=16;
                    y<<=16;
                }

                orientation &= (ROTATESPRITE_MAX-1);

                rotatesprite_(x,y,z,a,tilenum,shade,pal,2|orientation,alpha,0,x1,y1,x2,y2);
                continue;
            }

        case CON_SETGAMEPALETTE:
            insptr++;
            SetGamePalette(M32EXEC_GETVAR(*insptr++));
            continue;

#undef M32EXEC_GETVAR
