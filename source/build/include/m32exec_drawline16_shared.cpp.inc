// Shared Mapster32 DRAWLINE16* VM cases used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_EDITOR_COLOR_INDEX
# define M32EXEC_EDITOR_COLOR_INDEX(C) ((C)&255)
#endif

case CON_DRAWLINE16:
        case CON_DRAWLINE16B:
        case CON_DRAWLINE16Z:
            insptr++;
            {
                int32_t x1=M32EXEC_GETVAR(*insptr++), y1=M32EXEC_GETVAR(*insptr++);
                int32_t z1=tw==CON_DRAWLINE16Z?M32EXEC_GETVAR(*insptr++):0;
                int32_t x2=M32EXEC_GETVAR(*insptr++), y2=M32EXEC_GETVAR(*insptr++);
                int32_t z2=tw==CON_DRAWLINE16Z?M32EXEC_GETVAR(*insptr++):0;
                int32_t col=M32EXEC_GETVAR(*insptr++), odrawlinepat=drawlinepat;
                int32_t xofs=0, yofs=0;

                if (tw==CON_DRAWLINE16B || tw==CON_DRAWLINE16Z)
                {
                    editorGet2dScreenCoordinates(&x1,&y1, x1-pos.x,y1-pos.y, zoom);
                    editorGet2dScreenCoordinates(&x2,&y2, x2-pos.x,y2-pos.y, zoom);

                    if (tw==CON_DRAWLINE16Z && m32_sideview)
                    {
                        y1 += getscreenvdisp(z1-pos.z,zoom);
                        y2 += getscreenvdisp(z2-pos.z,zoom);
                    }

                    xofs = halfxdim16;
                    yofs = midydim16;
                }

                drawlinepat = m32_drawlinepat;
                editorDraw2dLine(xofs+x1,yofs+y1, xofs+x2,yofs+y2, col>=0?editorcolors[M32EXEC_EDITOR_COLOR_INDEX(col)]:((-col)&255));
                drawlinepat = odrawlinepat;
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_EDITOR_COLOR_INDEX
