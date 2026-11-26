//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#ifndef _WIN32
#error Only for Windows
#endif

#include "renderlayer.h"

#ifdef STARTUP_SETUP_WINDOW

#define NEED_WINDOWSX_H
#define NEED_COMMCTRL_H
#define ONLY_USERDEFS

#include "_control.h"
#include "build.h"
#include "cache1d.h"
#include "cmdline.h"
#include "common_game.h"
#include "compat.h"
#include "control.h"
#include "function.h"
#include "game.h"
#include "grpscan.h"
#include "inv.h"
#include "keyboard.h"
#include "startwin.game.h"
#include "windows_inc.h"

#define TAB_CONFIG 0
#define TAB_MESSAGES 1

static struct
{
    struct grpfile_t const * grp;
    char *gamedir;
    ud_setup_t shared;
    int polymer;
}
settings;

static HWND startupdlg;
static HWND pages[3];
static int done = -1;
static int mode = TAB_CONFIG;

static BUILDVFS_FIND_REC *finddirs;

static inline void clearfilenames(void)
{
    klistfree(finddirs);
    finddirs = NULL;
}

static inline void getfilenames(char const *path)
{
    clearfilenames();
    finddirs = klistpath(path,"*",BUILDVFS_FIND_DIR);
}

#define POPULATE_VIDEO 1
#define POPULATE_CONFIG 2
#define POPULATE_GAME 4
#define POPULATE_GAMEDIRS 8

#ifdef INPUT_MOUSE
#undef INPUT_MOUSE
#endif

#define INPUT_KB 0
#define INPUT_MOUSE 1
#define INPUT_JOYSTICK 2
#define INPUT_ALL 3

const char *controlstrings[] = { "Keyboard only", "Keyboard and mouse", "Keyboard and joystick", "All supported devices" };

static void PopulateForm(int32_t pgs)
{
    char buf[512];

    if (pgs & POPULATE_GAMEDIRS)
    {
        HWND hwnd = GetDlgItem(pages[TAB_CONFIG], IDCGAMEDIR);

        getfilenames("/");
        (void)ComboBox_ResetContent(hwnd);
        int const r = ComboBox_AddString(hwnd, "None");
        (void)ComboBox_SetItemData(hwnd, r, 0);
        (void)ComboBox_SetCurSel(hwnd, r);
        auto dirs = finddirs;
        for (int i=1, j=1; dirs != NULL; dirs=dirs->next)
        {
            if (Bstrcasecmp(dirs->name, "autoload") == 0)
            {
                j++;
                continue;
            }

            (void)ComboBox_AddString(hwnd, dirs->name);
            (void)ComboBox_SetItemData(hwnd, i, j);
            if (Bstrcasecmp(dirs->name, settings.gamedir) == 0)
                (void)ComboBox_SetCurSel(hwnd, i);

            i++;
            j++;
        }
    }

    if (pgs & POPULATE_VIDEO)
    {
        HWND hwnd = GetDlgItem(pages[TAB_CONFIG], IDCVMODE);
        int mode = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, settings.shared.bpp, settings.shared.fullscreen, 1);

        if (mode < 0 || (settings.shared.bpp < 15 && (settings.polymer)))
        {
            int CONSTEXPR cd[] = { 32, 24, 16, 15, 8, 0 };
            int i;

            for (i=0; cd[i];)
            {
                if (cd[i] >= settings.shared.bpp) i++;
                else break;
            }
            for (; cd[i]; i++)
            {
                mode = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, cd[i], settings.shared.fullscreen, 1);
                if (mode < 0) continue;
                settings.shared.bpp = cd[i];
                break;
            }
        }

        Button_SetCheck(GetDlgItem(pages[TAB_CONFIG], IDCFULLSCREEN), ((settings.shared.fullscreen) ? BST_CHECKED : BST_UNCHECKED));
        Button_SetCheck(GetDlgItem(pages[TAB_CONFIG], IDCPOLYMER), ((settings.polymer) ? BST_CHECKED : BST_UNCHECKED));

        (void)ComboBox_ResetContent(hwnd);

        for (int i=0; i<validmodecnt; i++)
        {
            if (validmode[i].fs != (settings.shared.fullscreen)) continue;
            if ((validmode[i].bpp < 15) && (settings.polymer)) continue;

            // all modes get added to the 3D mode list
            Bsprintf(buf, "%dx%d %s", validmode[i].xdim, validmode[i].ydim, validmode[i].bpp == 8 ? "software" : "OpenGL");
            int const j = ComboBox_AddString(hwnd, buf);
            (void)ComboBox_SetItemData(hwnd, j, i);
            if (i == mode)(void)ComboBox_SetCurSel(hwnd, j);
        }
    }

    if (pgs & POPULATE_CONFIG)
    {
        Button_SetCheck(GetDlgItem(pages[TAB_CONFIG], IDCALWAYSSHOW), (settings.shared.forcesetup ? BST_CHECKED : BST_UNCHECKED));
        Button_SetCheck(GetDlgItem(pages[TAB_CONFIG], IDCAUTOLOAD), (!(settings.shared.noautoload) ? BST_CHECKED : BST_UNCHECKED));

        HWND hwnd = GetDlgItem(pages[TAB_CONFIG], IDCINPUT);

        (void)ComboBox_ResetContent(hwnd);
        (void)ComboBox_SetCurSel(hwnd, 0);

        int j = 4;

        for (int i=0; i<j; i++)
        {
            (void)ComboBox_InsertString(hwnd, i, controlstrings[i]);
            (void)ComboBox_SetItemData(hwnd, i, i);

            switch (i)
            {
            case INPUT_MOUSE:
                if (settings.shared.usemouse && !settings.shared.usejoystick)(void)ComboBox_SetCurSel(hwnd, i);
                break;
            case INPUT_JOYSTICK:
                if (!settings.shared.usemouse && settings.shared.usejoystick)(void)ComboBox_SetCurSel(hwnd, i);
                break;
            case INPUT_ALL:
                if (settings.shared.usemouse && settings.shared.usejoystick)(void)ComboBox_SetCurSel(hwnd, i);
                break;
            }
        }
    }

    if (pgs & POPULATE_GAME)
    {
        HWND hwnd = GetDlgItem(pages[TAB_CONFIG], IDCDATA);

        for (auto fg = foundgrps; fg; fg=fg->next)
        {
            Bsprintf(buf, "%s\t%s", fg->type->name, fg->filename);
            int const j = ListBox_AddString(hwnd, buf);
            (void)ListBox_SetItemData(hwnd, j, (LPARAM)fg);
            if (settings.grp == fg)
                (void)ListBox_SetCurSel(hwnd, j);
        }
    }
}

static INT_PTR CALLBACK ConfigPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCFULLSCREEN:
            settings.shared.fullscreen = !settings.shared.fullscreen;
            PopulateForm(POPULATE_VIDEO);
            return TRUE;
        case IDCPOLYMER:
            settings.polymer = !settings.polymer;
            if (settings.shared.bpp == 8) settings.shared.bpp = 32;
            PopulateForm(POPULATE_VIDEO);
            return TRUE;
        #define STARTWIN_WIN32_SHARED_CASE_VMODE
        #include "../../build/include/startwin_win32_shared.cpp.inc"
        case IDCALWAYSSHOW:
            settings.shared.forcesetup = IsDlgButtonChecked(hwndDlg, IDCALWAYSSHOW) == BST_CHECKED;
            return TRUE;
        case IDCAUTOLOAD:
            settings.shared.noautoload = (IsDlgButtonChecked(hwndDlg, IDCAUTOLOAD) != BST_CHECKED);
            return TRUE;
        #define STARTWIN_WIN32_SHARED_CASE_INPUT
        #include "../../build/include/startwin_win32_shared.cpp.inc"

        #define STARTWIN_WIN32_SHARED_CASE_GAMEDIR
        #include "../../build/include/startwin_win32_shared.cpp.inc"
        case IDCDATA:
        {
            if (HIWORD(wParam) != LBN_SELCHANGE) break;
            intptr_t i = ListBox_GetCurSel((HWND)lParam);
            if (i != CB_ERR) i = ListBox_GetItemData((HWND)lParam, i);
            if (i != CB_ERR)
            {
                settings.grp = (grpfile_t const *)i;
            }
            return TRUE;
        }
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}
#define STARTWIN_WIN32_SHARED_SET_PAGE
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_ENABLE_CONFIG_DUKE_RR
#include "../../build/include/startwin_win32_shared.cpp.inc"

static INT_PTR CALLBACK startup_dlgproc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HBITMAP hbmp = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // Fetch the positions (in screen coordinates) of all the windows we need to tweak
        RECT chrome = {};
        AdjustWindowRect(&chrome, GetWindowLong(hwndDlg, GWL_STYLE), FALSE);
        RECT rdlg;
        GetWindowRect(hwndDlg, &rdlg);

        // Knock off the non-client area of the main dialogue to give just the client area
        rdlg.left -= chrome.left;
        rdlg.top -= chrome.top;
        rdlg.right -= chrome.right;
        rdlg.bottom -= chrome.bottom;

        RECT rtab;
        GetWindowRect(GetDlgItem(hwndDlg, WIN_STARTWIN_TABCTL), &rtab);

        // Translate them to client-relative coordinates wrt the main dialogue window
        rtab.right -= rtab.left - 1;
        rtab.bottom -= rtab.top - 1;
        rtab.left  -= rdlg.left;
        rtab.top -= rdlg.top;

        RECT rcancel;
        GetWindowRect(GetDlgItem(hwndDlg, WIN_STARTWIN_CANCEL), &rcancel);

        rcancel.right -= rcancel.left - 1;
        rcancel.bottom -= rcancel.top - 1;
        rcancel.left -= rdlg.left;
        rcancel.top -= rdlg.top;

        RECT rstart;
        GetWindowRect(GetDlgItem(hwndDlg, WIN_STARTWIN_START), &rstart);

        rstart.right -= rstart.left - 1;
        rstart.bottom -= rstart.top - 1;
        rstart.left -= rdlg.left;
        rstart.top -= rdlg.top;

        // And then convert the main dialogue coordinates to just width/length
        rdlg.right -= rdlg.left - 1;
        rdlg.bottom -= rdlg.top - 1;
        rdlg.left = 0;
        rdlg.top = 0;

        // Load the bitmap into the bitmap control and fetch its dimensions
        hbmp = LoadBitmap((HINSTANCE)win_gethinstance(), MAKEINTRESOURCE(RSRC_BMP));

        HWND hwnd = GetDlgItem(hwndDlg, WIN_STARTWIN_BITMAP);
        SendMessage(hwnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbmp);

        RECT r;
        GetClientRect(hwnd, &r);

        int const xoffset = r.right;
        int const yoffset = r.bottom - rdlg.bottom;

        // Shift and resize the controls that require it
        rtab.left += xoffset;
        rtab.bottom += yoffset;
        rcancel.left += xoffset;
        rcancel.top += yoffset;
        rstart.left += xoffset;
        rstart.top += yoffset;
        rdlg.right += xoffset;
        rdlg.bottom += yoffset;

        // Move the controls to their new positions
        MoveWindow(GetDlgItem(hwndDlg, WIN_STARTWIN_TABCTL), rtab.left, rtab.top, rtab.right, rtab.bottom, FALSE);
        MoveWindow(GetDlgItem(hwndDlg, WIN_STARTWIN_CANCEL), rcancel.left, rcancel.top, rcancel.right, rcancel.bottom, FALSE);
        MoveWindow(GetDlgItem(hwndDlg, WIN_STARTWIN_START), rstart.left, rstart.top, rstart.right, rstart.bottom, FALSE);

        // Move the main dialogue to the centre of the screen
        HDC hdc = GetDC(NULL);
        rdlg.left = (GetDeviceCaps(hdc, HORZRES) - rdlg.right) / 2;
        rdlg.top = (GetDeviceCaps(hdc, VERTRES) - rdlg.bottom) / 2;
        ReleaseDC(NULL, hdc);
        MoveWindow(hwndDlg, rdlg.left + chrome.left, rdlg.top + chrome.left,
                   rdlg.right + (-chrome.left+chrome.right), rdlg.bottom + (-chrome.top+chrome.bottom), TRUE);

        // Add tabs to the tab control
        {
            static char textSetup[] = TEXT("Setup");
            static char textMessageLog[] = TEXT("Message Log");

            hwnd = GetDlgItem(hwndDlg, WIN_STARTWIN_TABCTL);

            TCITEM tab = {};
            tab.mask = TCIF_TEXT;
            tab.pszText = textSetup;
            SendMessage(hwnd, TCM_INSERTITEM, (WPARAM)TAB_CONFIG, (LPARAM)&tab);
            tab.mask = TCIF_TEXT;
            tab.pszText = textMessageLog;
            SendMessage(hwnd, TCM_INSERTITEM, (WPARAM)TAB_MESSAGES, (LPARAM)&tab);

            // Work out the position and size of the area inside the tab control for the pages
            ZeroMemory(&r, sizeof(r));
            GetClientRect(hwnd, &r);
            SendMessage(hwnd, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
            r.right -= r.left-1;
            r.bottom -= r.top-1;
            r.top += rtab.top;
            r.left += rtab.left;

            // Create the pages and position them in the tab control, but hide them
            pages[TAB_CONFIG] = CreateDialog((HINSTANCE)win_gethinstance(), MAKEINTRESOURCE(WIN_STARTWINPAGE_CONFIG), hwndDlg, ConfigPageProc);
            SetWindowPos(pages[TAB_CONFIG], hwnd, r.left, r.top, r.right, r.bottom, SWP_HIDEWINDOW);

            pages[TAB_MESSAGES] = GetDlgItem(hwndDlg, WIN_STARTWIN_MESSAGES);
            SetWindowPos(pages[TAB_MESSAGES], hwnd, r.left, r.top, r.right, r.bottom, SWP_HIDEWINDOW);

            // Tell the editfield acting as the console to exclude the width of the scrollbar
            GetClientRect(pages[TAB_MESSAGES], &r);
            r.right -= GetSystemMetrics(SM_CXVSCROLL)+4;
            r.left = r.top = 0;
            SendMessage(pages[TAB_MESSAGES], EM_SETRECTNP,0,(LPARAM)&r);

            // Set a tab stop in the game data listbox
            {
                DWORD tabs[1] = { 150 };
                (void)ListBox_SetTabStops(GetDlgItem(pages[TAB_CONFIG], IDCDATA), 1, tabs);
            }

            SetFocus(GetDlgItem(hwndDlg, WIN_STARTWIN_START));
            SetWindowText(hwndDlg, apptitle);
        }
        return FALSE;
    }

    #define STARTWIN_WIN32_SHARED_CASE_NOTIFY
    #include "../../build/include/startwin_win32_shared.cpp.inc"

    #define STARTWIN_WIN32_SHARED_CASE_CLOSE_DESTROY_COMMAND_BASIC
    #include "../../build/include/startwin_win32_shared.cpp.inc"

    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == pages[TAB_MESSAGES])
            return (BOOL)(intptr_t)GetSysColorBrush(COLOR_WINDOW);
        break;

    default:
        break;
    }

    return FALSE;
}
#define STARTWIN_WIN32_SHARED_ISOPEN
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_OPEN
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_CLOSE
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_PUTS
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_SETTITLE
#include "../../build/include/startwin_win32_shared.cpp.inc"
#define STARTWIN_WIN32_SHARED_IDLE
#include "../../build/include/startwin_win32_shared.cpp.inc"

int32_t startwin_run(void)
{
    if (!startupdlg) return 1;

    done = -1;

    SetPage(TAB_CONFIG);
    EnableConfig(1);

#ifdef POLYMER
    settings.polymer = (glrendmode == REND_POLYMER);
#else
    settings.polymer = 0;
#endif

    settings.shared = ud.setup;
    settings.grp = g_selectedGrp;
    settings.gamedir = g_modDir;

    PopulateForm(-1);

    do
    {
        MSG msg;

        switch (GetMessage(&msg, NULL, 0,0))
        {
        case 0:
            done = 1;
            break;
        case -1:
            return -1;
        default:
            if (IsWindow(startupdlg) && IsDialogMessage(startupdlg, &msg))
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            break;
        }
    }
    while (done < 0);

    SetPage(TAB_MESSAGES);
    EnableConfig(0);

    if (done)
    {
        ud.setup = settings.shared;
#ifdef USE_OPENGL
        glrendmode = (settings.polymer) ? REND_POLYMER : REND_POLYMOST;
#endif
        g_selectedGrp = settings.grp;
        if (g_modDir != settings.gamedir)
            Bstrcpy(g_modDir, (g_noSetup == 0 && settings.gamedir != NULL) ? settings.gamedir : "/");
    }

    return done;
}

#endif // STARTUP_SETUP_WINDOW
