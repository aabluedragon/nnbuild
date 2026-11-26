// Shared Mapster32 DRAWCIRCLE16* VM cases used by Duke, RR, and Blood editors.

#ifndef M32EXEC_GETVAR
# define M32EXEC_GETVAR Gv_GetVar
#endif
#ifndef M32EXEC_EDITOR_COLOR_INDEX
# define M32EXEC_EDITOR_COLOR_INDEX(C) ((C)&255)
#endif

case CON_DRAWCIRCLE16:
        case CON_DRAWCIRCLE16B:
        case CON_DRAWCIRCLE16Z:
            insptr++;
            {
                int32_t x1=M32EXEC_GETVAR(*insptr++), y1=M32EXEC_GETVAR(*insptr++);
                int32_t z1 = tw==CON_DRAWCIRCLE16Z ? M32EXEC_GETVAR(*insptr++) : 0;
                int32_t r=M32EXEC_GETVAR(*insptr++);
                int32_t col=M32EXEC_GETVAR(*insptr++), odrawlinepat=drawlinepat;
                int32_t xofs=0, yofs=0, eccen=16384;

                if (tw==CON_DRAWCIRCLE16B || tw==CON_DRAWCIRCLE16Z)
                {
                    editorGet2dScreenCoordinates(&x1,&y1, x1-pos.x,y1-pos.y, zoom);
                    if (m32_sideview)
                        y1 += getscreenvdisp(z1-pos.z, zoom);
                    r = mulscale14(r,zoom);
                    eccen = scalescreeny(eccen);
                    xofs = halfxdim16;
                    yofs = midydim16;
                }

                drawlinepat = m32_drawlinepat;
                editorDraw2dCircle(xofs+x1, yofs+y1, r, eccen, col>=0?editorcolors[M32EXEC_EDITOR_COLOR_INDEX(col)]:((-col)&255));
                drawlinepat = odrawlinepat;
                continue;
            }

#undef M32EXEC_GETVAR
#undef M32EXEC_EDITOR_COLOR_INDEX
