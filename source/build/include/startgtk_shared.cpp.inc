// Shared GTK startup-window helpers used by the Build game frontends.
// Include with exactly one STARTGTK_SHARED_* selector defined.

#if defined(STARTGTK_SHARED_GET_MODS_DIR_NAMES_DUKE_BLOOD)

static unsigned char GetModsDirNames(GtkListStore *list)
{
    char *homedir;
    char pdir[BMAX_PATH];
    unsigned char iternumb = 0;
    BUILDVFS_FIND_REC *dirs = NULL;
    GtkTreeIter iter;

    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    if ((homedir = Bgethomedir()))
    {
        if (buildvfs_exists("user_profiles_disabled"))
            buildvfs_getcwd(pdir, sizeof(pdir));
        else
            Bsnprintf(pdir, sizeof(pdir), "%s/.config/" APPBASENAME, homedir);

        dirs = klistpath(pdir, "*", BUILDVFS_FIND_DIR);
        for (; dirs != NULL; dirs=dirs->next)
        {
            if ((Bstrcmp(dirs->name, "autoload") == 0) ||
                    (Bstrcmp(dirs->name, "..") == 0) ||
                    (Bstrcmp(dirs->name, ".") == 0))
                continue;
            else
            {
                gtk_list_store_append(list, &iter);
                gtk_list_store_set(list, &iter, 0,dirs->name, -1);
                iternumb++;
            }
        }
    }

    klistfree(dirs);
    dirs = NULL;

    pathsearchmode = bakpathsearchmode;
    return iternumb;
}
#undef STARTGTK_SHARED_GET_MODS_DIR_NAMES_DUKE_BLOOD
#endif

#if defined(STARTGTK_SHARED_GET_MODS_DIR_NAMES_RR_NNB)

static unsigned char GetModsDirNames(GtkListStore *list)
{
    char *homedir;
    char pdir[BMAX_PATH];
    unsigned char iternumb = 0;
    BUILDVFS_FIND_REC *dirs = NULL;
    GtkTreeIter iter;

    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    if ((homedir = Bgethomedir()))
    {
        Bsnprintf(pdir, sizeof(pdir), "%s/" ".eduke32", homedir);
        dirs = klistpath(pdir, "*", BUILDVFS_FIND_DIR);
        for (; dirs != NULL; dirs=dirs->next)
        {
            if ((Bstrcmp(dirs->name, "autoload") == 0) ||
                    (Bstrcmp(dirs->name, "..") == 0) ||
                    (Bstrcmp(dirs->name, ".") == 0))
                continue;
            else
            {
                gtk_list_store_append(list, &iter);
                gtk_list_store_set(list, &iter, 0,dirs->name, -1);
                iternumb++;
            }
        }
    }

    klistfree(dirs);
    dirs = NULL;

    pathsearchmode = bakpathsearchmode;
    return iternumb;
}
#undef STARTGTK_SHARED_GET_MODS_DIR_NAMES_RR_NNB
#endif

#if defined(STARTGTK_SHARED_ON_VMODE3DCOMBO_CHANGED)

static void on_vmode3dcombo_changed(GtkComboBox *combobox, gpointer user_data)
{
    GtkTreeModel *data;
    GtkTreeIter iter;
    int32_t val;
    UNREFERENCED_PARAMETER(user_data);

    if (!gtk_combo_box_get_active_iter(combobox, &iter)) return;
    if (!(data = gtk_combo_box_get_model(combobox))) return;
    gtk_tree_model_get(data, &iter, 1, &val, -1);
    settings.shared.xdim = validmode[val].xdim;
    settings.shared.ydim = validmode[val].ydim;
    settings.shared.bpp = validmode[val].bpp;
}
#undef STARTGTK_SHARED_ON_VMODE3DCOMBO_CHANGED
#endif

#if defined(STARTGTK_SHARED_ON_POLYMERCHECK_TOGGLED)
static void on_polymercheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    if (gtk_toggle_button_get_active(togglebutton))
    {
        glrendmode = REND_POLYMER;
        settings.polymer = TRUE;
        if (settings.shared.bpp == 8)
        {
            settings.shared.bpp = 32;
            PopulateForm(POPULATE_VIDEO);
        }
    }
    else
    {
        glrendmode = REND_POLYMOST;
        settings.polymer = FALSE;
    }
}
#undef STARTGTK_SHARED_ON_POLYMERCHECK_TOGGLED
#endif

#if defined(STARTGTK_SHARED_ON_INPUTDEVCOMBO_CHANGED)

static void on_inputdevcombo_changed(GtkComboBox *combobox, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    switch (gtk_combo_box_get_active(combobox))
    {
    case 0: settings.shared.usemouse = 0; settings.shared.usejoystick = 0; break;
    case 1:	settings.shared.usemouse = 1; settings.shared.usejoystick = 0; break;
    case 2:	settings.shared.usemouse = 0; settings.shared.usejoystick = 1; break;
    case 3:	settings.shared.usemouse = 1; settings.shared.usejoystick = 1; break;
    }
}
#undef STARTGTK_SHARED_ON_INPUTDEVCOMBO_CHANGED
#endif

#if defined(STARTGTK_SHARED_ON_CUSTOMMODCOMBO_CHANGED)

static void on_custommodcombo_changed(GtkComboBox *combobox, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreePath *path;
    char *value;
    UNREFERENCED_PARAMETER(user_data);

    if (gtk_combo_box_get_active_iter(combobox, &iter))
    {
        model = gtk_combo_box_get_model(combobox);
        gtk_tree_model_get(model, &iter, 0,&value, -1);
        path = gtk_tree_model_get_path(model, &iter);

        if (*gtk_tree_path_get_indices(path) == NONE)
            settings.gamedir = NULL;
        else settings.gamedir = value;
    }
}
#undef STARTGTK_SHARED_ON_CUSTOMMODCOMBO_CHANGED
#endif

#if defined(STARTGTK_SHARED_ON_STARTWIN_DELETE_EVENT)

static gboolean on_startwin_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    UNREFERENCED_PARAMETER(widget);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(user_data);
    if (mode == TAB_CONFIG) { retval = 0; gtk_main_quit(); }
    else quitevent++;
    return TRUE;	// FALSE would let the event go through. we want the game to decide when to close
}
#undef STARTGTK_SHARED_ON_STARTWIN_DELETE_EVENT
#endif

#if defined(STARTGTK_SHARED_ON_GAMELIST_SELECTION_CHANGED)

static void on_gamelist_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    UNREFERENCED_PARAMETER(user_data);

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        grpfile_t const *fg;
        gtk_tree_model_get(model, &iter, 2, (gpointer)&fg, -1);
        settings.grp = fg;
    }
}
#undef STARTGTK_SHARED_ON_GAMELIST_SELECTION_CHANGED
#endif

#if defined(STARTGTK_SHARED_SET_PAGE)

static void SetPage(int32_t n)
{
    if (!gtkenabled || !stwidgets.startwin) return;
    mode = n;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(stwidgets.tabs), n);

    // each control in the config page vertical layout plus the start button should be made (in)sensitive
    if (n == TAB_CONFIG) n = TRUE; else n = FALSE;
    gtk_widget_set_sensitive(stwidgets.startbutton, n);
    gtk_container_foreach(GTK_CONTAINER(stwidgets.configtlayout),
                          (GtkCallback)gtk_widget_set_sensitive,
                          (gpointer)&n);
}
#undef STARTGTK_SHARED_SET_PAGE
#endif

#if defined(STARTGTK_SHARED_NAME_SORTER)

static gint name_sorter(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
    gchar *as, *bs;
    gint r;
    UNREFERENCED_PARAMETER(user_data);
    gtk_tree_model_get(model, a, 0, &as, -1);
    gtk_tree_model_get(model, b, 0, &bs, -1);

    r = g_utf8_collate(as,bs);

    g_free(as);
    g_free(bs);

    return r;
}
#undef STARTGTK_SHARED_NAME_SORTER
#endif

#if defined(STARTGTK_SHARED_WINDOW_SCAFFOLD)
    stwidgets.startwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(stwidgets.startwin), apptitle);	// NOTE: use global app title
    gtk_window_set_position(GTK_WINDOW(stwidgets.startwin), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(stwidgets.startwin), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(stwidgets.startwin), GDK_WINDOW_TYPE_HINT_DIALOG);

    // Horizontal layout of banner and controls
    stwidgets.hlayout = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.startwin), stwidgets.hlayout);

    // banner
    {
        GdkPixbuf *pixbuf = load_banner();
        stwidgets.banner = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref((gpointer)pixbuf);
    }
    gtk_box_pack_start(GTK_BOX(stwidgets.hlayout), stwidgets.banner, FALSE, FALSE, 0);
    gtk_misc_set_alignment(GTK_MISC(stwidgets.banner), 0.5, 0);

    // Vertical layout of tab control and start+cancel buttons
    stwidgets.vlayout = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stwidgets.hlayout), stwidgets.vlayout, TRUE, TRUE, 0);

    // Tab control
    stwidgets.tabs = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(stwidgets.vlayout), stwidgets.tabs, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.tabs), 4);

    // layout table of config page
    stwidgets.configtlayout = gtk_table_new(6, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.configtlayout);

#undef STARTGTK_SHARED_WINDOW_SCAFFOLD
#endif

#if defined(STARTGTK_SHARED_GAME_DATA_TAB)
#ifndef STARTGTK_TREE_COLUMN_NULL
# define STARTGTK_TREE_COLUMN_NULL nullptr
#endif
    // Game data layout
    stwidgets.gamevlayout = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.gamevlayout);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.gamevlayout), 4);

    // Game data field LabelText
    stwidgets.gamelabel = gtk_label_new_with_mnemonic("_Game:");
    gtk_box_pack_start(GTK_BOX(stwidgets.gamevlayout), stwidgets.gamelabel, FALSE, FALSE, 0);
    gtk_misc_set_alignment(GTK_MISC(stwidgets.gamelabel), 0, 0.5);

    // Game data scrollable area
    stwidgets.gamescroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(stwidgets.gamevlayout), stwidgets.gamescroll, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stwidgets.gamescroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(stwidgets.gamescroll), GTK_SHADOW_IN);

    // Game data list
    {
        GtkListStore *list = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
        GtkCellRenderer *cell;
        GtkTreeViewColumn *col;

        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list), 0, name_sorter, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list), 0, GTK_SORT_ASCENDING);

        stwidgets.gamelist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        col = gtk_tree_view_column_new_with_attributes("Game", cell, "text", 0, STARTGTK_TREE_COLUMN_NULL);
        gtk_tree_view_column_set_expand(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(stwidgets.gamelist), col);
        col = gtk_tree_view_column_new_with_attributes("GRP file", cell, "text", 1, STARTGTK_TREE_COLUMN_NULL);
        gtk_tree_view_column_set_min_width(col, 64);
        gtk_tree_view_append_column(GTK_TREE_VIEW(stwidgets.gamelist), col);
    }
    gtk_container_add(GTK_CONTAINER(stwidgets.gamescroll), stwidgets.gamelist);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(stwidgets.gamelist), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(stwidgets.gamelist), FALSE);

    // Game tab
    stwidgets.gametab = gtk_label_new("Game");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), 1), stwidgets.gametab);

#undef STARTGTK_TREE_COLUMN_NULL
#undef STARTGTK_SHARED_GAME_DATA_TAB
#endif

#if defined(STARTGTK_SHARED_CONFIG_FOOTER)
    // Empty horizontal layout
    stwidgets.emptyhlayout = gtk_hbox_new(TRUE, 0);
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.emptyhlayout, 0,3, 3,4, (GtkAttachOptions)0,
        (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 4, 0);

#ifdef STARTGTK_CONFIG_FOOTER_HAS_AUTOLOAD
    // Autoload checkbox
    stwidgets.autoloadcheck = gtk_check_button_new_with_mnemonic("_Enable \"autoload\" folder");
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.autoloadcheck, 0,3, 4,5, GTK_FILL, (GtkAttachOptions)0, 2, 2);
#endif

    // Always show config checkbox
    stwidgets.alwaysshowcheck = gtk_check_button_new_with_mnemonic("_Always show this window at startup");
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.alwaysshowcheck, 0,3, 5,6, GTK_FILL, (GtkAttachOptions)0, 2, 2);

    // Configuration tab
    stwidgets.configtab = gtk_label_new("Configuration");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), 0), stwidgets.configtab);

#undef STARTGTK_CONFIG_FOOTER_HAS_AUTOLOAD
#undef STARTGTK_SHARED_CONFIG_FOOTER
#endif

#if defined(STARTGTK_SHARED_MESSAGE_TAB)
#ifndef STARTGTK_MESSAGES_TAB_INDEX
# define STARTGTK_MESSAGES_TAB_INDEX 2
#endif
    // Messages scrollable area
    stwidgets.messagesscroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.messagesscroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stwidgets.messagesscroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    // Messages text area
    stwidgets.messagestext = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.messagesscroll), stwidgets.messagestext);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(stwidgets.messagestext), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(stwidgets.messagestext), GTK_WRAP_WORD);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(stwidgets.messagestext), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(stwidgets.messagestext), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(stwidgets.messagestext), 2);

    // Messages tab
    stwidgets.messagestab = gtk_label_new("Messages");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), STARTGTK_MESSAGES_TAB_INDEX), stwidgets.messagestab);

#undef STARTGTK_MESSAGES_TAB_INDEX
#undef STARTGTK_SHARED_MESSAGE_TAB
#endif

#if defined(STARTGTK_SHARED_DIALOG_BUTTONS)
    // Dialogue box buttons layout
    stwidgets.buttons = gtk_hbutton_box_new();
    gtk_box_pack_start(GTK_BOX(stwidgets.vlayout), stwidgets.buttons, FALSE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.buttons), 3);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(stwidgets.buttons), GTK_BUTTONBOX_END);

    // Cancel button
    stwidgets.cancelbutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.buttons), stwidgets.cancelbutton);
    GTK_WIDGET_SET_FLAGS(stwidgets.cancelbutton, GTK_CAN_DEFAULT);

    stwidgets.cancelbuttonalign = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.cancelbutton), stwidgets.cancelbuttonalign);

    stwidgets.cancelbuttonlayout = gtk_hbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(stwidgets.cancelbuttonalign), stwidgets.cancelbuttonlayout);

    stwidgets.cancelbuttonicon = gtk_image_new_from_stock("gtk-cancel", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(stwidgets.cancelbuttonlayout), stwidgets.cancelbuttonicon, FALSE, FALSE, 0);

    stwidgets.cancelbuttonlabel = gtk_label_new_with_mnemonic("_Cancel");
    gtk_box_pack_start(GTK_BOX(stwidgets.cancelbuttonlayout), stwidgets.cancelbuttonlabel, FALSE, FALSE, 0);

    // Start button
    stwidgets.startbutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.buttons), stwidgets.startbutton);
    GTK_WIDGET_SET_FLAGS(stwidgets.startbutton, GTK_CAN_DEFAULT);

    gtk_window_set_default(GTK_WINDOW(stwidgets.startwin), stwidgets.startbutton);

    stwidgets.startbuttonalign = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.startbutton), stwidgets.startbuttonalign);

    stwidgets.startbuttonlayout = gtk_hbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(stwidgets.startbuttonalign), stwidgets.startbuttonlayout);

    stwidgets.startbuttonicon = gtk_image_new_from_stock("gtk-execute", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(stwidgets.startbuttonlayout), stwidgets.startbuttonicon, FALSE, FALSE, 0);

    stwidgets.startbuttonlabel = gtk_label_new_with_mnemonic("_Start");
    gtk_box_pack_start(GTK_BOX(stwidgets.startbuttonlayout), stwidgets.startbuttonlabel, FALSE, FALSE, 0);

#undef STARTGTK_SHARED_DIALOG_BUTTONS
#endif

#if defined(STARTGTK_SHARED_BUILD_ENTRY_POINTS)

bool startwin_isopen(void)
{
    return gtkenabled && stwidgets.startwin;
}

int32_t startwin_open(void)
{
    if (!gtkenabled) return 0;
    if (stwidgets.startwin) return 1;

    stwidgets.startwin = create_window();
    if (stwidgets.startwin)
    {
        SetPage(TAB_MESSAGES);
        gtk_widget_show_all(stwidgets.startwin);
        gtk_main_iteration_do(FALSE);
        return 0;
    }
    return -1;
}

int32_t startwin_close(void)
{
    if (!gtkenabled) return 0;
    if (!stwidgets.startwin) return 1;
    gtk_widget_destroy(stwidgets.startwin);
    stwidgets.startwin = NULL;
    return 0;
}

#define STARTGTK_SHARED_STARTWIN_PUTS
#endif

#if defined(STARTGTK_SHARED_STARTWIN_PUTS)

int32_t startwin_puts(const char *str)
{
    GtkWidget *textview;
    GtkTextBuffer *textbuffer;
    GtkTextIter enditer;
    GtkTextMark *mark;
    const char *aptr, *bptr;

    if (!gtkenabled || !str) return 0;
    if (!stwidgets.startwin) return 1;
    if (!(textview = stwidgets.messagestext)) return -1;
    textbuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    gtk_text_buffer_get_end_iter(textbuffer, &enditer);
    for (aptr = bptr = str; *aptr != 0;)
    {
        switch (*bptr)
        {
        case '\b':
            if (bptr > aptr)
                gtk_text_buffer_insert(textbuffer, &enditer, (const gchar *)aptr, (gint)(bptr-aptr)-1);
#if GTK_CHECK_VERSION(2,6,0)
            gtk_text_buffer_backspace(textbuffer, &enditer, FALSE, TRUE);
#else
            {
                GtkTextIter iter2 = enditer;
                gtk_text_iter_backward_cursor_position(&iter2);
                //FIXME: this seems be deleting one too many chars somewhere!
                if (!gtk_text_iter_equal(&iter2, &enditer))
                    gtk_text_buffer_delete_interactive(textbuffer, &iter2, &enditer, TRUE);
            }
#endif
            aptr = ++bptr;
            break;
        case 0:
            if (bptr > aptr)
                gtk_text_buffer_insert(textbuffer, &enditer, (const gchar *)aptr, (gint)(bptr-aptr));
            aptr = bptr;
            break;
        case '\r':	// FIXME
        default:
            bptr++;
            break;
        }
    }

    mark = gtk_text_buffer_create_mark(textbuffer, NULL, &enditer, 1);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(textview), mark, 0.0, FALSE, 0.0, 1.0);
    gtk_text_buffer_delete_mark(textbuffer, mark);

    return 0;
}
#undef STARTGTK_SHARED_STARTWIN_PUTS
#endif

#if defined(STARTGTK_SHARED_BUILD_ENTRY_POINTS)

int32_t startwin_settitle(const char *title)
{
    if (!gtkenabled) return 0;
    if (!stwidgets.startwin) return 1;
    gtk_window_set_title(GTK_WINDOW(stwidgets.startwin), title);
    return 0;
}

int32_t startwin_idle(void *s)
{
    UNREFERENCED_PARAMETER(s);
    if (!gtkenabled) return 0;
    //if (!stwidgets.startwin) return 1;
    gtk_main_iteration_do(FALSE);
    return 0;
}

#undef STARTGTK_SHARED_BUILD_ENTRY_POINTS
#endif
