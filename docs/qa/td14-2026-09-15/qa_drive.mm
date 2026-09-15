// T-D14 QA: HID-layer key injection + screenshots (the card mandates HID; no
// osascript). Modes:
//   walk      hold W (walk forward, may step up)
//   jump      tap Space
//   sneak     hold Left Shift
//   combo     hold W, then tap Space (jump while moving)
//
// Window id is resolved by PID (per the build-env lesson: matching by process
// NAME can hit a dead window left in the window list).
#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static pid_t g_pid = 0;

static CGWindowID window_id_for_pid(pid_t pid) {
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    CGWindowID best = 0;
    double best_area = 0.0;
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        NSDictionary *w = (__bridge NSDictionary *)CFArrayGetValueAtIndex(list, i);
        const pid_t owner = [w[(id)kCGWindowOwnerPID] intValue];
        if (owner != pid) {
            continue;
        }
        NSDictionary *b = w[(id)kCGWindowBounds];
        const double area = [b[@"Width"] doubleValue] * [b[@"Height"] doubleValue];
        if (area > best_area) {
            best_area = area;
            best = (CGWindowID)[w[(id)kCGWindowNumber] unsignedIntValue];
        }
    }
    CFRelease(list);
    return best;
}

static void post_key(CGKeyCode code, bool down) {
    CGEventRef e = CGEventCreateKeyboardEvent(NULL, code, down);
    if (!e) {
        return;
    }
    // A modifier key only registers as "held" if its own flag is set on the
    // event; clearing flags unconditionally made Left Shift invisible to the
    // client (measured: sneak never engaged).
    CGEventFlags f = 0;
    if (code == 56 || code == 60) {
        f |= kCGEventFlagMaskShift;
    }
    CGEventSetFlags(e, f);
    // The virtual key goes to the FRONTMOST app at the HID tap layer, so the
    // caller must have activated the target first (same process!).
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// 86 ms hold: long enough that a per-frame glfwGetKey poll cannot miss the
// pulse (the T009 finding: osascript's 2-5 ms pulses fall between samples).
static void tap_key(CGKeyCode code, useconds_t hold_us) {
    post_key(code, true);
    usleep(hold_us);
    post_key(code, false);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: qa_drive <pid> <walk|jump|sneak|combo|whold> [seconds]\n");
        return 2;
    }
    g_pid = static_cast<pid_t>(std::atoi(argv[1]));
    const char *mode = argv[2];
    const double secs = argc > 3 ? std::atof(argv[3]) : 3.0;

    const CGWindowID wid = window_id_for_pid(g_pid);
    std::fprintf(stderr, "pid=%d window=%u mode=%s\n", g_pid, (unsigned)wid, mode);
    if (wid == 0) {
        std::fprintf(stderr, "no window for pid (dead window or not on screen)\n");
        return 1;
    }

    // Bring the app to the front so HID events reach it. Done in this same
    // process right before the injection: every separate shell round-trip would
    // let the host steal focus back (build-env lesson).
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:g_pid];
    if (app) {
        [app activateWithOptions:NSApplicationActivateAllWindows];
        usleep(400 * 1000);
    }

    // Key codes: W=13, Space=49, LeftShift=56.
    if (std::strcmp(mode, "walk") == 0 || std::strcmp(mode, "whold") == 0) {
        post_key(13, true);
        usleep(static_cast<useconds_t>(secs * 1e6));
        post_key(13, false);
    } else if (std::strcmp(mode, "jump") == 0) {
        tap_key(49, 120 * 1000);
        usleep(static_cast<useconds_t>(secs * 1e6));
    } else if (std::strcmp(mode, "sneak") == 0) {
        post_key(56, true);
        usleep(static_cast<useconds_t>(secs * 1e6));
        post_key(56, false);
    } else if (std::strcmp(mode, "clickresume") == 0) {
        // Click the RESUME row (window y 310..346 fb px -> global 399..435).
        CGPoint p = CGPointMake(960.0, 417.0);
        CGEventRef mv = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, mv);
        CFRelease(mv);
        usleep(200 * 1000);
        CGEventRef dn = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, dn);
        CFRelease(dn);
        usleep(150 * 1000);
        CGEventRef up = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
        usleep(400 * 1000);
    } else if (std::strcmp(mode, "pauseclick") == 0 || std::strcmp(mode, "pauseclick2") == 0) {
        // T-D14: open the pause menu, then click the AUTO-JUMP button with real
        // mouse events (HID tap). Coordinates are GLOBAL SCREEN POINTS at 1x:
        // window origin (320,89) + button centre (640, 374) in framebuffer px.
        tap_key(53, 120 * 1000); // ESC -> pause
        usleep(700 * 1000);
        const int n = std::strcmp(mode, "pauseclick2") == 0 ? 2 : 1;
        for (int i = 0; i < n; ++i) {
            CGPoint p = CGPointMake(960.0, 453.0);
            CGEventRef mv = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap, mv);
            CFRelease(mv);
            usleep(200 * 1000);
            CGEventRef dn = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap, dn);
            CFRelease(dn);
            usleep(150 * 1000);
            CGEventRef up = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap, up);
            CFRelease(up);
            usleep(500 * 1000);
        }
    } else if (std::strcmp(mode, "esc") == 0) {
        tap_key(53, 120 * 1000);
        usleep(static_cast<useconds_t>(secs * 1e6));
    } else if (std::strcmp(mode, "esc2") == 0) {
        tap_key(53, 120 * 1000);
        usleep(300 * 1000);
        tap_key(53, 120 * 1000);
        usleep(static_cast<useconds_t>(secs * 1e6));
    } else if (std::strcmp(mode, "combo") == 0) {
        post_key(13, true);
        usleep(500 * 1000);
        tap_key(49, 120 * 1000);
        usleep(static_cast<useconds_t>(secs * 1e6));
        post_key(13, false);
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }

    // Window screenshot at the end (screencapture does not steal focus, T-D1).
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd), "screencapture -x -o -l%u /tmp/td14_qa/shot-%s.png", (unsigned)wid, mode);
    if (std::system(cmd) != 0) {
        std::fprintf(stderr, "screencapture failed\n");
    }
    std::fprintf(stderr, "done\n");
    return 0;
}
