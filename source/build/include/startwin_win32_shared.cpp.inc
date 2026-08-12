// Shared Win32 startup-window helpers used by the Build game frontends.
// Include with exactly one STARTWIN_WIN32_SHARED_* selector defined.

#if defined(STARTWIN_WIN32_SHARED_SET_PAGE)


static void SetPage(int pageNum)
{
    HWND tab = GetDlgItem(startupdlg, WIN_STARTWIN_TABCTL);
    auto const cur = SendMessage(tab, TCM_GETCURSEL, 0, 0);
    ShowWindow(pages[cur], SW_HIDE);
    SendMessage(tab, TCM_SETCURSEL, pageNum, 0);
    ShowWindow(pages[pageNum], SW_SHOW);
    mode = pageNum;

    SetFocus(GetDlgItem(startupdlg, WIN_STARTWIN_TABCTL));
}
#undef STARTWIN_WIN32_SHARED_SET_PAGE
#endif

#if defined(STARTWIN_WIN32_SHARED_ENABLE_CONFIG_DUKE_RR)

static void EnableConfig(bool n)
{
    //EnableWindow(GetDlgItem(startupdlg, WIN_STARTWIN_CANCEL), n);
    EnableWindow(GetDlgItem(startupdlg, WIN_STARTWIN_START), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCDATA), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCFULLSCREEN), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCGAMEDIR), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCINPUT), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCPOLYMER), n);
    EnableWindow(GetDlgItem(pages[TAB_CONFIG], IDCVMODE), n);
}
#undef STARTWIN_WIN32_SHARED_ENABLE_CONFIG_DUKE_RR
#endif

#if defined(STARTWIN_WIN32_SHARED_ISOPEN)

bool startwin_isopen(void)
{
    return !!startupdlg;
}
#undef STARTWIN_WIN32_SHARED_ISOPEN
#endif

#if defined(STARTWIN_WIN32_SHARED_OPEN)

int32_t startwin_open(void)
{
    if (startupdlg) return 1;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);
    startupdlg = CreateDialog((HINSTANCE)win_gethinstance(), MAKEINTRESOURCE(WIN_STARTWIN), NULL, startup_dlgproc);
    if (startupdlg)
    {
        SetPage(TAB_MESSAGES);
        EnableConfig(0);
        return 0;
    }
    return -1;
}
#undef STARTWIN_WIN32_SHARED_OPEN
#endif

#if defined(STARTWIN_WIN32_SHARED_CLOSE)

int32_t startwin_close(void)
{
    if (!startupdlg) return 1;
    DestroyWindow(startupdlg);
    startupdlg = NULL;
    return 0;
}
#undef STARTWIN_WIN32_SHARED_CLOSE
#endif

#if defined(STARTWIN_WIN32_SHARED_PUTS)

int32_t startwin_puts(const char *buf)
{
    if (!startupdlg) return 1;

    const HWND edctl = pages[TAB_MESSAGES];

    if (!edctl) return -1;

    static HWND dactrl = NULL;
    if (!dactrl) dactrl = GetDlgItem(startupdlg, WIN_STARTWIN_TABCTL);

    int const vis = ((int)SendMessage(dactrl, TCM_GETCURSEL,0,0) == TAB_MESSAGES);

    if (vis)
        SendMessage(edctl, WM_SETREDRAW, FALSE, 0);

    int const curlen = SendMessage(edctl, WM_GETTEXTLENGTH, 0,0);
    SendMessage(edctl, EM_SETSEL, (WPARAM)curlen, (LPARAM)curlen);

    int const   numlines = SendMessage(edctl, EM_GETLINECOUNT, 0, 0);
    static bool newline  = false;
    const char *p        = buf;

    while (*p)
    {
        if (newline)
        {
            SendMessage(edctl, EM_REPLACESEL, 0, (LPARAM)"\r\n");
            newline = false;
        }
        const char *q = p;
        while (*q && *q != '\n') q++;
        static char workbuf[1024];
        Bmemcpy(workbuf, p, q-p);
        if (*q == '\n')
        {
            if (!q[1])
            {
                newline = true;
                workbuf[q-p] = 0;
            }
            else
            {
                workbuf[q-p] = '\r';
                workbuf[q-p+1] = '\n';
                workbuf[q-p+2] = 0;
            }
            p = q+1;
        }
        else
        {
            workbuf[q-p] = 0;
            p = q;
        }
        SendMessage(edctl, EM_REPLACESEL, 0, (LPARAM)workbuf);
    }

    int const newnumlines = SendMessage(edctl, EM_GETLINECOUNT, 0, 0);
    SendMessage(edctl, EM_LINESCROLL, 0, newnumlines - numlines);

    if (vis)
        SendMessage(edctl, WM_SETREDRAW, TRUE, 0);

    return 0;
}
#undef STARTWIN_WIN32_SHARED_PUTS
#endif

#if defined(STARTWIN_WIN32_SHARED_SETTITLE)

int32_t startwin_settitle(const char *str)
{
    if (!startupdlg) return 1;
    SetWindowText(startupdlg, str);
    return 0;
}
#undef STARTWIN_WIN32_SHARED_SETTITLE
#endif

#if defined(STARTWIN_WIN32_SHARED_IDLE)

int32_t startwin_idle(void *v)
{
    if (!startupdlg || !IsWindow(startupdlg)) return 0;
    if (IsDialogMessage(startupdlg, (MSG *)v)) return 1;
    return 0;
}
#undef STARTWIN_WIN32_SHARED_IDLE
#endif


#if defined(STARTWIN_WIN32_SHARED_CASE_VMODE)
        case IDCVMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int i = ComboBox_GetCurSel((HWND)lParam);
                if (i != CB_ERR) i = ComboBox_GetItemData((HWND)lParam, i);
                if (i != CB_ERR)
                {
                    settings.shared.xdim = validmode[i].xdim;
                    settings.shared.ydim = validmode[i].ydim;
                    settings.shared.bpp  = validmode[i].bpp;
                }
            }
            return TRUE;
#undef STARTWIN_WIN32_SHARED_CASE_VMODE
#endif

#if defined(STARTWIN_WIN32_SHARED_CASE_INPUT)
        case IDCINPUT:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int i = ComboBox_GetCurSel((HWND)lParam);
                if (i != CB_ERR) i = ComboBox_GetItemData((HWND)lParam, i);
                if (i != CB_ERR)
                {
                    switch (i)
                    {
                    case INPUT_KB:
                        settings.shared.usemouse = settings.shared.usejoystick = 0;
                        break;
                    case INPUT_MOUSE:
                        settings.shared.usemouse = 1;
                        settings.shared.usejoystick = 0;
                        break;
                    case INPUT_JOYSTICK:
                        settings.shared.usemouse = 0;
                        settings.shared.usejoystick = 1;
                        break;
                    case INPUT_ALL:
                        settings.shared.usemouse = settings.shared.usejoystick = 1;
                        break;
                    }
                }
            }
            return TRUE;
#undef STARTWIN_WIN32_SHARED_CASE_INPUT
#endif

#if defined(STARTWIN_WIN32_SHARED_CASE_GAMEDIR)
        case IDCGAMEDIR:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int i = ComboBox_GetCurSel((HWND)lParam);
                if (i != CB_ERR) i = ComboBox_GetItemData((HWND)lParam, i);
                if (i != CB_ERR)
                {
                    if (i==0)
                        settings.gamedir = NULL;
                    else
                    {
                        BUILDVFS_FIND_REC *dir = finddirs;
                        for (int j = 1; dir != NULL; dir = dir->next, j++)
                        {
                            if (j == i)
                            {
                                settings.gamedir = dir->name;
                                break;
                            }
                        }
                    }
                }
            }
            return TRUE;
#undef STARTWIN_WIN32_SHARED_CASE_GAMEDIR
#endif

#if defined(STARTWIN_WIN32_SHARED_CASE_NOTIFY)
    case WM_NOTIFY:
    {
        auto nmhdr = (LPNMHDR)lParam;
        if (nmhdr->idFrom != WIN_STARTWIN_TABCTL) break;
        int const cur = SendMessage(nmhdr->hwndFrom, TCM_GETCURSEL,0,0);
        switch (nmhdr->code)
        {
            case TCN_SELCHANGING:
            case TCN_SELCHANGE:
                if (cur < 0 || !pages[cur])
                    break;
                ShowWindow(pages[cur], nmhdr->code == TCN_SELCHANGING ? SW_HIDE : SW_SHOW);
                return TRUE;
        }
        break;
    }
#undef STARTWIN_WIN32_SHARED_CASE_NOTIFY
#endif

#if defined(STARTWIN_WIN32_SHARED_CASE_CLOSE_DESTROY_COMMAND_BASIC)
    case WM_CLOSE:
        if (mode == TAB_CONFIG) done = 0;
        else quitevent++;
        return TRUE;

    case WM_DESTROY:
        if (hbmp)
        {
            DeleteObject(hbmp);
            hbmp = NULL;
        }

        if (pages[TAB_CONFIG])
        {
            DestroyWindow(pages[TAB_CONFIG]);
            pages[TAB_CONFIG] = NULL;
        }

        startupdlg = NULL;
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case WIN_STARTWIN_CANCEL:
            if (mode == TAB_CONFIG) done = 0;
            else quitevent++;
            return TRUE;
        case WIN_STARTWIN_START:
            done = 1;
            return TRUE;
        }
        return FALSE;
#undef STARTWIN_WIN32_SHARED_CASE_CLOSE_DESTROY_COMMAND_BASIC
#endif
