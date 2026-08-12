    if (asksave == 1)
        asksave++;
    else if (asksave == 2 && (bstatus + lastbstatus) == 0
#if M32_UNDO
             && mapstate
#endif
        )
    {
        int32_t i;
        // check keys so that e.g. bulk deletions won't produce
        // as much revisions as deleted sprites
        for (i=ARRAY_SIZE(keystatus)-1; i>=0; i--)
            if (keystatus[i])
                break;
        if (i==-1)
        {
#if M32_UNDO
            create_map_snapshot();
#else
            CheckMapCorruption(5, 0);
#endif
            asksave++;
        }
    }
    else if (asksave == 3)
        asksave++;

    if (totalclock > autosavetimer && autosave)
    {
        if (asksave == 4)
        {
            if (CheckMapCorruption(5, 0)>=4)
            {
                if (SaveBoard("autosave_corrupt.map", M32_SB_NOEXT) != NULL)
                    message("Board autosaved to AUTOSAVE_CORRUPT.MAP");
            }
            else
            {
                if (SaveBoard("autosave.map", 0) != NULL)
                    message("Board autosaved to AUTOSAVE.MAP");
            }

            asksave++;
        }
        autosavetimer = (int32_t) totalclock+120*autosave;
    }

    if (PRESSED_KEYSC(F12))   //F12
    {
#ifdef ENGINE_SCREENSHOT_DEBUG
        extern int32_t engine_screenshot;
        engine_screenshot = 1;
#else
        videoCaptureScreen("captxxxx.tga", eitherSHIFT);
        silentmessage("Saved screenshot");
#endif
    }
}
