void SetGamePalette(int32_t palid)
{
    if ((unsigned)palid >= MAXBASEPALS)
        palid = 0;

    videoSetPalette(GAMMA_CALC, palid, 2);
}

static void SearchSectors(int32_t dir)  // <0: backwards, >=0: forwards
{
    int32_t ii=0;
    dir = 1-2*(dir<0);

    if (cursector_lotag!=0)
    {
        if ((dir>0 && cursectornum<MAXSECTORS) || (dir<0 && cursectornum>=0))
            cursectornum += dir;

        for (ii=cursectornum; ii>=0 && ii<numsectors; ii+=dir)
        {
            if (sector[ii].lotag==cursector_lotag)
            {
                pos.x = wall[sector[ii].wallptr].x;
                pos.y = wall[sector[ii].wallptr].y;
                printmessage16("%s Sector search: found", dir<0?"<":">");
                cursectornum = ii;
                return;
            }
        }
    }
    printmessage16("%s Sector search: none found", dir<0?"<":">");
}
