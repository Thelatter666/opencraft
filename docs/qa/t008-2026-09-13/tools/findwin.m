#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>

int main(void) {
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        CFDictionaryRef info = CFArrayGetValueAtIndex(list, i);
        CFStringRef owner = CFDictionaryGetValue(info, kCGWindowOwnerName);
        NSNumber *wid = CFDictionaryGetValue(info, kCGWindowNumber);
        NSDictionary *b = (__bridge NSDictionary *)CFDictionaryGetValue(info, kCGWindowBounds);
        NSString *o = (__bridge NSString *)owner;
        if (o && [o containsString:@"opencraft"]) {
            NSNumber *w = b[@"Width"], *h = b[@"Height"], *x = b[@"X"], *y = b[@"Y"];
            if (w.doubleValue > 1000) {
                printf("%u %g %g %g %g\n", wid.unsignedIntValue, x.doubleValue, y.doubleValue, w.doubleValue, h.doubleValue);
            }
        }
    }
    CFRelease(list);
    return 0;
}
