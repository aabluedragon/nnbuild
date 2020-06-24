void GAME_drawosdchar(int32_t x, int32_t y, char ch, int32_t shade, int32_t pal);
void GAME_drawosdstr(int32_t x, int32_t y, const char *ch, int32_t len, int32_t shade, int32_t pal);
void GAME_drawosdcursor(int32_t x, int32_t y, int32_t type, int32_t lastkeypress);
int32_t GAME_getcolumnwidth(int32_t w);
int32_t GAME_getrowheight(int32_t h);
void GAME_clearbackground(int c, int r);
void GAME_onshowosd(int shown);

extern int osdhightile;
extern float osdscale, osdrscale;

