// T-I2 on-machine evidence tool (macOS). HID-layer input injection, per
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
    if (argc < 2) {
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

    if ([cmd isEqualToString:@"cursor"]) {
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = CGEventGetLocation(probe);
        CFRelease(probe);
        printf("%g %g\n", loc.x, loc.y);
        return 0;
    }

    if ([cmd isEqualToString:@"moveto"]) {
        // Absolute parking, done while the game is NOT frontmost (so the delta
        // cannot rotate a view): the delta is computed here, the event carries
        // it, and the physical cursor lands on the target.
        const int tx = atoi(argv[2]);
        const int ty = atoi(argv[3]);
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = CGEventGetLocation(probe);
        CFRelease(probe);
        const int dx = tx - (int)loc.x;
        const int dy = ty - (int)loc.y;
        CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointMake(tx, ty), kCGMouseButtonLeft);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
        post(ev);
        printf("moved %d %d -> %d %d\n", dx, dy, tx, ty);
        return 0;
    }

    fprintf(stderr, "unknown cmd\n");
    return 2;
}
