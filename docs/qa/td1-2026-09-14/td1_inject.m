// T-D1 QA input injector (HID layer, per the project's documented rule:
// CGEventPost(kCGHIDEventTap) with an 86 ms pulse — osascript pulses are only
// 2-5 ms and the client's per-frame glfwGetKey polling misses them).
//
// Usage:
//   td1_inject key <keycode> <hold_ms>              (press, hold, release)
//   td1_inject keydown <keycode>                    (press and keep held)
//   td1_inject keyup <keycode>
//   td1_inject tap <keycode> <ms> <gap_ms> <n>      (n taps with a gap)
//   td1_inject click <0|1> <hold_ms>
//   td1_inject move <dx> <dy>
//   td1_inject double <keycode> <hold_ms> <gap_ms>  (two taps = double-tap)
//
// macOS keycodes: W=13, S=1, A=0, D=2, Space=49, LeftCtrl=59, LeftShift=56,
//                 ESC=53. HID "Left Control" is 59.
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void post_key(CGKeyCode code, bool down) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, code, down);
    // System-level HID tap: same path as real hardware, unlike
    // CGEventPostToPid (which was measured to drop keyups).
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

static void pulse(CGKeyCode code, int hold_ms) {
    post_key(code, true);
    usleep((useconds_t)hold_ms * 1000);
    post_key(code, false);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: td1_inject key|keydown|keyup|tap|double|click|move ...\n");
        return 2;
    }
    const char *cmd = argv[1];

    if (strcmp(cmd, "key") == 0 && argc >= 4) {
        pulse((CGKeyCode)atoi(argv[2]), argc >= 5 ? atoi(argv[3]) : 86);
        return 0;
    }
    if (strcmp(cmd, "keydown") == 0 && argc >= 3) {
        post_key((CGKeyCode)atoi(argv[2]), true);
        return 0;
    }
    if (strcmp(cmd, "keyup") == 0 && argc >= 3) {
        post_key((CGKeyCode)atoi(argv[2]), false);
        return 0;
    }
    if (strcmp(cmd, "tap") == 0 && argc >= 3) {
        const CGKeyCode code = (CGKeyCode)atoi(argv[2]);
        const int hold = argc >= 4 ? atoi(argv[3]) : 86;
        const int gap = argc >= 5 ? atoi(argv[4]) : 200;
        const int n = argc >= 6 ? atoi(argv[5]) : 1;
        for (int i = 0; i < n; ++i) {
            pulse(code, hold);
            if (i + 1 < n) {
                usleep((useconds_t)gap * 1000);
            }
        }
        return 0;
    }
    // Two short taps separated by `gap_ms` — the double-tap-forward sprint
    // gesture. Keep the gap under the 7-tick (350 ms) window.
    if (strcmp(cmd, "double") == 0 && argc >= 3) {
        const CGKeyCode code = (CGKeyCode)atoi(argv[2]);
        const int hold = argc >= 4 ? atoi(argv[3]) : 60;
        const int gap = argc >= 5 ? atoi(argv[4]) : 80;
        pulse(code, hold);
        usleep((useconds_t)gap * 1000);
        pulse(code, hold);
        return 0;
    }
    if (strcmp(cmd, "click") == 0 && argc >= 4) {
        const int button = atoi(argv[2]);
        const int hold = argc >= 5 ? atoi(argv[4]) : 86;
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = probe ? CGEventGetLocation(probe) : CGPointMake(0, 0);
        if (probe) {
            CFRelease(probe);
        }
        const CGEventType down = button == 0 ? kCGEventLeftMouseDown : kCGEventRightMouseDown;
        const CGEventType up = button == 0 ? kCGEventLeftMouseUp : kCGEventRightMouseUp;
        CGEventRef d = CGEventCreateMouseEvent(NULL, down, loc, (CGMouseButton)button);
        CGEventPost(kCGHIDEventTap, d);
        CFRelease(d);
        usleep((useconds_t)hold * 1000);
        CGEventRef u = CGEventCreateMouseEvent(NULL, up, loc, (CGMouseButton)button);
        CGEventPost(kCGHIDEventTap, u);
        CFRelease(u);
        return 0;
    }
    if (strcmp(cmd, "move") == 0 && argc >= 4) {
        const int dx = atoi(argv[2]);
        const int dy = atoi(argv[3]);
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = probe ? CGEventGetLocation(probe) : CGPointMake(0, 0);
        if (probe) {
            CFRelease(probe);
        }
        CGPoint dst = CGPointMake(loc.x + dx, loc.y + dy);
        CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, dst, kCGMouseButtonLeft);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
        return 0;
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
