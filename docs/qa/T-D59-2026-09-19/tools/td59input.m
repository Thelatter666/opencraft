// T-D46 on-machine evidence tool (macOS) - copied verbatim from docs/qa/T-D45-2026-09-18/tools/td45input.m. HID-layer input injection, per
// docs/05 §3.1: CGEventCreate{Keyboard,Mouse}Event + CGEventPost(kCGHIDEventTap).
// No osascript anywhere, and the window id is looked up from the PID (name
// matching hits dead windows, docs/05 §3.1 rule 3).
//
// Build:
//   clang -framework Foundation -framework CoreGraphics -o /tmp/ti2input ti2input.m
// Usage:
//   ti2input activate <pid>
//   ti2input win <pid>                    -> "<window_id> <x> <y> <w> <h>"
//   ti2input key <code> down|up
//   ti2input keytap <code> <ms>           -> held long enough for the 20 TPS poll
//   ti2input click <0|1> down|up
//   ti2input clicktap <0|1> <ms>
//   ti2input move <dx> <dy>               -> pointer-locked deltas (kCGMouseEventDeltaX/Y)
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void post(CGEventRef ev) {
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

static void key_event(int code, bool down) {
    post(CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, down));
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "bad args\n");
        return 2;
    }
    NSString *cmd = [NSString stringWithUTF8String:argv[1]];

    if ([cmd isEqualToString:@"activate"]) {
        pid_t pid = (pid_t)atoi(argv[2]);
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (app == nil) {
            fprintf(stderr, "no running application for pid %d\n", pid);
            return 3;
        }
        // NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps
        [app activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
        printf("activated pid=%d name=%s\n", pid, [[app localizedName] UTF8String]);
        return 0;
    }

    if ([cmd isEqualToString:@"win"]) {
        pid_t pid = (pid_t)atoi(argv[2]);
        CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements,
                                                     kCGNullWindowID);
        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
            CFDictionaryRef info = CFArrayGetValueAtIndex(list, i);
            NSNumber *owner = CFDictionaryGetValue(info, kCGWindowOwnerPID);
            NSDictionary *b = (__bridge NSDictionary *)CFDictionaryGetValue(info, kCGWindowBounds);
            if (owner.intValue != pid || b == nil) {
                continue;
            }
            NSNumber *w = b[@"Width"], *h = b[@"Height"], *x = b[@"X"], *y = b[@"Y"];
            // Menu-bar and status windows belong to no app's game frame; the
            // game window is the big one (1280x720 + title bar).
            if (w.doubleValue < 800.0 || h.doubleValue < 600.0) {
                continue;
            }
            printf("%u %g %g %g %g\n", ((NSNumber *)CFDictionaryGetValue(info, kCGWindowNumber)).unsignedIntValue,
                   x.doubleValue, y.doubleValue, w.doubleValue, h.doubleValue);
        }
        CFRelease(list);
        return 0;
    }

    if ([cmd isEqualToString:@"hold"] || [cmd isEqualToString:@"release"]) {
        // Several keys in ONE process, with no activate() in between: the point
        // is to get a modifier and its key down without the focus churn that
        // clears GLFW's key state (docs/05 §3.1 rule 2).
        const bool down = [cmd isEqualToString:@"hold"];
        for (int i = 2; i < argc; ++i) {
            key_event(atoi(argv[i]), down);
        }
        return 0;
    }

    if ([cmd isEqualToString:@"key"]) {
        key_event(atoi(argv[2]), strcmp(argv[3], "down") == 0);
        return 0;
    }

    if ([cmd isEqualToString:@"keytap"]) {
        const int code = atoi(argv[2]);
        const int ms = argc > 3 ? atoi(argv[3]) : 120;
        key_event(code, true);
        usleep((useconds_t)ms * 1000);
        key_event(code, false);
        return 0;
    }

    if ([cmd isEqualToString:@"click"] || [cmd isEqualToString:@"clicktap"]) {
        const int button = atoi(argv[2]);
        CGEventType down_type = button == 0 ? kCGEventLeftMouseDown : kCGEventRightMouseDown;
        CGEventType up_type = button == 0 ? kCGEventLeftMouseUp : kCGEventRightMouseUp;
        // Optional explicit location (wx wy) after the pulse length: aim the
        // event at the centre of the game window so a drifted real cursor
        // cannot route the click to another window. Without it the current
        // cursor position is used.
        CGFloat px = 0.0;
        CGFloat py = 0.0;
        if (argc >= 6) {
            px = atof(argv[4]);
            py = atof(argv[5]);
        } else {
            CGEventRef probe = CGEventCreate(NULL);
            CGPoint loc = probe ? CGEventGetLocation(probe) : CGPointMake(100, 100);
            if (probe) {
                CFRelease(probe);
            }
            px = loc.x;
            py = loc.y;
        }
        const CGPoint at = CGPointMake(px, py);
        if ([cmd isEqualToString:@"click"]) {
            post(CGEventCreateMouseEvent(NULL, strcmp(argv[3], "down") == 0 ? down_type : up_type, at,
                                         (CGMouseButton)button));
            return 0;
        }
        const int ms = argc > 3 ? atoi(argv[3]) : 120;
        post(CGEventCreateMouseEvent(NULL, down_type, at, (CGMouseButton)button));
        usleep((useconds_t)ms * 1000);
        post(CGEventCreateMouseEvent(NULL, up_type, at, (CGMouseButton)button));
        return 0;
    }

    if ([cmd isEqualToString:@"clickraw"]) {
        // Left click at an explicit point, WITHOUT the front/activate preamble:
        // used when keys are already held, because activating a window can make
        // GLFW drop them.
        const CGPoint at = CGPointMake(atof(argv[2]), atof(argv[3]));
        post(CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, at, kCGMouseButtonLeft));
        usleep(60 * 1000);
        post(CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, at, kCGMouseButtonLeft));
        return 0;
    }

    if ([cmd isEqualToString:@"move"]) {
        const int dx = atoi(argv[2]);
        const int dy = atoi(argv[3]);
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = CGEventGetLocation(probe);
        CFRelease(probe);
        CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointMake(loc.x + dx, loc.y + dy),
                                                kCGMouseButtonLeft);
        // The delta fields are what a pointer-locked GLFW client accumulates;
        // a plain absolute move produces delta 0 (docs/05 §3.1 findings).
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
        post(ev);
        return 0;
    }


    if ([cmd isEqualToString:@"trust"]) {
        // Read-only probe: does THIS binary hold the Accessibility grant that
        // CGEventPost needs? Without it the events are dropped silently.
        printf("accessibility trusted = %d\n", AXIsProcessTrusted());
        return 0;
    }

    if ([cmd isEqualToString:@"front"]) {
        pid_t pid = (pid_t)atoi(argv[2]);
        AXUIElementRef app = AXUIElementCreateApplication(pid);
        if (app == NULL) {
            fprintf(stderr, "no AX application for pid %d\n", pid);
            return 3;
        }
        // Raise + focus the app's window through the Accessibility API: on
        // macOS 14+ NSApplicationActivateIgnoringOtherApps is a no-op, so the
        // old activate path can no longer bring the window forward.
        CFTypeRef window = NULL;
        if (AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, &window) == kAXErrorSuccess && window != NULL) {
            AXUIElementPerformAction((AXUIElementRef)window, kAXRaiseAction);
            CFRelease(window);
        }
        AXUIElementSetAttributeValue(app, kAXFrontmostAttribute, kCFBooleanTrue);
        CFRelease(app);
        printf("front pid=%d\n", pid);
        return 0;
    }
    fprintf(stderr, "unknown cmd\n");
    return 2;
}
