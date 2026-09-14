// T-D1 FOV pair v2: aims at the distant hill (good scale landmark), then takes
// A=rest, B=sprint at ~180 ms after engagement (player has barely moved while
// the 0.5-easing FOV has nearly settled), C=sprint later, D=after release.
// Sprint via double-tap W (Ctrl+Space is eaten by macOS). Window BY PID.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const CGKeyCode KC_W=13;
static void post(CGKeyCode c,bool d){CGEventRef e=CGEventCreateKeyboardEvent(NULL,c,d);CGEventPost(kCGHIDEventTap,e);CFRelease(e);}
static void ms(int m){usleep((useconds_t)m*1000);}
static CGWindowID win(pid_t pid,CGRect*b){CFArrayRef L=CGWindowListCopyWindowInfo(kCGWindowListOptionAll|kCGWindowListExcludeDesktopElements,kCGNullWindowID);CGWindowID best=0;double a=0;
 for(CFIndex i=0;L&&i<CFArrayGetCount(L);++i){NSDictionary*d=(__bridge NSDictionary*)CFArrayGetValueAtIndex(L,i);
  if([d[@"kCGWindowOwnerPID"]intValue]!=pid)continue;NSDictionary*bb=d[@"kCGWindowBounds"];
  double w=[bb[@"Width"]doubleValue],h=[bb[@"Height"]doubleValue];
  if(w*h>a){a=w*h;best=(CGWindowID)[d[@"kCGWindowNumber"]unsignedIntValue];if(b)*b=CGRectMake([bb[@"X"]doubleValue],[bb[@"Y"]doubleValue],w,h);}}
 if(L)CFRelease(L);return best;}
static void shot(CGWindowID wid,const char*d,const char*n){char c[1200];snprintf(c,sizeof(c),"screencapture -x -o -l%u '%s/%s'",wid,d,n);printf("  shot %s rc=%d\n",n,system(c));}
int main(int argc,char**argv){
 pid_t pid=(pid_t)atoi(argv[1]);const char*dir=argv[2];
 NSRunningApplication*app=[NSRunningApplication runningApplicationWithProcessIdentifier:pid];
 if(!app){fprintf(stderr,"no app\n");return 1;}
 CGRect b=CGRectZero;CGWindowID wid=win(pid,&b);
 int cx=(int)(b.origin.x+b.size.width/2),cy=(int)(b.origin.y+b.size.height/2);
 printf("== pid=%d win=%u\n",pid,wid);
 post(KC_W,false);
 [app activateWithOptions:NSApplicationActivateAllWindows];ms(800);
 CGPoint p=CGPointMake(cx,cy);CGWarpMouseCursorPosition(p);ms(150);
 CGEventRef d1=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDown,p,kCGMouseButtonLeft);CGEventSetIntegerValueField(d1,kCGMouseEventClickState,1);CGEventPost(kCGHIDEventTap,d1);CFRelease(d1);ms(90);
 CGEventRef u1=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseUp,p,kCGMouseButtonLeft);CGEventSetIntegerValueField(u1,kCGMouseEventClickState,1);CGEventPost(kCGHIDEventTap,u1);CFRelease(u1);ms(900);
 // keep the spawn facing (the hill fills the frame and is far away -> pure scale)
 shot(wid,dir,"A_rest.png");
 // sprint, sampling early while the player has barely moved
 post(KC_W,true);ms(150);post(KC_W,false);ms(120);post(KC_W,true);
 ms(180); shot(wid,dir,"B_sprint_180ms.png");
 ms(120); shot(wid,dir,"C_sprint_300ms.png");
 ms(700); shot(wid,dir,"D_sprint_1000ms.png");
 post(KC_W,false); ms(1400);
 shot(wid,dir,"E_after_release.png");
 printf("== done\n");return 0;}
