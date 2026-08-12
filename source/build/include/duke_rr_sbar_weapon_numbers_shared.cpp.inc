static void G_DrawWeapNum(int16_t ind, int32_t x, int32_t y, int32_t num1, int32_t num2, int32_t ha)
{
    char dabuf[16];

    const int32_t sbscale = sbarsc(65536);
    const int32_t sby = sbary(y);

    rotatesprite_fs(sbarx(x-7), sby, sbscale, 0, THREEBYFIVE+ind+1, ha-10, 7, 10);
    rotatesprite_fs(sbarx(x-3), sby, sbscale, 0, THREEBYFIVE+10, ha, 0, 10);

    if (VOLUMEONE && (ind > HANDBOMB_WEAPON || ind < 0))
    {
        minitextshade(x+1, y-4, "ORDER", 20, 11, 2+8+16+ROTATESPRITE_MAX);
        return;
    }

    rotatesprite_fs(sbarx(x+9), sby, sbscale, 0, THREEBYFIVE+11, ha, 0, 10);

    if (num1 > 99) num1 = 99;
    if (num2 > 99) num2 = 99;

    Bsprintf(dabuf, "%d", num1);
    if (num1 > 9)
    {
        rotatesprite_fs(sbarx(x), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+4), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
    }
    else rotatesprite_fs(sbarx(x+4), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);

    Bsprintf(dabuf, "%d", num2);
    if (num2 > 9)
    {
        rotatesprite_fs(sbarx(x+13), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+17), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
        return;
    }
    rotatesprite_fs(sbarx(x+13), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
}

static void G_DrawWeapNum2(char ind, int32_t x, int32_t y, int32_t num1, int32_t num2, char ha)
{
    char dabuf[16];

    const int32_t sbscale = sbarsc(65536);
    const int32_t sby = sbary(y);

    rotatesprite_fs(sbarx(x-7), sby, sbscale, 0, THREEBYFIVE+ind+1, ha-10, 7, 10);
    rotatesprite_fs(sbarx(x-4), sby, sbscale, 0, THREEBYFIVE+10, ha, 0, 10);
    rotatesprite_fs(sbarx(x+13), sby, sbscale, 0, THREEBYFIVE+11, ha, 0, 10);

    Bsprintf(dabuf, "%d", num1);
    if (num1 > 99)
    {
        rotatesprite_fs(sbarx(x), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+4), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+8), sby, sbscale, 0, THREEBYFIVE+dabuf[2]-'0', ha, 0, 10);
    }
    else if (num1 > 9)
    {
        rotatesprite_fs(sbarx(x+4), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+8), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
    }
    else rotatesprite_fs(sbarx(x+8), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);

    Bsprintf(dabuf, "%d", num2);
    if (num2 > 99)
    {
        rotatesprite_fs(sbarx(x+17), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+21), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+25), sby, sbscale, 0, THREEBYFIVE+dabuf[2]-'0', ha, 0, 10);
    }
    else if (num2 > 9)
    {
        rotatesprite_fs(sbarx(x+17), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
        rotatesprite_fs(sbarx(x+21), sby, sbscale, 0, THREEBYFIVE+dabuf[1]-'0', ha, 0, 10);
        return;
    }
    else
        rotatesprite_fs(sbarx(x+25), sby, sbscale, 0, THREEBYFIVE+dabuf[0]-'0', ha, 0, 10);
}
