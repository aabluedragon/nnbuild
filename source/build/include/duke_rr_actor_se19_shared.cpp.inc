        case SE_19_EXPLOSION_LOWERS_CEILING: //Battlestar galactia shields

            if (pData[0])
            {
                if (pData[0] == 1)
                {
                    pData[0]++;
                    x = pSector->wallptr;
                    q = x+pSector->wallnum;

                    for (j=x; j<q; j++)
                    {
                        if (wall[j].overpicnum == BIGFORCE)
                        {
                            wall[j].cstat &= (128+32+8+4+2);
                            wall[j].overpicnum = 0;

                            if (wall[j].nextwall >= 0)
                            {
                                wall[wall[j].nextwall].overpicnum = 0;
                                wall[wall[j].nextwall].cstat &= (128+32+8+4+2);
                            }
                        }
                    }
                }

                if (pSector->ceilingz < pSector->floorz)
                    pSector->ceilingz += SP(spriteNum);
                else
                {
                    pSector->ceilingz = pSector->floorz;

                    for (SPRITES_OF(STAT_EFFECTOR, j))
                    {
                        if (sprite[j].lotag == SE_0_ROTATING_SECTOR && sprite[j].hitag==spriteHitag)
                        {
                            sectortype *const pSector     = &sector[sprite[j].sectnum];
                            int const         ownerSector = sprite[sprite[j].owner].sectnum;

                            pSector->ceilingpal   = sector[ownerSector].floorpal;
                            pSector->floorpal     = pSector->ceilingpal;
                            pSector->ceilingshade = sector[ownerSector].floorshade;
                            pSector->floorshade   = pSector->ceilingshade;

                            actor[sprite[j].owner].t_data[0] = 2;
                        }
                    }

                    DELETE_SPRITE_AND_CONTINUE(spriteNum);
                }
            }
            else //Not hit yet
            {
                if (G_FindExplosionInSector(pSprite->sectnum) >= 0)
                {
                    P_DoQuote(QUOTE_UNLOCKED, g_player[myconnectindex].ps);

                    for (SPRITES_OF(STAT_EFFECTOR, l))
                    {
                        switch (sprite[l].lotag & 0x7fff)
                        {
                        case SE_0_ROTATING_SECTOR:
                            if (sprite[l].hitag == spriteHitag)
                            {
                                int const spriteOwner = sprite[l].owner;
                                int const sectNum     = sprite[l].sectnum;

                                sector[sectNum].ceilingshade = sprite[spriteOwner].shade;
                                sector[sectNum].floorshade   = sector[sectNum].ceilingshade;
                                sector[sectNum].ceilingpal   = sprite[spriteOwner].pal;
                                sector[sectNum].floorpal     = sector[sectNum].ceilingpal;
                            }
                            break;

                        case SE_1_PIVOT:
                        case SE_12_LIGHT_SWITCH:
//                        case SE_18_INCREMENTAL_SECTOR_RISE_FALL:
                        case SE_19_EXPLOSION_LOWERS_CEILING:
                            if (spriteHitag == sprite[l].hitag)
                                if (actor[l].t_data[0] == 0)
                                {
                                    actor[l].t_data[0] = 1;  // Shut them all on
                                    sprite[l].owner    = spriteNum;
                                }

                            break;
                        }
                    }
                }
            }

            break;
