// Does step-assist fire through the REAL client adapter semantics?
// WorldSource does NOT override shape_top_at => default returns 1.0 for any
// solid block => the only risers are integral heights => with step_height 0.6
// no candidate lift is ever <= 0.6.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include "opencraft/physics/player_physics.hpp"
#include "physics_test_world.hpp"
using namespace opencraft::physics;
using physics_test::BoxWorld;
int main(){
  // integral risers only (what the client can express)
  for (double riser : {1.0, 2.0}) {
    BoxWorld w;
    w.solid(-256,9,63,63,-256,256);
    w.solid(10,256,63,63,-256,256);
    w.solid(10,256,64,(int)(63+riser),-256,256);
    PlayerState s; s.position={2.5,64.0,0.5}; s.on_ground=true; s.fall_peak_y=64.0;
    PhysicsConfig cfg = PhysicsConfig::for_entity(EntityKind::Player);
    std::set<double> lifts; bool any=false; double finalx=0;
    for(int t=0;t<60;++t){
      InputState in; in.yaw=-M_PI/2; in.forward=true;
      MoveResult r; step_player(s,in,w,cfg,&r);
      if(r.stepped){any=true; lifts.insert(r.step_height_used);}
      finalx=s.position.x;
    }
    printf("riser %.0f: stepped=%s final_x=%.3f\n", riser, any?"YES":"NO", finalx);
  }
  // sub-block risers (only reachable with the partial seam the tests use)
  for (double riser : {0.25,0.5,0.75}) {
    BoxWorld w;
    w.solid(-256,9,63,63,-256,256);
    w.solid(10,256,63,63,-256,256);
    w.partial(10,256,64,64,-256,256,riser);
    PlayerState s; s.position={2.5,64.0,0.5}; s.on_ground=true; s.fall_peak_y=64.0;
    PhysicsConfig cfg = PhysicsConfig::for_entity(EntityKind::Player);
    std::set<double> lifts; bool any=false; double finalx=0;
    for(int t=0;t<60;++t){
      InputState in; in.yaw=-M_PI/2; in.forward=true;
      MoveResult r; step_player(s,in,w,cfg,&r);
      if(r.stepped){any=true; lifts.insert(r.step_height_used);}
      finalx=s.position.x;
    }
    printf("riser %.2f (partial seam): stepped=%s final_x=%.3f lift=%.3f\n", riser, any?"YES":"NO", finalx, lifts.empty()?0.0:*lifts.begin());
  }
  return 0;
}
