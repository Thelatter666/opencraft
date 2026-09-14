// T-D1 real-machine QA driver: window lookup BY PID -> activate -> focus click
// -> HID inject -> captures, all in ONE process. Window lookup filters on
// kCGWindowOwnerPID (name matching returned windows of already-dead instances).
// Captures shell out to screencapture(1) (CGWindowListCreateImage is obsoleted
// on macOS 15); screencapture was measured NOT to change the frontmost app.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const CGKeyCode KC_W = 13, KC_CTRL = 59, KC_SPACE = 49;

static void post(CGKeyCode code, bool down) {
    CGEventRef ev = CGEventCreateKeyboardEvent(NULL, code, down);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }

static void capture(CGWindowID wid, const char *outdir, const char *name) {
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "screencapture -x -o -l%u '%s/%s'", wid, outdir, name);
    const int rc = system(cmd);
    printf("  shot: %s (rc=%d)\n", name, rc);
}

static CGWindowID window_of_pid(pid_t pid, CGRect *bounds) {
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    CGWindowID best = 0;
    double best_area = 0;
    for (CFIndex i = 0; list && i < CFArrayGetCount(list); ++i) {
        NSDictionary *d = (__bridge NSDictionary *)CFArrayGetValueAtIndex(list, i);
        if ([d[@"kCGWindowOwnerPID"] intValue] != pid) continue;
        NSDictionary *b = d[@"kCGWindowBounds"];
        const double w = [b[@"Width"] doubleValue], h = [b[@"Height"] doubleValue];
        if (w * h > best_area) {
            best_area = w * h;
            best = (CGWindowID)[d[@"kCGWindowNumber"] unsignedIntValue];
            if (bounds) *bounds = CGRectMake([b[@"X"] doubleValue], [b[@"Y"] doubleValue], w, h);
        }
    }
    if (list) CFRelease(list);
    return best;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: qa_drive <pid> <outdir>\n"); return 2; }
    const pid_t pid = (pid_t)atoi(argv[1]);
    const char *outdir = argv[2];
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    if (!app) { fprintf(stderr, "no running app for pid %d\n", pid); return 1; }
    CGRect b = CGRectZero;
    const CGWindowID wid = window_of_pid(pid, &b);
    if (wid == 0) { fprintf(stderr, "no window owned by pid %d\n", pid); return 1; }
    const int cx = (int)(b.origin.x + b.size.width / 2);
    const int cy = (int)(b.origin.y + b.size.height / 2);
    printf("== pid=%d window=%u bounds=%.0fx%.0f centre=(%d,%d)\n", pid, wid, b.size.width, b.size.height, cx, cy);

    post(KC_W, false); post(KC_CTRL, false); post(KC_SPACE, false);
    [app activateWithOptions:NSApplicationActivateAllWindows];
    sleep_ms(700);

    CGPoint p = CGPointMake(cx, cy);
    CGWarpMouseCursorPosition(p);
    sleep_ms(150);
    CGEventRef d = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
    CGEventSetIntegerValueField(d, kCGMouseEventClickState, 1);
    CGEventPost(kCGHIDEventTap, d); CFRelease(d);
    sleep_ms(90);
    CGEventRef u = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
    CGEventSetIntegerValueField(u, kCGMouseEventClickState, 1);
    CGEventPost(kCGHIDEventTap, u); CFRelease(u);
    sleep_ms(800);
    printf("== frontmost=%s appActive=%s\n",
           [[[[NSWorkspace sharedWorkspace] frontmostApplication] localizedName] UTF8String],
           [app isActive] ? "yes" : "no");

    capture(wid, outdir, "00_static.png");

    printf("== walk: W held 2.5s\n");
    post(KC_W, true); sleep_ms(2500);
    capture(wid, outdir, "01_walk.png");
    post(KC_W, false); sleep_ms(1200);

    printf("== double-tap W\n");
    post(KC_W, true); sleep_ms(60);
    post(KC_W, false); sleep_ms(80);
    post(KC_W, true); sleep_ms(1300);
    capture(wid, outdir, "02_doubletap_sprint.png");
    sleep_ms(1400);
    capture(wid, outdir, "03_doubletap_sprint_later.png");
    post(KC_W, false); sleep_ms(1200);

    printf("== ctrl+W sprint\n");
    post(KC_CTRL, true); sleep_ms(150);
    post(KC_W, true); sleep_ms(2600);
    capture(wid, outdir, "04_sprint_ctrl_fov.png");
    sleep_ms(1400);
    capture(wid, outdir, "05_sprint_ctrl_fov_later.png");

    printf("== sprint jumps x3\n");
    for (int i = 0; i < 3; ++i) {
        post(KC_SPACE, true); sleep_ms(90); post(KC_SPACE, false);
        sleep_ms(600);
        char name[64];
        snprintf(name, sizeof(name), "06_sprint_jump_%d.png", i + 1);
        capture(wid, outdir, name);
        sleep_ms(1000);
    }
    post(KC_W, false); post(KC_CTRL, false);
    sleep_ms(1000);
    capture(wid, outdir, "07_after_release.png");
    printf("== done\n");
    return 0;
}
