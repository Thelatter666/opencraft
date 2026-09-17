// T-A2 evidence helper: locate the OpenCraft window by PID and bring it to the
// front. No input injection is needed for this task (nothing has to be aimed or
// moved), so this is minutes of code instead of the HID machinery other cards
// used - but the two lessons that cost time before are kept:
//   * the window is looked up by PID, never by owner name (a dead instance's
//     window matches the name and yields the wrong id);
//   * the caller still filters by size, because the same pid owns a menu-bar
//     window as well.
//
// usage: ta2win win <pid>       -> "<windowid> <x> <y> <w> <h>" per window
//        ta2win activate <pid>  -> bring the app forward and raise its windows
// Adapted from docs/qa/t008-2026-09-13/tools/findwin.m.

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include <stdio.h>
#include <stdlib.h>

static CFArrayRef window_infos(void) {
    return CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
}

static int list_windows(pid_t pid) {
    CFArrayRef list = window_infos();
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        CFDictionaryRef info = CFArrayGetValueAtIndex(list, i);
        NSNumber *owner = (__bridge NSNumber *)CFDictionaryGetValue(info, kCGWindowOwnerPID);
        if (owner == nil || owner.intValue != (int)pid) {
            continue;
        }
        NSNumber *wid = (__bridge NSNumber *)CFDictionaryGetValue(info, kCGWindowNumber);
        NSDictionary *bounds = (__bridge NSDictionary *)CFDictionaryGetValue(info, kCGWindowBounds);
        if (wid == nil || bounds == nil) {
            continue;
        }
        NSNumber *x = bounds[@"X"];
        NSNumber *y = bounds[@"Y"];
        NSNumber *w = bounds[@"Width"];
        NSNumber *h = bounds[@"Height"];
        if (x == nil || y == nil || w == nil || h == nil) {
            continue;
        }
        printf("%u %g %g %g %g\n", wid.unsignedIntValue, x.doubleValue, y.doubleValue, w.doubleValue, h.doubleValue);
    }
    CFRelease(list);
    return 0;
}

static int activate(pid_t pid) {
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (app == nil) {
        return 1;
    }
    // Options 0: the macOS 14+ supported spelling (the old "ignore other apps"
    // flag is deprecated and a no-op). The AX raise below is what actually
    // lifts the window in front of the terminal.
    [app activateWithOptions:0];
    usleep(200000);

    AXUIElementRef ax_app = AXUIElementCreateApplication(pid);
    if (ax_app != NULL) {
        CFTypeRef windows = NULL;
        if (AXUIElementCopyAttributeValue(ax_app, kAXWindowsAttribute, &windows) == kAXErrorSuccess && windows != NULL) {
            CFArrayRef array = (CFArrayRef)windows;
            for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
                AXUIElementPerformAction((AXUIElementRef)CFArrayGetValueAtIndex(array, i), kAXRaiseAction);
            }
            CFRelease(windows);
        }
        CFRelease(ax_app);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s win|activate <pid>\n", argv[0]);
        return 2;
    }
    const pid_t pid = (pid_t)atoi(argv[2]);
    if (strcmp(argv[1], "win") == 0) {
        return list_windows(pid);
    }
    if (strcmp(argv[1], "activate") == 0) {
        return activate(pid);
    }
    fprintf(stderr, "unknown subcommand: %s\n", argv[1]);
    return 2;
}
