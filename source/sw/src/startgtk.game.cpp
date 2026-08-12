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

#include "build.h"
#include "common.h"
#include "common_game.h"
#include "compat.h"
#include "dynamicgtk.h"
#include "game.h"
#include "grpscan.h"
#include "gtkpixdata.h"
#include "config.h"

#if defined POLYMER && 0
# define POLYMEROPTION
#endif

enum
{
    NONE,
    ALL,
    POPULATE_VIDEO,
    POPULATE_CONFIG,
    POPULATE_GAME,
};

enum
{
    TAB_CONFIG,
    TAB_GAME,
    TAB_MESSAGES,
};

enum
{
    INPUT_KB,
    INPUT_MOUSE,
    INPUT_JOYSTICK,
    INPUT_ALL,
};

static struct
{
    GtkWidget *startwin;
    GtkWidget *hlayout;
    GtkWidget *banner;
    GtkWidget *vlayout;
    GtkWidget *tabs;
    GtkWidget *configtlayout;
    GtkWidget *displayvlayout;
    GtkWidget *vmode3dlabel;
    GtkWidget *vmode3dcombo;
    GtkWidget *fullscreencheck;
#ifdef POLYMEROPTION
    GtkWidget *polymercheck;
#endif
    GtkWidget *inputdevlabel;
    GtkWidget *inputdevcombo;
    GtkWidget *emptyhlayout;
    GtkWidget *alwaysshowcheck;
    GtkWidget *configtab;
    GtkWidget *gamevlayout;
    GtkWidget *gamelabel;
    GtkWidget *gamescroll;
    GtkWidget *gamelist;
    GtkWidget *gametab;
    GtkWidget *messagesscroll;
    GtkWidget *messagestext;
    GtkWidget *messagestab;
    GtkWidget *buttons;
    GtkWidget *cancelbutton;
    GtkWidget *cancelbuttonalign;
    GtkWidget *cancelbuttonlayout;
    GtkWidget *cancelbuttonicon;
    GtkWidget *cancelbuttonlabel;
    GtkWidget *startbutton;
    GtkWidget *startbuttonalign;
    GtkWidget *startbuttonlayout;
    GtkWidget *startbuttonicon;
    GtkWidget *startbuttonlabel;
} stwidgets;

static struct
{
    grpfile_t const * grp;
    ud_setup_t shared;
#ifdef POLYMEROPTION
    int polymer;
#endif
} settings;

static int32_t retval = -1, mode = TAB_MESSAGES;
extern int32_t gtkenabled;
static void PopulateForm(unsigned char pgs);


// -- EVENT CALLBACKS AND CREATION STUFF --------------------------------------
#define STARTGTK_SHARED_ON_VMODE3DCOMBO_CHANGED
#include "../../build/include/startgtk_shared.cpp.inc"

static void on_fullscreencheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    settings.shared.fullscreen = gtk_toggle_button_get_active(togglebutton);
    PopulateForm(POPULATE_VIDEO);
}

#ifdef POLYMEROPTION
#define STARTGTK_SHARED_ON_POLYMERCHECK_TOGGLED
#include "../../build/include/startgtk_shared.cpp.inc"
#endif
#define STARTGTK_SHARED_ON_INPUTDEVCOMBO_CHANGED
#include "../../build/include/startgtk_shared.cpp.inc"

static void on_alwaysshowcheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    settings.shared.forcesetup = gtk_toggle_button_get_active(togglebutton);
}

static void on_cancelbutton_clicked(GtkButton *button, gpointer user_data)
{
    UNREFERENCED_PARAMETER(button);
    UNREFERENCED_PARAMETER(user_data);
    if (mode == TAB_CONFIG) { retval = 0; gtk_main_quit(); }
    else quitevent++;
}

static void on_startbutton_clicked(GtkButton *button, gpointer user_data)
{
    UNREFERENCED_PARAMETER(button);
    UNREFERENCED_PARAMETER(user_data);
    retval = 1;
    gtk_main_quit();
}
#define STARTGTK_SHARED_ON_GAMELIST_SELECTION_CHANGED
#include "../../build/include/startgtk_shared.cpp.inc"
#define STARTGTK_SHARED_ON_STARTWIN_DELETE_EVENT
#include "../../build/include/startgtk_shared.cpp.inc"


// -- SUPPORT FUNCTIONS -------------------------------------------------------

static GdkPixbuf *load_banner(void)
{
    return gdk_pixbuf_from_pixdata((GdkPixdata const *)&startbanner_pixdata, FALSE, NULL);
}
#define STARTGTK_SHARED_SET_PAGE
#include "../../build/include/startgtk_shared.cpp.inc"

static void PopulateForm(unsigned char pgs)
{
    if ((pgs == ALL) || (pgs == POPULATE_VIDEO))
    {
        int32_t mode3d, i;
        GtkListStore *modes3d;
        GtkTreeIter iter;
        char buf[64];

        mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, settings.shared.bpp, settings.shared.fullscreen, 1);
        if (mode3d < 0)
        {
            int32_t i, cd[] = { 32, 24, 16, 15, 8, 0 };

            for (i=0; cd[i];) { if (cd[i] >= settings.shared.bpp) i++; else break; }
            for (; cd[i]; i++)
            {
                mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, cd[i], settings.shared.fullscreen, 1);
                if (mode3d < 0) continue;
                settings.shared.bpp = cd[i];
                break;
            }
        }

        modes3d = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(stwidgets.vmode3dcombo)));
        gtk_list_store_clear(modes3d);

        for (i=0; i<validmodecnt; i++)
        {
            if (validmode[i].fs != settings.shared.fullscreen) continue;

            // all modes get added to the 3D mode list
            Bsprintf(buf, "%dx%d %s", validmode[i].xdim, validmode[i].ydim, validmode[i].bpp == 8 ? "software" : "OpenGL");
            gtk_list_store_append(modes3d, &iter);
            gtk_list_store_set(modes3d, &iter, 0,buf, 1,i, -1);
            if (i == mode3d)
            {
                g_signal_handlers_block_by_func(stwidgets.vmode3dcombo, (gpointer)on_vmode3dcombo_changed, NULL);
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(stwidgets.vmode3dcombo), &iter);
                g_signal_handlers_unblock_by_func(stwidgets.vmode3dcombo, (gpointer)on_vmode3dcombo_changed, NULL);
            }
        }
    }

    if ((pgs == ALL) || (pgs == POPULATE_CONFIG))
    {
        GtkTreeIter iter;
        unsigned char i;
        const char *availabledev[] =
        {
            "Keyboard only",
            "Keyboard and mouse",
            "Keyboard and joystick",
            "All supported devices"
        };

        // populate input devices combo
        GtkListStore * devlist = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(stwidgets.inputdevcombo)));
        gtk_list_store_clear(devlist);

        for (i=0; i<(int32_t)G_N_ELEMENTS(availabledev); i++)
        {
            gtk_list_store_append(devlist, &iter);
            gtk_list_store_set(devlist, &iter, 0,availabledev[i], -1);
        }
        switch (settings.shared.usemouse)
        {
        case 0: if (settings.shared.usejoystick)
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_JOYSTICK);
            else
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_KB);
            break;
        case 1:	if (settings.shared.usejoystick)
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_ALL);
            else
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_MOUSE);
            break;
        }

        // populate check buttons
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.fullscreencheck), settings.shared.fullscreen);
#ifdef POLYMEROPTION
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.polymercheck), settings.polymer);
#endif
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.alwaysshowcheck), settings.shared.forcesetup);
    }

    if ((pgs == ALL) || (pgs == POPULATE_GAME))
    {
        GtkListStore *list;
        GtkTreeIter iter;
        GtkTreeView *gamelist;

        gamelist = GTK_TREE_VIEW(stwidgets.gamelist);
        list = GTK_LIST_STORE(gtk_tree_view_get_model(gamelist));
        gtk_list_store_clear(list);

        for (grpfile_t const * fg = foundgrps; fg; fg=fg->next)
        {
            gtk_list_store_append(list, &iter);
            gtk_list_store_set(list, &iter, 0, fg->type->name, 1, fg->filename, 2, (void const *)fg, -1);
            if (settings.grp == fg)
            {
                GtkTreeSelection *sel = gtk_tree_view_get_selection(gamelist);
                g_signal_handlers_block_by_func(sel, (gpointer)on_gamelist_selection_changed, NULL);
                gtk_tree_selection_select_iter(sel, &iter);
                g_signal_handlers_unblock_by_func(sel, (gpointer)on_gamelist_selection_changed, NULL);
            }
        }
    }
}
#define STARTGTK_SHARED_NAME_SORTER
#include "../../build/include/startgtk_shared.cpp.inc"

static GtkWidget *create_window(void)
{
    // Basic window
#define STARTGTK_SHARED_WINDOW_SCAFFOLD
#include "../../build/include/startgtk_shared.cpp.inc"

    // 3D video mode LabelText
    stwidgets.vmode3dlabel = gtk_label_new_with_mnemonic("_Video mode:");
    gtk_misc_set_alignment(GTK_MISC(stwidgets.vmode3dlabel), 0.3, 0);
#ifdef POLYMEROPTION
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dlabel, 0,1, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 0);
#else
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dlabel, 0,1, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 7);
#endif

    // 3D video mode combo
    {
        GtkListStore *list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
        GtkCellRenderer *cell;

        stwidgets.vmode3dcombo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(stwidgets.vmode3dcombo), cell, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(stwidgets.vmode3dcombo), cell, "text", 0, nullptr);
    }

#ifdef POLYMEROPTION
   gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dcombo, 1,2, 0,1,
       (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 0);
#else
   gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dcombo, 1,2, 0,1, (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 7);
#endif

    // Fullscreen checkbox
    stwidgets.displayvlayout = gtk_vbox_new(TRUE, 0);
#ifdef POLYMEROPTION
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.displayvlayout, 2,3, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 0);
#else
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.displayvlayout, 2,3, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 7);
#endif

    stwidgets.fullscreencheck = gtk_check_button_new_with_mnemonic("_Fullscreen");
    gtk_box_pack_start(GTK_BOX(stwidgets.displayvlayout), stwidgets.fullscreencheck, FALSE, FALSE, 0);

#ifdef POLYMEROPTION
    // Polymer checkbox
    stwidgets.polymercheck = gtk_check_button_new_with_mnemonic("_Polymer");
    gtk_box_pack_start(GTK_BOX(stwidgets.displayvlayout), stwidgets.polymercheck, FALSE, FALSE, 0);
#endif

    // Input devices LabelText
    stwidgets.inputdevlabel = gtk_label_new_with_mnemonic("_Input devices:");
    gtk_misc_set_alignment(GTK_MISC(stwidgets.inputdevlabel), 0.3, 0);
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.inputdevlabel, 0,1, 1,2, GTK_FILL, (GtkAttachOptions)0, 4, 0);

    // Input devices combo
    {
        GtkListStore *list = gtk_list_store_new(1, G_TYPE_STRING);
        GtkCellRenderer *cell;

        stwidgets.inputdevcombo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(stwidgets.inputdevcombo), cell, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(stwidgets.inputdevcombo), cell, "text", 0, nullptr);
    }
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.inputdevcombo, 1,2, 1,2,
        (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 0);

#define STARTGTK_SHARED_CONFIG_FOOTER
#include "../../build/include/startgtk_shared.cpp.inc"

#define STARTGTK_SHARED_GAME_DATA_TAB
#include "../../build/include/startgtk_shared.cpp.inc"

#define STARTGTK_SHARED_MESSAGE_TAB
#include "../../build/include/startgtk_shared.cpp.inc"

#define STARTGTK_SHARED_DIALOG_BUTTONS
#include "../../build/include/startgtk_shared.cpp.inc"

    // Wire up the signals
    g_signal_connect((gpointer) stwidgets.startwin, "delete_event",
                     G_CALLBACK(on_startwin_delete_event),
                     NULL);
    g_signal_connect((gpointer) stwidgets.vmode3dcombo, "changed",
                     G_CALLBACK(on_vmode3dcombo_changed),
                     NULL);
    g_signal_connect((gpointer) stwidgets.fullscreencheck, "toggled",
                     G_CALLBACK(on_fullscreencheck_toggled),
                     NULL);
#ifdef POLYMEROPTION
    g_signal_connect((gpointer) stwidgets.polymercheck, "toggled",
                     G_CALLBACK(on_polymercheck_toggled),
                     NULL);
#endif
    g_signal_connect((gpointer) stwidgets.inputdevcombo, "changed",
                     G_CALLBACK(on_inputdevcombo_changed),
                     NULL);
    g_signal_connect((gpointer) stwidgets.alwaysshowcheck, "toggled",
                     G_CALLBACK(on_alwaysshowcheck_toggled),
                     NULL);
    g_signal_connect((gpointer) stwidgets.cancelbutton, "clicked",
                     G_CALLBACK(on_cancelbutton_clicked),
                     NULL);
    g_signal_connect((gpointer) stwidgets.startbutton, "clicked",
                     G_CALLBACK(on_startbutton_clicked),
                     NULL);
    {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(stwidgets.gamelist));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
        g_signal_connect((gpointer) sel, "changed",
                         G_CALLBACK(on_gamelist_selection_changed),
                         NULL);
    }

    // Associate labels with their controls
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.vmode3dlabel), stwidgets.vmode3dcombo);
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.inputdevlabel), stwidgets.inputdevcombo);
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.gamelabel), stwidgets.gamelist);

    return stwidgets.startwin;
}


// -- BUILD ENTRY POINTS ------------------------------------------------------
#define STARTGTK_SHARED_BUILD_ENTRY_POINTS
#include "../../build/include/startgtk_shared.cpp.inc"

int32_t startwin_run(void)
{
    if (!gtkenabled) return 1;
    if (!stwidgets.startwin) return 1;

    SetPage(TAB_CONFIG);

    settings.shared = ud_setup;
    settings.grp = g_selectedGrp;
#ifdef POLYMEROPTION
    settings.polymer = (glrendmode == REND_POLYMER) & (settings.shared.bpp != 8);
#endif
    PopulateForm(ALL);

    gtk_main();

    SetPage(TAB_MESSAGES);
    if (retval) // launch the game with these parameters
    {
        ud_setup = settings.shared;
#ifdef POLYMEROPTION
        glrendmode = (settings.polymer) ? REND_POLYMER : REND_POLYMOST;
#endif
        g_selectedGrp = settings.grp;

    }

    return retval;
}
