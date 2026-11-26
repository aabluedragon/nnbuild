
#import <Cocoa/Cocoa.h>

#include "duke3d.h"
#include "game.h"
#include "common.h"
#include "common_game.h"
#include "build.h"
#include "compat.h"
#include "baselayer.h"
#include "grpscan.h"
#include "osxbits.h"
#include "sdl_inc.h"
#include "SDL_syswm.h"
#include "vbjs.h"

#import "GrpFile.game.h"
#import "GameListSource.game.h"

// SDL_RaiseWindow only orders a Cocoa window forward.  A separate DevTools
// window also needs to become the key window, otherwise the game's SDL
// relative-mouse view remains the target of the activation click.
extern "C" void VBJS_CocoaMakeKeyWindow(SDL_Window *window)
{
    if (!window) return;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        return;

    NSWindow *nativeWindow = info.info.cocoa.window;
    [nativeWindow setAcceptsMouseMovedEvents:YES];
    [NSApp activateIgnoringOtherApps:YES];
    [nativeWindow makeKeyAndOrderFront:nil];
}

extern "C" void VBJS_CocoaPlaceGameLeft(SDL_Window *window)
{
    if (!window) return;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) return;
    NSWindow *nativeWindow = info.info.cocoa.window;
    // DevTools opens on the leftmost available display.  macOS's
    // mainScreen can still be the game display in a multi-display layout, so
    // select the display with the smallest global x coordinate explicitly.
    NSScreen *screen = [NSScreen mainScreen];
    for (NSScreen *candidate in [NSScreen screens])
        if ([candidate visibleFrame].origin.x < [screen visibleFrame].origin.x)
            screen = candidate;
    NSRect const usable = [screen visibleFrame];
    NSRect const frame = [nativeWindow frame];
    CGFloat const y = usable.origin.y + ((usable.size.height > frame.size.height) ? (usable.size.height - frame.size.height) / 2 : 0);
    [nativeWindow setFrameOrigin:NSMakePoint(usable.origin.x + 20, y)];
}

// SDL relative mouse mode is process-wide on macOS.  A native AppKit window
// avoids the second-SDL-window activation/capture race entirely.
@interface VBJSCocoaDevtools : NSObject
{
    NSWindow *window;
    NSTextView *console;
    NSTextField *expression;
    NSString *lastConsole;
}
- (void)show;
- (void)refresh;
- (void)run:(id)sender;
- (BOOL)handleSDLEvent:(SDL_Event const *)event;
@end

@implementation VBJSCocoaDevtools
- (id)init
{
    self = [super init];
    if (!self) return nil;
    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 516, 666)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"NNBuild JavaScript DevTools"];
    [window setMinSize:NSMakeSize(360, 420)];
    [window setReleasedWhenClosed:NO];
    NSView *content = [window contentView];
    NSTextField *title = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 630, 492, 22)];
    [title setStringValue:@"NNBuild JavaScript DevTools"];
    [title setEditable:NO]; [title setSelectable:NO]; [title setBezeled:NO]; [title setDrawsBackground:NO];
    [title setFont:[NSFont boldSystemFontOfSize:14]]; [content addSubview:title]; [title release];
    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(12, 116, 492, 502)];
    [scroll setHasVerticalScroller:YES]; [scroll setBorderType:NSBezelBorder];
    console = [[NSTextView alloc] initWithFrame:[[scroll contentView] bounds]];
    [console setEditable:NO]; [console setSelectable:YES]; [console setFont:[NSFont userFixedPitchFontOfSize:12]];
    [scroll setDocumentView:console]; [content addSubview:scroll]; [scroll release];
    expression = [[NSTextField alloc] initWithFrame:NSMakeRect(12, 78, 390, 26)];
    [expression setStringValue:@"$event"]; [expression setTarget:self]; [expression setAction:@selector(run:)]; [content addSubview:expression];
    NSButton *run = [[NSButton alloc] initWithFrame:NSMakeRect(410, 78, 94, 26)];
    [run setTitle:@"Evaluate"]; [run setTarget:self]; [run setAction:@selector(run:)]; [content addSubview:run]; [run release];
    return self;
}
- (void)dealloc { [lastConsole release]; [expression release]; [console release]; [window release]; [super dealloc]; }
- (void)show
{
    // Keep the game on the left and DevTools on the right when the display
    // has room, so both windows remain visible and independently clickable.
    SDL_Window *gameSDLWindow = SDL_GL_GetCurrentWindow();
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (gameSDLWindow && SDL_GetWindowWMInfo(gameSDLWindow, &info) && info.subsystem == SDL_SYSWM_COCOA)
    {
        NSWindow *gameWindow = info.info.cocoa.window;
        NSScreen *screen = [gameWindow screen] ?: [NSScreen mainScreen];
        NSRect usable = [screen visibleFrame];
        NSRect gameFrame = [gameWindow frame];
        NSRect devFrame = [window frame];
        CGFloat const gap = 12;
        if (gameFrame.size.width + gap + devFrame.size.width <= usable.size.width)
        {
            CGFloat const totalWidth = gameFrame.size.width + gap + devFrame.size.width;
            CGFloat const x = usable.origin.x + (usable.size.width - totalWidth) / 2;
            CGFloat const gameY = usable.origin.y + ((usable.size.height - gameFrame.size.height) / 2 > 0 ? (usable.size.height - gameFrame.size.height) / 2 : 0);
            [gameWindow setFrameOrigin:NSMakePoint(x, gameY)];
            [window setFrameOrigin:NSMakePoint(x + gameFrame.size.width + gap,
                                               usable.origin.y + ((usable.size.height - devFrame.size.height) / 2 > 0 ? (usable.size.height - devFrame.size.height) / 2 : 0))];
        }
    }
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
}
- (void)refresh
{
    NSMutableString *text = [NSMutableString string];
    for (int i = 0; i < 12; ++i) { char const *line = VBJS_DevtoolsLog(i); if (line) [text appendFormat:@"%s\n", line]; }
    if (![text isEqualToString:lastConsole]) { [console setString:text]; [lastConsole release]; lastConsole = [text copy]; }
}
- (void)run:(id)sender
{
    UNREFERENCED_PARAMETER(sender); char result[1024];
    VBJS_DevtoolsEvaluate([[expression stringValue] UTF8String], result, sizeof(result));
    [console setString:[NSString stringWithFormat:@"> %@\n%s", [expression stringValue], result]];
}
- (BOOL)handleSDLEvent:(SDL_Event const *)event
{
    if (!event || ![window isVisible]) return NO;
    if (event->type == SDL_MOUSEBUTTONDOWN)
    {
        // SDL's Cocoa pump owns the NSApplication event loop.  Query the
        // actual desktop cursor instead of SDL's (game-relative) coordinates.
        NSPoint const inWindow = [window convertPointFromScreen:[NSEvent mouseLocation]];
        NSPoint const inContent = [[window contentView] convertPoint:inWindow fromView:nil];
        NSView *hit = [[window contentView] hitTest:inContent];
        if (!hit) return NO;
        [NSApp activateIgnoringOtherApps:YES];
        [window makeKeyAndOrderFront:nil];
        if (hit == expression || [hit isDescendantOf:expression])
        {
            [window makeFirstResponder:expression];
            return YES;
        }
        for (NSView *view = hit; view && view != [window contentView]; view = [view superview])
            if ([view isKindOfClass:[NSButton class]]) { [(NSButton *)view performClick:nil]; return YES; }
        return YES; // the console is selectable, but must never control Duke.
    }

    id responder = [window firstResponder];
    BOOL const editingExpression = responder == expression || [responder isKindOfClass:[NSTextView class]];
    if (!editingExpression) return NO;
    if (event->type == SDL_TEXTINPUT)
    {
        NSString *typed = [NSString stringWithUTF8String:event->text.text];
        if (typed) [expression setStringValue:[[expression stringValue] stringByAppendingString:typed]];
        return YES;
    }
    if (event->type == SDL_KEYDOWN)
    {
        if (event->key.keysym.scancode == SDL_SCANCODE_RETURN || event->key.keysym.scancode == SDL_SCANCODE_KP_ENTER)
        {
            [self run:nil]; return YES;
        }
        if (event->key.keysym.scancode == SDL_SCANCODE_BACKSPACE)
        {
            NSString *value = [expression stringValue];
            if ([value length]) [expression setStringValue:[value substringToIndex:[value length] - 1]];
            return YES;
        }
    }
    return NO;
}
@end

static VBJSCocoaDevtools *g_vbjsDevtools = nil;
extern "C" void VBJS_CocoaDevtoolsShow(void) { if (!g_vbjsDevtools) { g_vbjsDevtools = [[VBJSCocoaDevtools alloc] init]; [g_vbjsDevtools show]; } }
extern "C" void VBJS_CocoaDevtoolsRefresh(void) { [g_vbjsDevtools refresh]; }
extern "C" int VBJS_CocoaDevtoolsProcessEvent(void const *event) { return [g_vbjsDevtools handleSDLEvent:(SDL_Event const *)event] ? 1 : 0; }
extern "C" void VBJS_CocoaDevtoolsShutdown(void) { [g_vbjsDevtools release]; g_vbjsDevtools = nil; }

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

static inline NSRect NSRectChangeXY(NSRect const rect, CGFloat const x, CGFloat const y)
{
    return NSMakeRect(x, y, rect.size.width, rect.size.height);
}
static inline NSRect NSSizeAddXY(NSSize const size, CGFloat const x, CGFloat const y)
{
    return NSMakeRect(x, y, size.width, size.height);
}
static inline CGFloat NSRightEdge(NSRect rect)
{
    return rect.origin.x + rect.size.width;
}
static inline CGFloat NSTopEdge(NSRect rect)
{
    return rect.origin.y + rect.size.height;
}

static inline void setFontToSmall(id control)
{
    [control setFont:[NSFont fontWithDescriptor:[[control font] fontDescriptor] size:[NSFont smallSystemFontSize]]];
}

static inline void setControlToSmall(id control)
{
    [control setControlSize:NSControlSizeSmall];
}

static inline NSTextField * makeLabel(NSString * labelText)
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

static inline NSButton * makeCheckbox(NSString * labelText)
{
    NSButton *checkbox = [[NSButton alloc] init];
    setFontToSmall(checkbox);
    setControlToSmall([checkbox cell]);
    [checkbox setTitle:labelText];
    [checkbox setButtonType:NSButtonTypeSwitch];
    [checkbox sizeToFit];
    return checkbox;
}

static inline NSPopUpButton * makeComboBox(void)
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

/* setAppleMenu disappeared from the headers in 10.4 */
@interface NSApplication(NSAppleMenu)
- (void)setAppleMenu:(NSMenu *)menu;
@end

#define STARTOSX_SHARED_APPLICATION_MENUS
#define STARTOSX_SHARED_MENU_INLINE inline
#include "../../build/include/startosx_shared.mm.inc"

static int retval = -1;

static struct {
    grpfile_t const * grp;
    struct {
        int32_t fullscreen;
        int32_t xdim, ydim, bpp;
        int32_t forcesetup;
    } shared;
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


#define STARTOSX_ALWAYS_SHOW_LABEL @"Show startup menu by default"
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
    quitevent = 1;  // closing the window aborts, including while retrying a connection

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
        xdim = settings.shared.xdim;
        ydim = settings.shared.ydim;
        bpp  = settings.shared.bpp;
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
    // Also signal a quit so Cancel works after the setup dialog has returned -
    // e.g. while the game is idling on this window retrying a connection to a
    // server that isn't up yet. The main loop watches quitevent and shuts down.
    quitevent = 1;
}

- (void)start:(id)sender
{
    UNREFERENCED_PARAMETER(sender);

    int mode = [[modeslist3d objectAtIndex:[videoMode3DPUButton indexOfSelectedItem]] intValue];
    if (mode >= 0) {
        settings.shared.xdim = validmode[mode].xdim;
        settings.shared.ydim = validmode[mode].ydim;
        settings.shared.bpp = validmode[mode].bpp;
        settings.shared.fullscreen = validmode[mode].fs;
    }

    int row = [[gameList documentView] selectedRow];
    if (row >= 0) {
        settings.grp = [[gamelistsrc grpAtIndex:row] entryptr];
    }

    settings.shared.forcesetup = [alwaysShowButton state] == NSControlStateValueOn;

    retval = 1;
}

- (void)setupRunMode
{
    videoGetModes();

    [fullscreenButton setState: (settings.shared.fullscreen ? NSControlStateValueOn : NSControlStateValueOff)];
    [alwaysShowButton setState: (settings.shared.forcesetup ? NSControlStateValueOn : NSControlStateValueOff)];
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
#include "../../build/include/startosx_shared.mm.inc"

int startwin_run(void)
{
    if (startwin == nil) return 0;

    settings.shared.fullscreen = ud.config.ScreenMode;
    settings.shared.xdim = ud.config.ScreenWidth;
    settings.shared.ydim = ud.config.ScreenHeight;
    settings.shared.bpp = ud.config.ScreenBPP;
    settings.shared.forcesetup = ud.config.ForceSetup;
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
        ud.config.ScreenMode = settings.shared.fullscreen;
        ud.config.ScreenWidth = settings.shared.xdim;
        ud.config.ScreenHeight = settings.shared.ydim;
        ud.config.ScreenBPP = settings.shared.bpp;
        ud.config.ForceSetup = settings.shared.forcesetup;
        g_selectedGrp = settings.grp;
    }

    return retval;
}
