int32_t AskIfSure(const char *text)
{
    int32_t retval=1;

    if (in3dmode())
    {
        videoBeginDrawing(); //{{{
        printext256(0,0,whitecol,0,text?text:"Are you sure you want to proceed?",0);
        videoEndDrawing();   //}}}
    }
    else
    {
        _printmessage16("%s", text?text:"Are you sure you want to proceed?");
    }

    videoShowFrame(1);

    while ((keystatus[KEYSC_ESC]|keystatus[KEYSC_ENTER]|keystatus[KEYSC_SPACE]|keystatus[KEYSC_N]) == 0)
    {
        idle_waitevent();

        if (handleevents())
        {
            if (quitevent)
            {
                retval = 1;
                break;
            }
        }

        if (PRESSED_KEYSC(Y) || PRESSED_KEYSC(ENTER))
        {
            retval = 0;
            break;
        }
    }

    if (PRESSED_KEYSC(ESC))
        retval = 1;

    return retval;
}
