static void med_handlecommon(int32_t xpos, int32_t ypos, int32_t *row, int32_t rowmax)
{
    idle_waitevent();
    if (handleevents())
        quitevent = 0;

    _printmessage16("Edit mode, press <Esc> to exit");

    if (PRESSED_KEYSC(DOWN))
    {
        if (*row < rowmax)
        {
            med_printcurline(xpos, ypos, *row, 0);
            (*row)++;
        }
    }

    if (PRESSED_KEYSC(UP))
    {
        if (*row > 0)
        {
            med_printcurline(xpos, ypos, *row, 0);
            (*row)--;
        }
    }

    if (PRESSED_KEYSC(ENTER))
        med_editval = 1;
}
