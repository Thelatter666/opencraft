// T-D1 sprint-jump arc driver v4: rotate `rot_px` pixels first (mouse-look),
// then double-tap W to sprint and jump on the newly faced runway.
// Sprint via double-tap because holding Ctrl makes Space hit the macOS
// input-source-switch shortcut; window resolved BY PID.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const CGKeyCode KC_W=13,KC_SPACE=49;
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
 pid_t pid=(pid_t)atoi(argv[1]);const char*dir=argv[2];int rot=argc>3?atoi(argv[3]):0;const char*tag=argc>4?argv[4]:"a";
 NSRunningApplication*app=[NSRunningApplication runningApplicationWithProcessIdentifier:pid];
 if(!app){fprintf(stderr,"no app\n");return 1;}
 CGRect b=CGRectZero;CGWindowID wid=win(pid,&b);
 int cx=(int)(b.origin.x+b.size.width/2),cy=(int)(b.origin.y+b.size.height/2);
 printf("== pid=%d win=%u rot=%dpx tag=%s\n",pid,wid,rot,tag);
 post(KC_W,false);post(KC_SPACE,false);
 [app activateWithOptions:NSApplicationActivateAllWindows];ms(800);
 CGPoint p=CGPointMake(cx,cy);CGWarpMouseCursorPosition(p);ms(150);
 CGEventRef d1=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDown,p,kCGMouseButtonLeft);CGEventSetIntegerValueField(d1,kCGMouseEventClickState,1);CGEventPost(kCGHIDEventTap,d1);CFRelease(d1);ms(90);
 CGEventRef u1=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseUp,p,kCGMouseButtonLeft);CGEventSetIntegerValueField(u1,kCGMouseEventClickState,1);CGEventPost(kCGHIDEventTap,u1);CFRelease(u1);ms(900);
 // rotate by `rot` pixels (sign = direction), in 60px steps
 int done=0;
 while(done!=rot){ int step=rot-done; if(step>60)step=60; if(step<-60)step=-60;
   CGEventRef e=CGEventCreateMouseEvent(NULL,kCGEventMouseMoved,CGPointMake(cx+step,cy),kCGMouseButtonLeft);
   CGEventSetIntegerValueField(e,kCGMouseEventDeltaX,step);CGEventSetIntegerValueField(e,kCGMouseEventDeltaY,0);
   CGEventPost(kCGHIDEventTap,e);CFRelease(e); done+=step; ms(35); }
 ms(500);
 char n[128];
 snprintf(n,sizeof(n),"%s_rotated.png",tag); shot(wid,dir,n);
 // double-tap W -> sprint
 post(KC_W,true);ms(150);post(KC_W,false);ms(120);post(KC_W,true);
 ms(600);
 snprintf(n,sizeof(n),"%s_sprint.png",tag); shot(wid,dir,n);
 // sprint jumps
 for(int i=0;i<6;++i){ post(KC_SPACE,true);ms(90);post(KC_SPACE,false); ms(520);
   if(i==0){snprintf(n,sizeof(n),"%s_midair.png",tag);shot(wid,dir,n);} ms(760); }
 post(KC_W,false); ms(1200);
 printf("== done\n");return 0;}
