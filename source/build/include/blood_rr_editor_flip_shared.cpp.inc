    if (PRESSED_KEYSC(F))  //F (Flip)
    {
        if (eitherALT)  //ALT-F (relative alignmment flip)
        {
            if (!AIMING_AT_SPRITE && ASSERT_AIMING)
                SetFirstWall(searchsector, searchwall, eitherSHIFT);
        }
        else
        {
            static const int8_t next2[4] = { 1, 3, 0, 2 };  // 0->1->3->2->0
            static const int8_t prev2[4] = { 2, 0, 3, 1 };  // 0<-1<-3<-2<-0

            static const char *flip2label[4] = { "none", "X", "Y", "X and Y" };

            if (AIMING_AT_WALL_OR_MASK)
            {
                i = wall[searchbottomwall].cstat;
                i = ((i>>3)&1)+((i>>7)&2);    //3-x,8-y

                i = eitherSHIFT ? prev2[i] : next2[i];
                message("Wall %d flip %s", searchwall, flip2label[i]);

                i = ((i&1)<<3)+((i&2)<<7);
                wall[searchbottomwall].cstat &= ~0x0108;
                wall[searchbottomwall].cstat |= i;
                asksave = 1;
            }
            else if (AIMING_AT_CEILING_OR_FLOOR)  //8-way ceiling/floor flipping (bits 2,4,5)
                toggle_cf_flipping(searchsector, AIMING_AT_FLOOR);
            else if (AIMING_AT_SPRITE)
            {
                i = sprite[searchwall].cstat;
                if (((i&48) == 32) && ((i&64) == 0))
                {
                    sprite[searchwall].cstat &= ~0xc;
                    sprite[searchwall].cstat |= ((i&4)^4);
                    message("Sprite %d flip bit %s",searchwall, ONOFF(sprite[searchwall].cstat&4));
                }
                else
                {
                    i = ((i>>2)&3);

                    i = eitherSHIFT ? prev2[i] : next2[i];
                    message("Sprite %d flip %s", searchwall, flip2label[i]);

                    sprite[searchwall].cstat &= ~0xc;
                    sprite[searchwall].cstat |= (i<<2);
                }
                asksave = 1;
            }
        }
    }
