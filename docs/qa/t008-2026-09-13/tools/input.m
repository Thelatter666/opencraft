// Minimal input injector for headless GUI acceptance on macOS.
// Usage (pid = target process, or 0 for session-wide posting):
//   input <pid> key <keycode> down|up
//   input <pid> click <0|1> down|up   (0 = left, 1 = right)
//   input <pid> move <dx> <dy>        (pointer-locked mouse deltas)
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "bad args\n"); return 2; }
    const pid_t pid = (pid_t)atoi(argv[1]);
    NSString *cmd = [NSString stringWithUTF8String:argv[2]];

    if ([cmd isEqualToString:@"key"]) {
        int code = atoi(argv[3]);
        bool down = strcmp(argv[4], "down") == 0;
        CGEventRef ev = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)code, down);
        if (pid > 0) { CGEventPostToPid(pid, ev); } else { CGEventPost(kCGSessionEventTap, ev); }
        CFRelease(ev);
        return 0;
    }
    if ([cmd isEqualToString:@"click"]) {
        int button = atoi(argv[3]);
        bool down = strcmp(argv[4], "down") == 0;
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = probe ? CGEventGetLocation(probe) : CGPointMake(100, 100);
        if (probe) { CFRelease(probe); }
        CGEventType type = down ? (button == 0 ? kCGEventLeftMouseDown : kCGEventRightMouseDown)
                                : (button == 0 ? kCGEventLeftMouseUp : kCGEventRightMouseUp);
        CGEventRef ev = CGEventCreateMouseEvent(NULL, type, loc, (CGMouseButton)button);
        if (pid > 0) { CGEventPostToPid(pid, ev); } else { CGEventPost(kCGSessionEventTap, ev); }
        CFRelease(ev);
        return 0;
    }
    if ([cmd isEqualToString:@"clickat"]) {
        // clickat <pid> clickat <x> <y> <button> down|up  (absolute screen coords)
        int x = atoi(argv[3]);
        int y = atoi(argv[4]);
        int button = atoi(argv[5]);
        bool down = strcmp(argv[6], "down") == 0;
        CGEventType type = down ? (button == 0 ? kCGEventLeftMouseDown : kCGEventRightMouseDown)
                                : (button == 0 ? kCGEventLeftMouseUp : kCGEventRightMouseUp);
        CGWarpMouseCursorPosition(CGPointMake(x, y));
        usleep(30000);
        CGEventRef ev = CGEventCreateMouseEvent(NULL, type, CGPointMake(x, y), (CGMouseButton)button);
        CGEventSetIntegerValueField(ev, kCGMouseEventClickState, 1);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
        usleep(60000);
        CGEventRef up_ev = NULL;
        (void)up_ev;
        return 0;
    }
    if ([cmd isEqualToString:@"move"]) {
        int dx = atoi(argv[3]);
        int dy = atoi(argv[4]);
        CGEventRef probe = CGEventCreate(NULL);
        CGPoint loc = CGEventGetLocation(probe);
        CFRelease(probe);
        CGPoint dst = CGPointMake(loc.x + dx, loc.y + dy);
        CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, dst, kCGMouseButtonLeft);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
        CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
        if (pid > 0) { CGEventPostToPid(pid, ev); } else { CGEventPost(kCGSessionEventTap, ev); }
        CFRelease(ev);
        return 0;
    }
    fprintf(stderr, "unknown cmd\n");
    return 2;
}
