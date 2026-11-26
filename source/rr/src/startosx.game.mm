
#import <Cocoa/Cocoa.h>

#include "duke3d.h"
#include "game.h"
#include "common.h"
#include "common_game.h"
#include "build.h"
#include "compat.h"
#include "baselayer.h"
#include "grpscan.h"

#import "GrpFile.game.h"
#import "GameListSource.game.h"

#ifndef MAC_OS_X_VERSION_10_5
# define NSImageScaleNone NSScaleNone
#endif

#ifndef MAC_OS_X_VERSION_10_12
# define NSEventModifierFlagOption NSAlternateKeyMask
# define NSEventModifierFlagCommand NSCommandKeyMask
# define NSEventMaskAny NSAnyEventMask
# define NSWindowStyleMaskTitled NSTitledWindowMask
# define NSWindowStyleMaskClosable NSClosableWindowMask
# define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
# define NSWindowStyleMaskResizable NSResizableWindowMask
# define NSAlertStyleInformational NSInformationalAlertStyle
# define NSControlSizeSmall NSSmallControlSize
#endif

#ifndef MAC_OS_X_VERSION_10_14
# define NSButtonTypeSwitch NSSwitchButton
# define NSBezelStyleRounded NSRoundedBezelStyle
# define NSControlStateValueOn NSOnState
# define NSControlStateValueOff NSOffState
#endif

static NSRect NSRectChangeXY(NSRect const rect, CGFloat const x, CGFloat const y)
{
    return NSMakeRect(x, y, rect.size.width, rect.size.height);
}
static NSRect NSSizeAddXY(NSSize const size, CGFloat const x, CGFloat const y)
{
    return NSMakeRect(x, y, size.width, size.height);
}
#if 0
static CGFloat NSRightEdge(NSRect rect)
{
    return rect.origin.x + rect.size.width;
}
#endif
static CGFloat NSTopEdge(NSRect rect)
{
    return rect.origin.y + rect.size.height;
}

static void setFontToSmall(id control)
{
    [control setFont:[NSFont fontWithDescriptor:[[control font] fontDescriptor] size:[NSFont smallSystemFontSize]]];
}

static void setControlToSmall(id control)
{
    [control setControlSize:NSControlSizeSmall];
}

static NSTextField * makeLabel(NSString * labelText)
{
    NSTextField *textField = [[NSTextField alloc] init];
    setFontToSmall(textField);
    setControlToSmall([textField cell]);
    [textField setStringValue:labelText];
    [textField setBezeled:NO];
    [textField setDrawsBackground:NO];
    [textField setEditable:NO];
    [textField setSelectable:NO];
    [textField sizeToFit];
    return textField;
}

static NSButton * makeCheckbox(NSString * labelText)
{
    NSButton *checkbox = [[NSButton alloc] init];
    setFontToSmall(checkbox);
    setControlToSmall([checkbox cell]);
    [checkbox setTitle:labelText];
    [checkbox setButtonType:NSButtonTypeSwitch];
    [checkbox sizeToFit];
    return checkbox;
}

static NSPopUpButton * makeComboBox(void)
{
    NSPopUpButton *comboBox = [[NSPopUpButton alloc] init];
    [comboBox setPullsDown:NO];
    setFontToSmall(comboBox);
    setControlToSmall([comboBox cell]);
    [comboBox setBezelStyle:NSBezelStyleRounded];
    [comboBox setPreferredEdge:NSMaxYEdge];
    [[comboBox cell] setArrowPosition:NSPopUpArrowAtCenter];
    [comboBox sizeToFit];
    return comboBox;
}

static id nsapp;

/* setAppleMenu disappeared from the headers in 10.4 */
@interface NSApplication(NSAppleMenu)
- (void)setAppleMenu:(NSMenu *)menu;
@end

#define STARTOSX_SHARED_APPLICATION_MENUS
#include "../../build/include/startosx_shared.mm.inc"

static int retval = -1;

static struct {
    grpfile_t const * grp;
    int fullscreen;
    int xdim3d, ydim3d, bpp3d;
    int forcesetup;
} settings;

@interface StartupWindow : NSWindow <NSWindowDelegate>
{
    NSMutableArray *modeslist3d;
    GameListSource *gamelistsrc;

    NSButton *alwaysShowButton;
    NSButton *fullscreenButton;
    NSTextView *messagesView;
    NSTabView *tabView;
    NSTabViewItem *tabViewItemSetup;
    NSTabViewItem *tabViewItemMessageLog;
    NSPopUpButton *videoMode3DPUButton;
    NSScrollView *gameList;

    NSButton *cancelButton;
    NSButton *startButton;
}

- (StartupWindow *)init;

- (void)dealloc;
- (void)populateVideoModes:(BOOL)firstTime;

- (void)fullscreenClicked:(id)sender;

- (void)cancel:(id)sender;
- (void)start:(id)sender;

- (void)setupRunMode;
- (void)setupMessagesMode;

- (void)putsMessage:(NSString *)str;

@end

@implementation StartupWindow : NSWindow

- (StartupWindow *)init
{
#define STARTOSX_SHARED_INIT_CHROME
#include "../../build/include/startosx_shared.mm.inc"


#define STARTOSX_SHARED_SETUP_AND_MESSAGE_TABS
#include "../../build/include/startosx_shared.mm.inc"
    }

    return self;
}

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)canBecomeMainWindow
{
    return YES;
}

- (BOOL) windowShouldClose:(id)sender
{
    UNREFERENCED_PARAMETER(sender);

    retval = 0;

    return YES;
}

- (void)dealloc
{
    [gamelistsrc release];
    [modeslist3d release];
    [super dealloc];
}

- (void)populateVideoModes:(BOOL)firstTime
{
    int i, mode3d, fullscreen = ([fullscreenButton state] == NSControlStateValueOn);
    int idx3d = -1;
    int xdim = 0, ydim = 0, bpp = 0;

    if (firstTime) {
        xdim = settings.xdim3d;
        ydim = settings.ydim3d;
        bpp  = settings.bpp3d;
    } else {
        mode3d = [[modeslist3d objectAtIndex:[videoMode3DPUButton indexOfSelectedItem]] intValue];
        if (mode3d >= 0) {
            xdim = validmode[mode3d].xdim;
            ydim = validmode[mode3d].ydim;
            bpp = validmode[mode3d].bpp;
        }

    }
    mode3d = videoCheckMode(&xdim, &ydim, bpp, fullscreen, 1);
    if (mode3d < 0) {
        int i, cd[] = { 32, 24, 16, 15, 8, 0 };
        for (i=0; cd[i]; ) { if (cd[i] >= bpp) i++; else break; }
        for ( ; cd[i]; i++) {
            mode3d = videoCheckMode(&xdim, &ydim, cd[i], fullscreen, 1);
            if (mode3d < 0) continue;
            break;
        }
    }

    [modeslist3d release];
    [videoMode3DPUButton removeAllItems];

    modeslist3d = [[NSMutableArray alloc] init];

    for (i = 0; i < validmodecnt; i++) {
        if (fullscreen == validmode[i].fs) {
            if (i == mode3d) idx3d = [modeslist3d count];
            [modeslist3d addObject:[NSNumber numberWithInt:i]];
            [videoMode3DPUButton addItemWithTitle:[NSString stringWithFormat:@"%d %C %d %d-bpp",
                                                   validmode[i].xdim, 0xd7, validmode[i].ydim, validmode[i].bpp]];
        }
    }

    if (idx3d >= 0) [videoMode3DPUButton selectItemAtIndex:idx3d];
}

- (void)fullscreenClicked:(id)sender
{
    UNREFERENCED_PARAMETER(sender);

    [self populateVideoModes:NO];
}

- (void)cancel:(id)sender
{
    UNREFERENCED_PARAMETER(sender);

    retval = 0;
}

- (void)start:(id)sender
{
    UNREFERENCED_PARAMETER(sender);

    int mode = [[modeslist3d objectAtIndex:[videoMode3DPUButton indexOfSelectedItem]] intValue];
    if (mode >= 0) {
        settings.xdim3d = validmode[mode].xdim;
        settings.ydim3d = validmode[mode].ydim;
        settings.bpp3d = validmode[mode].bpp;
        settings.fullscreen = validmode[mode].fs;
    }

    int row = [[gameList documentView] selectedRow];
    if (row >= 0) {
        settings.grp = [[gamelistsrc grpAtIndex:row] entryptr];
    }

    settings.forcesetup = [alwaysShowButton state] == NSControlStateValueOn;

    retval = 1;
}

- (void)setupRunMode
{
    videoGetModes();

    [fullscreenButton setState: (settings.fullscreen ? NSControlStateValueOn : NSControlStateValueOff)];
    [alwaysShowButton setState: (settings.forcesetup ? NSControlStateValueOn : NSControlStateValueOff)];
    [self populateVideoModes:YES];

#define STARTOSX_SHARED_ENABLE_SETUP_CONTROLS
#include "../../build/include/startosx_shared.mm.inc"
    gamelistsrc = [[GameListSource alloc] init];
    [[gameList documentView] setDataSource:gamelistsrc];
    [[gameList documentView] deselectAll:nil];

    if (settings.grp)
    {
        int row = [gamelistsrc findIndexForGrpname:[NSString stringWithUTF8String:settings.grp->filename]];
#define STARTOSX_SHARED_SELECT_GAME_LIST_ROW
#include "../../build/include/startosx_shared.mm.inc"
    }

#define STARTOSX_SHARED_FINISH_RUN_MODE
#include "../../build/include/startosx_shared.mm.inc"
}

#define STARTOSX_SHARED_SETUP_MESSAGES_MODE
#include "../../build/include/startosx_shared.mm.inc"

- (void)putsMessage:(NSString *)str
{
    NSRange end;
    NSTextStorage *text = [messagesView textStorage];
    BOOL shouldAutoScroll;

    shouldAutoScroll = ((int)NSMaxY([messagesView bounds]) == (int)NSMaxY([messagesView visibleRect]));

    end.location = [text length];
    end.length = 0;

    [text beginEditing];
    [messagesView replaceCharactersInRange:end withString:str];
    [text endEditing];

    if (shouldAutoScroll) {
        end.location = [text length];
        end.length = 0;
        [messagesView scrollRangeToVisible:end];
    }
}

@end

static StartupWindow *startwin = nil;

#define STARTOSX_SHARED_STARTWIN_COMMON
#define STARTOSX_SHARED_STARTWIN_OPEN_FINISH_LAUNCHING
#include "../../build/include/startosx_shared.mm.inc"

int startwin_run(void)
{
    if (startwin == nil) return 0;

    settings.fullscreen = ud.setup.fullscreen;
    settings.xdim3d = ud.setup.xdim;
    settings.ydim3d = ud.setup.ydim;
    settings.bpp3d = ud.setup.bpp;
    settings.forcesetup = ud.setup.forcesetup;
    settings.grp = g_selectedGrp;

    [startwin setupRunMode];

    do
    {
        NSEvent *event = [nsapp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantFuture] inMode:NSDefaultRunLoopMode dequeue:YES];
        [nsapp sendEvent:event];
        [nsapp updateWindows];
    }
    while (retval == -1);

    [startwin setupMessagesMode];
    [nsapp updateWindows];

    if (retval) {
        ud.setup.fullscreen = settings.fullscreen;
        ud.setup.xdim = settings.xdim3d;
        ud.setup.ydim = settings.ydim3d;
        ud.setup.bpp = settings.bpp3d;
        ud.setup.forcesetup = settings.forcesetup;
        g_selectedGrp = settings.grp;
    }

    return retval;
}
