static char med_disptext[80];
static char med_edittext[80];
static const char *med_typename="";
static int32_t med_dispwidth=24;
static int32_t med_editval=0;
static int32_t med_thenum=-1;

static void handlemed(int32_t dohex, const char *disp_membername, const char *edit_membername,
                      void *themember, int32_t thesizeof, int32_t themax, int32_t sign)
{
    int32_t i, val;
    int32_t flags = sign;
    sign &= 1;

    assert(!dohex || thesizeof == sizeof(int16_t));

    if (thesizeof==sizeof(int8_t))
    {
        if (sign)
            val = *(int8_t *)themember;
        else
            val = *(uint8_t *)themember;
    }
    else if (thesizeof==sizeof(int16_t))
    {
        val = *(int16_t *)themember;
        // Bug fix : Do not sign extend when dealing with hex values
        if (dohex)
            val &= 0xFFFF;
    }
    else //if (thesizeof==sizeof(int32_t))
        val = *(int32_t *)themember;

    if (dohex)
        i=Bsprintf(med_disptext,"%s: %x", disp_membername, val);
    else
        i=Bsprintf(med_disptext,"%s: %d", disp_membername, val);

    for (; i<med_dispwidth; i++)
        med_disptext[i] = ' ';

    if (med_editval)
    {
        // If editing a hex value and either SHIFT is pressed then toggle a specific bit
        if (SHIFTS_IS_PRESSED && dohex)
        {
            // Get highest bit in maximum value
            int32_t max_bit = 0;
            while (max_bit < 15 && 1<<(max_bit+1) < themax)
                max_bit++;

            Bsprintf(med_edittext, "Toggle %s %d %s Bit 0..%d: ", med_typename, med_thenum, edit_membername, max_bit);
            printmessage16("%s", med_edittext);

            const int32_t bit = getnumber16(med_edittext, -1, max_bit, 1);
            if (bit >= 0)
                *(int16_t *)themember = (int16_t)((1<<bit) ^ (*(int16_t *)themember));
        }
        // Else directly edit value
        else
        {
            Bsprintf(med_edittext,"%s %d %s: ", med_typename, med_thenum, edit_membername);

            printmessage16("%s", med_edittext);
            val = getnumber16(med_edittext, val, themax, flags);

            if (thesizeof==sizeof(int8_t))
            {
                if (sign)
                    *(int8_t *)themember = (int8_t)val;
                else
                    *(uint8_t *)themember = (uint8_t)val;
            }
            else if (thesizeof==sizeof(int16_t))
                *(int16_t *)themember = (int16_t)val;
            else //if (thesizeof==sizeof(int32_t))
                *(int32_t *)themember = val;
        }
    }
}

static void med_printcurline(int32_t xpos, int32_t ypos, int32_t row, int32_t selected)
{
    printext16(xpos,ypos+row*8,editorcolors[11],editorcolors[!!selected],med_disptext,0);
}
