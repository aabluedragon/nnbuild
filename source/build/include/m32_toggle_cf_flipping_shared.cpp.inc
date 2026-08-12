static void toggle_cf_flipping(int32_t sectnum, int32_t floorp)
{
    static const int8_t next3[8] = { 6, 7, 4, 5, 0, 1, 3, 2 };  // 0->6->3->5->1->7->2->4->0
    static const int8_t prev3[8] = { 4, 5, 7, 6, 2, 3, 0, 1 };  // 0<-6<-3<-5<-1<-7<-2<-4<-0

    static const int16_t orient[8] = { 360, -360, -180, 180, -270, 270, 90, -90 };

    const int32_t search = floorp ? SEARCH_FLOOR : SEARCH_CEILING;

    uint16_t *const stat = &SECTORFLD(sectnum,stat, floorp);
    int32_t i = *stat;

    i = (i&0x4)+((i>>4)&3);
    i = eitherSHIFT ? prev3[i] : next3[i];
    message("Sector %d %s flip %d deg%s", sectnum, typestr[search],
            klabs(orient[i])%360, orient[i] < 0 ? " mirrored":"");
    i = (i&0x4)+((i&3)<<4);
    *stat &= ~0x34;
    *stat |= i;
    asksave = 1;
}
