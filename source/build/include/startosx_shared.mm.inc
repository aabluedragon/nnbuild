// Shared macOS startup-window helpers used by the Build game frontends.
// Include with STARTOSX_SHARED_APPLICATION_MENUS or STARTOSX_SHARED_STARTWIN_COMMON defined.

#if defined(STARTOSX_SHARED_APPLICATION_MENUS)
#ifndef STARTOSX_SHARED_MENU_INLINE
# define STARTOSX_SHARED_MENU_INLINE
# define STARTOSX_SHARED_MENU_INLINE_WAS_UNDEFINED
#endif

static STARTOSX_SHARED_MENU_INLINE NSString * GetApplicationName(void)
{
    NSString *appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleDisplayName"];
    if (!appName)
        appName = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];

    return appName;
}

static STARTOSX_SHARED_MENU_INLINE void CreateApplicationMenus(void)
{
    NSString *appName;
    NSString *title;
    NSMenu *rootMenu;
    NSMenu *serviceMenu;
    NSMenuItem *menuItem;

    NSMenu *mainMenu = [[NSMenu alloc] init];

    /* Create the application menu */
    appName = GetApplicationName();
    rootMenu = [[NSMenu alloc] init];

    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:rootMenu];
    [mainMenu addItem:menuItem];
    [menuItem release];

    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [rootMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

    [rootMenu addItem:[NSMenuItem separatorItem]];

    serviceMenu = [[NSMenu alloc] init];
    menuItem = (NSMenuItem *)[rootMenu addItemWithTitle:@"Services" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:serviceMenu];

    [nsapp setServicesMenu:serviceMenu];
    [serviceMenu release];

    [rootMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Hide " stringByAppendingString:appName];
    [rootMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

    menuItem = (NSMenuItem *)[rootMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSEventModifierFlagOption|NSEventModifierFlagCommand)];

    [rootMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [rootMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [rootMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];

    /* Create the main menu bar */
    [nsapp setMainMenu:mainMenu];
    [mainMenu release];  /* we're done with it, let NSApp own it. */

    /* Tell the application object that this is now the application menu */
    [nsapp setAppleMenu:rootMenu];
    [rootMenu release];
}

#ifdef STARTOSX_SHARED_MENU_INLINE_WAS_UNDEFINED
# undef STARTOSX_SHARED_MENU_INLINE
# undef STARTOSX_SHARED_MENU_INLINE_WAS_UNDEFINED
#else
# undef STARTOSX_SHARED_MENU_INLINE
#endif
#undef STARTOSX_SHARED_APPLICATION_MENUS
#endif

#if defined(STARTOSX_SHARED_INIT_CHROME)
#ifndef STARTOSX_IMAGE_SCALING
# define STARTOSX_IMAGE_SCALING NSImageScaleNone
#endif
#ifndef STARTOSX_IMAGE_AUTOREMASK
# define STARTOSX_IMAGE_AUTOREMASK (NSViewMaxXMargin | NSViewHeightSizable)
#endif
#ifndef STARTOSX_BUTTON_BEZEL_STYLE
# define STARTOSX_BUTTON_BEZEL_STYLE NSBezelStyleRounded
#endif
#ifndef STARTOSX_TAB_AUTOREMASK
# define STARTOSX_TAB_AUTOREMASK (NSViewWidthSizable | NSViewHeightSizable)
#endif
    NSUInteger const style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSRect const windowFrame = NSMakeRect(0, 0, 480, 280);
    self = [super initWithContentRect:windowFrame styleMask:style backing:NSBackingStoreBuffered defer:NO];

    if (self)
    {
        // window properties
        [self setDelegate:self];
        [self setReleasedWhenClosed:NO];
#if defined MAC_OS_X_VERSION_10_3 && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_3
        [self setContentMinSize:[[self contentView] frame].size];
#else
        [self setMinSize:[NSWindow frameRectForContentRect:[[self contentView] frame] styleMask:[self styleMask]].size];
#endif


        // image on the left
        NSRect const imageFrame = NSMakeRect(0, 0, 100, 280);
        NSImageView * imageView = [[NSImageView alloc] initWithFrame:imageFrame];
        [imageView setImageScaling:STARTOSX_IMAGE_SCALING];
        [imageView setImage:[NSImage imageNamed:@"game"]];
        [[self contentView] addSubview:imageView];
        [imageView setAutoresizingMask:STARTOSX_IMAGE_AUTOREMASK];


        // buttons
        CGFloat const buttonWidth = 80;
        CGFloat const buttonHeight = 32;

        NSRect const startButtonFrame = NSMakeRect(windowFrame.size.width - buttonWidth, 0, buttonWidth, buttonHeight);
        startButton = [[NSButton alloc] initWithFrame:startButtonFrame];
        [[self contentView] addSubview:startButton];
        [startButton setTitle:@"Start"];
        [startButton setTarget:self];
        [startButton setAction:@selector(start:)];
        [startButton setBezelStyle:STARTOSX_BUTTON_BEZEL_STYLE];
        [startButton setKeyEquivalent:@"\r"];
        [startButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];

        NSRect const cancelButtonFrame = NSMakeRect(startButtonFrame.origin.x - buttonWidth, 0, buttonWidth, buttonHeight);
        cancelButton = [[NSButton alloc] initWithFrame:cancelButtonFrame];
        [[self contentView] addSubview:cancelButton];
        [cancelButton setTitle:@"Cancel"];
        [cancelButton setTarget:self];
        [cancelButton setAction:@selector(cancel:)];
        [cancelButton setBezelStyle:STARTOSX_BUTTON_BEZEL_STYLE];
        [cancelButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];


        // tab frame
        NSRect const tabViewFrame = NSMakeRect(imageFrame.size.width, buttonHeight, windowFrame.size.width - imageFrame.size.width, windowFrame.size.height - buttonHeight - 5);
        tabView = [[NSTabView alloc] initWithFrame:tabViewFrame];
        [[self contentView] addSubview:tabView];
        setFontToSmall(tabView);
        setControlToSmall(tabView);
        [tabView setAutoresizingMask:STARTOSX_TAB_AUTOREMASK];

#undef STARTOSX_TAB_AUTOREMASK
#undef STARTOSX_BUTTON_BEZEL_STYLE
#undef STARTOSX_IMAGE_AUTOREMASK
#undef STARTOSX_IMAGE_SCALING
#undef STARTOSX_SHARED_INIT_CHROME
#endif

#if defined(STARTOSX_SHARED_SETUP_AND_MESSAGE_TABS)
#ifndef STARTOSX_ALWAYS_SHOW_LABEL
# define STARTOSX_ALWAYS_SHOW_LABEL @"Always show this window at startup"
#endif
        // setup tab

        tabViewItemSetup = [[NSTabViewItem alloc] init];
        [tabView addTabViewItem:tabViewItemSetup];
        [tabViewItemSetup setLabel:@"Setup"];
        NSRect const tabViewItemSetupFrame = [[tabViewItemSetup view] frame];


        // always show checkbox
        alwaysShowButton = makeCheckbox(STARTOSX_ALWAYS_SHOW_LABEL);
        [[tabViewItemSetup view] addSubview:alwaysShowButton];
        NSSize const alwaysShowButtonSize = [alwaysShowButton frame].size;
        NSRect const alwaysShowButtonFrame = NSSizeAddXY(alwaysShowButtonSize, tabViewItemSetupFrame.size.width - alwaysShowButtonSize.width, 0);
        [alwaysShowButton setFrame:alwaysShowButtonFrame];
        [alwaysShowButton setAutoresizingMask:NSViewMinXMargin | NSViewMaxYMargin];


        // video mode selectors and labels
        NSTextField * labelVideoMode = makeLabel(@"Video mode:");
        [[tabViewItemSetup view] addSubview:labelVideoMode];
        NSSize const labelVideoModeSize = [labelVideoMode frame].size;
        [labelVideoMode setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];

        fullscreenButton = makeCheckbox(@"Fullscreen");
        [[tabViewItemSetup view] addSubview:fullscreenButton];
        NSSize const fullscreenButtonSize = [fullscreenButton frame].size;
        [fullscreenButton setAction:@selector(fullscreenClicked:)];
        [fullscreenButton setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];

        videoMode3DPUButton = makeComboBox();
        [[tabViewItemSetup view] addSubview:videoMode3DPUButton];
        NSSize const videoMode3DPUButtonSize = [videoMode3DPUButton frame].size;
        CGFloat const videoMode3DButtonX = labelVideoModeSize.width; // NSRightEdge(labelVideoModeFrame);
        NSRect const videoMode3DPUButtonFrame = NSMakeRect(videoMode3DButtonX, tabViewItemSetupFrame.size.height - videoMode3DPUButtonSize.height, tabViewItemSetupFrame.size.width - videoMode3DButtonX - fullscreenButtonSize.width, videoMode3DPUButtonSize.height);
        [videoMode3DPUButton setFrame:videoMode3DPUButtonFrame];
        [videoMode3DPUButton setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];

        NSRect const labelVideoModeFrame = NSSizeAddXY(labelVideoModeSize, 0, videoMode3DPUButtonFrame.origin.y + rintf((videoMode3DPUButtonSize.height - labelVideoModeSize.height) * 0.5f) + 1);
        [labelVideoMode setFrame:labelVideoModeFrame];

        NSRect const fullscreenButtonFrame = NSSizeAddXY(fullscreenButtonSize, tabViewItemSetupFrame.size.width - fullscreenButtonSize.width, videoMode3DPUButtonFrame.origin.y + rintf((videoMode3DPUButtonSize.height - fullscreenButtonSize.height) * 0.5f) + 1);
        [fullscreenButton setFrame:fullscreenButtonFrame];


        // game selector and label
        NSTextField * labelGame = makeLabel(@"Game:");
        [[tabViewItemSetup view] addSubview:labelGame];
        NSSize const labelGameSize = [labelGame frame].size;
        NSRect const labelGameFrame = NSSizeAddXY(labelGameSize, 0, videoMode3DPUButtonFrame.origin.y - labelGameSize.height);
        [labelGame setFrame:labelGameFrame];
        [labelGame setAutoresizingMask:NSViewMaxXMargin | NSViewMinYMargin];

        CGFloat const gameListVerticalPadding = 3;
        CGFloat const gameListY = NSTopEdge(alwaysShowButtonFrame) + gameListVerticalPadding;
        NSRect const gameListFrame = NSMakeRect(0, gameListY, tabViewItemSetupFrame.size.width, labelGameFrame.origin.y - gameListY - gameListVerticalPadding);
        gameList = [[NSScrollView alloc] initWithFrame:gameListFrame];
        [[tabViewItemSetup view] addSubview:gameList];
        [gameList setBorderType:NSBezelBorder];
        [gameList setHasVerticalScroller:YES];
        [gameList setHasHorizontalScroller:NO];
        setControlToSmall([[gameList verticalScroller] cell]);
        NSSize const gameListContentSize = [gameList contentSize];
        [gameList setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        NSTableView * gameListTable = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, gameListContentSize.width, gameListContentSize.height)];
        [gameList setDocumentView:gameListTable];

        NSTableColumn * nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"0"];
        [gameListTable addTableColumn:nameColumn];
        NSTableColumn * fileColumn = [[NSTableColumn alloc] initWithIdentifier:@"1"];
        [gameListTable addTableColumn:fileColumn];
        [nameColumn setEditable:NO];
        [[nameColumn headerCell] setStringValue:@"Name"];
        [nameColumn setWidth:gameListContentSize.width * (2.f/3.f)];
        [fileColumn setEditable:NO];
        [[fileColumn headerCell] setStringValue:@"File"];
        [gameListTable sizeLastColumnToFit];
        [gameListTable setAutoresizingMask:NSViewWidthSizable];


        // message log tab

        tabViewItemMessageLog = [[NSTabViewItem alloc] init];
        [tabView addTabViewItem:tabViewItemMessageLog];
        [tabViewItemMessageLog setLabel:@"Message Log"];
        NSRect const tabViewItemMessageLogFrame = [[tabViewItemMessageLog view] frame];


        // message log
        NSScrollView * messagesScrollView = [[NSScrollView alloc] initWithFrame:NSRectChangeXY(tabViewItemMessageLogFrame, 0, 0)];
        [[tabViewItemMessageLog view] addSubview:messagesScrollView];
        [messagesScrollView setBorderType:NSBezelBorder];
        [messagesScrollView setHasVerticalScroller:YES];
        [messagesScrollView setHasHorizontalScroller:NO];
        setControlToSmall([[messagesScrollView verticalScroller] cell]);
        NSSize const messagesScrollViewContentSize = [messagesScrollView contentSize];
        [messagesScrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        messagesView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, messagesScrollViewContentSize.width, messagesScrollViewContentSize.height)];
        [messagesScrollView setDocumentView:messagesView];
        [messagesView setEditable:NO];
        [messagesView setRichText:NO];
        setFontToSmall(messagesView);
        [messagesView setMinSize:NSMakeSize(0.0, messagesScrollViewContentSize.height)];
        [messagesView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
        [messagesView setVerticallyResizable:YES];
        [messagesView setHorizontallyResizable:NO];
        [messagesView setAutoresizingMask:NSViewWidthSizable];

        [[messagesView textContainer] setContainerSize:NSMakeSize(messagesScrollViewContentSize.width, FLT_MAX)];
        [[messagesView textContainer] setWidthTracksTextView:YES];

#undef STARTOSX_ALWAYS_SHOW_LABEL
#undef STARTOSX_SHARED_SETUP_AND_MESSAGE_TABS
#endif

#if defined(STARTOSX_SHARED_ENABLE_SETUP_CONTROLS)
    // enable all the controls on the Configuration page
    NSEnumerator *enumerator = [[[tabViewItemSetup view] subviews] objectEnumerator];
    NSControl *control;
    while ((control = [enumerator nextObject]))
    {
        if ([control respondsToSelector:@selector(setEnabled:)])
            [control setEnabled:true];
    }

#undef STARTOSX_SHARED_ENABLE_SETUP_CONTROLS
#endif

#if defined(STARTOSX_SHARED_SELECT_GAME_LIST_ROW)
        if (row >= 0)
        {
            [[gameList documentView] scrollRowToVisible:row];
#if defined MAC_OS_X_VERSION_10_3 && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_3
            [[gameList documentView] selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
#else
            [[gameList documentView] selectRow:row byExtendingSelection:NO];
#endif
        }

#undef STARTOSX_SHARED_SELECT_GAME_LIST_ROW
#endif

#if defined(STARTOSX_SHARED_FINISH_RUN_MODE)
    [cancelButton setEnabled:true];
    [startButton setEnabled:true];

    [tabView selectTabViewItem:tabViewItemSetup];
    [NSCursor unhide]; // Why should I need to do this?

#undef STARTOSX_SHARED_FINISH_RUN_MODE
#endif

#if defined(STARTOSX_SHARED_SETUP_MESSAGES_MODE)
- (void)setupMessagesMode
{
    [tabView selectTabViewItem:tabViewItemMessageLog];

    // disable all the controls on the Configuration page except "always show", so the
    // user can enable it if they want to while waiting for something else to happen
    NSEnumerator *enumerator = [[[tabViewItemSetup view] subviews] objectEnumerator];
    NSControl *control;
    while ((control = [enumerator nextObject]))
    {
        if (control != alwaysShowButton && [control respondsToSelector:@selector(setEnabled:)])
            [control setEnabled:false];
    }

    [cancelButton setEnabled:false];
    [startButton setEnabled:false];
}

#undef STARTOSX_SHARED_SETUP_MESSAGES_MODE
#endif

#if defined(STARTOSX_SHARED_STARTWIN_COMMON)
bool startwin_isopen(void)
{
    return startwin != nil;
}

int startwin_open(void)
{
#ifdef STARTOSX_SHARED_STARTWIN_OPEN_FINISH_LAUNCHING
    // fix for "ld: absolute address to symbol _NSApp in a different linkage unit not supported"
    // (OS X 10.6) when building for PPC
    nsapp = [NSApplication sharedApplication];
#endif

    if (startwin != nil) return 1;

    startwin = [[StartupWindow alloc] init];
    if (startwin == nil) return -1;

    [startwin setupMessagesMode];

#ifdef STARTOSX_SHARED_STARTWIN_OPEN_FINISH_LAUNCHING
    [nsapp finishLaunching];
#endif

    [startwin center];
    [startwin makeKeyAndOrderFront:nil];

    CreateApplicationMenus();

    return 0;
}

int startwin_close(void)
{
    if (startwin == nil) return 1;

    [startwin close];
    [startwin release];
    startwin = nil;

    return 0;
}

int startwin_puts(const char *s)
{
    NSString *ns;

    if (!s) return -1;
    if (startwin == nil) return 1;

    ns = [NSString stringWithUTF8String:s];
    [startwin putsMessage:ns];
    [ns release];

    return 0;
}

int startwin_settitle(const char *s)
{
    NSString *ns;

    if (!s) return -1;
    if (startwin == nil) return 1;

    ns = [NSString stringWithUTF8String:s];
    [startwin setTitle:ns];
    [ns release];

    return 0;
}

int startwin_idle(void *v)
{
    UNREFERENCED_PARAMETER(v);

    if (startwin)
    {
        NSEvent *event;
        do
        {
            event = [nsapp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate date] inMode:NSDefaultRunLoopMode dequeue:YES];
            [nsapp sendEvent:event];
        }
        while (event != nil);

        [startwin displayIfNeeded];
        [nsapp updateWindows];
    }

    return 0;
}

#undef STARTOSX_SHARED_STARTWIN_OPEN_FINISH_LAUNCHING
#undef STARTOSX_SHARED_STARTWIN_COMMON
#endif
