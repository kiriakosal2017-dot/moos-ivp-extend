/************************************************************/
/*    NAME: Mike Benjamin                                   */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.cpp                                   */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#include <iterator>
#include <vector>
#include <algorithm>
#include <cmath>
#include <unistd.h>
#include "GenRescue.h"
#include "MBUtils.h"
#include "ColorParse.h"
#include "XYPoint.h"
#include "XYSegList.h"
#include "GeomUtils.h"
#include "PathUtils.h"
#include "XYFormatUtilsPoly.h"
#include "XYFieldGenerator.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

GenRescue::GenRescue()
{
  // Initialize state variables
  m_nav_x = 0;
  m_nav_y = 0;
  m_nav_x_set = 0;
  m_nav_y_set = 0;

  m_plan_pending  = false;
  m_rescued_count = 0;
  m_strategy = "nn";      // DEFAULT = efficient nearest-neighbour collector + MAX SPEED.
                          // The real competition launches the two boats FAR APART, so
                          // there is NO early COLREGS -- the game reduces to "who collects
                          // their share fastest" = pure efficiency. nn's greedy tour is
                          // ~40% shorter than snake's lawnmower (deterministic path-length
                          // analysis) and front-loads nearby swimmers (best for a claiming
                          // race); postPath() drives it at the helm-domain max 1.6 (vs the
                          // stem bhv's 1.2) for a further +33% rate. The earlier "snake is
                          // champion" was an ARTIFACT of the harness starting both boats ~7m
                          // apart, which forced a fake head-on COLREGS that penalized fast
                          // racers and favored lane-sweeps; it does not happen with far
                          // starts. Path-based -> standard BHV_Waypoint, deliverable-safe.
  m_cur_target_id = -1;   // dev: no committed target yet
  m_adapt_mode = -1;      // adapt: mode decided+locked on first confident plan
  m_snk_dir = -1; m_snk_xflip = 0;     // snk_rand: orientation chosen on first plan
  m_transit_speed = 1.6;               // helm-domain max (domain speed:0:1.6); stem bhv default is 1.2
  srand((unsigned) getpid());          // distinct RNG per match process

  m_opp_set = false;
  m_opp_x = 0; m_opp_y = 0;

  m_last_plan_time  = 0;
  m_replan_interval = 15;   // game-seconds between forced re-plans

  srand((unsigned)getpid());   // distinct random tours per vehicle process
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool GenRescue::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key  = msg.GetKey();
    string sval = msg.GetString();

    bool handled = true;
    if(key == "SWIMMER_ALERT") 
      handled = handleMailNewSwimmer(sval);
    else if(key == "FOUND_SWIMMER") 
      handled = handleMailFoundSwimmer(sval);
    else if(key == "NAV_X") {
      m_nav_x = msg.GetDouble();
      m_nav_x_set = true;
    }
    else if(key == "NAV_Y") {
      m_nav_y = msg.GetDouble();
      m_nav_y_set = true;
    }
    else if(key == "NODE_REPORT")
      handled = handleMailNodeReport(sval);

    else if(key != "APPCAST_REQ") // handle by AppCastingMOOSApp
      handled = false;
    
    if(!handled)
      reportRunWarning("Unhandled Mail: " + key +"=" + sval);
    
  }
  return(true);
}
 
//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool GenRescue::OnConnectToServer()
{
  RegisterVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()

bool GenRescue::Iterate()
{
  AppCastingMOOSApp::Iterate();

  // While swimmers remain, keep the vehicle in SURVEYING. When the survey
  // path completes, BHV_Waypoint fires endflag RETURN=true; forcing RETURN
  // back to false toggles MODE out of and back into SURVEYING, which re-
  // initialises waypt_survey from point 0 -- i.e. it RE-RUNS the snake path,
  // re-attempting any swimmer slipped past (slip_radius 8m > rescue 5m) on
  // the previous loop. This is our "repeat" mechanism.
  bool have_nav = m_nav_x_set && m_nav_y_set;
  if(!m_swimmers.empty())
    Notify("RETURN", "false");

  // Re-plan only when the swimmer set changed (new alert or a rescue). The
  // path is a fixed snake, so we must NOT re-post periodically -- updating
  // points resets the behaviour to vertex 0, which at a 15s cadence would
  // restart the boat before it gets anywhere.
  // devb (PHASE 2) is REACTIVE: republish RESCUE_TGT every tick for the custom
  // BHV_Rescue behaviour (no waypoint index -> no thrash). All other strategies
  // post a fresh waypoint path only when the swimmer set changed (a new alert);
  // re-posting resets BHV_Waypoint to vertex 0 so we must not do it every tick.
  if(have_nav && m_strategy == "devb")
    planDevB();
  else if(have_nav && m_strategy == "hunt")
    planHunt();
  else if(have_nav && m_plan_pending)
    planPath();

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()

bool GenRescue::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp(); 

  STRING_LIST sParams;
  m_MissionReader.GetConfiguration(GetAppName(), sParams);
  
  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string sLine  = *p;
    string param  = tolower(biteStringX(sLine, '='));
    string value  = sLine;
    if(param == "vname")
      m_vname = value;
    else if(param == "strategy")
      m_strategy = tolower(value);
  }
  
  RegisterVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: RegisterVariables()

void GenRescue::RegisterVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("SWIMMER_ALERT", 0);
  Register("FOUND_SWIMMER", 0);
  // Ownship position: without these, m_nav_*_set never becomes true and
  // planPath() never runs -- the swimmer-aware path is never posted
  // and the vehicle silently falls back to the .bhv default waypoints.
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
  Register("NODE_REPORT", 0);
}


//---------------------------------------------------------
// Procedure: handleMailNewSwimmer()

bool GenRescue::handleMailNewSwimmer(string str)
{
  // str example: "x=34, y=85, id=21"
  double x, y, id_d;
  bool ok_x  = tokParse(str, "x",  ',', '=', x);
  bool ok_y  = tokParse(str, "y",  ',', '=', y);
  bool ok_id = tokParse(str, "id", ',', '=', id_d);
  if(!ok_x || !ok_y || !ok_id)
    return(false);

  int id = (int)(id_d);
  // A brand-new swimmer id means the tour must be re-planned.
  // (Re-broadcasts of a known id just refresh its position.)
  if(m_swimmers.count(id) == 0)
    m_plan_pending = true;
  m_swimmers[id] = XYPoint(x, y);
  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailFoundSwimmer()

bool GenRescue::handleMailFoundSwimmer(string str)
{
  // str example: "id=21, finder=abe"
  double id_d;
  if(!tokParse(str, "id", ',', '=', id_d))
    return(false);

  int id = (int)(id_d);
  if(m_swimmers.count(id)) {
    m_swimmers.erase(id);   // drop the rescued swimmer from bookkeeping
    m_rescued_count++;
    // NOTE: deliberately do NOT re-plan here for snake/greedy/random. Re-posting
    // SURVEY_UPDATE resets BHV_Waypoint's index to vertex 0 (the bottom lane),
    // so re-planning on every rescue kept yanking the boat back to the bottom --
    // it yo-yo'd in the lower lanes and never reached the top-edge swimmers (all
    // 5 misses on athens_02 had y in [-10,-2], the LAST lane). One uninterrupted
    // snake from bottom to top fits easily in the time budget (~500m path vs
    // 1.2 m/s x ~1140s). Keeping the erase (above) lets m_swimmers empty out
    // when all are rescued, which releases the RETURN guard so the boat heads
    // home cleanly.
    // dev too: do NOT re-plan on a rescue. BHV_Waypoint auto-advances through the
    // posted tour, so re-planning here only re-posts SURVEY_UPDATE and resets the
    // waypoint index -> thrash -> boat rescues ~0. dev still re-plans on a NEW
    // swimmer (handleMailNewSwimmer), which is the genuinely dynamic event.
    //
    // EXCEPTION: 'claim' WANTS to re-plan on every claim -- it must skip swimmers
    // the opponent just stole (else it drives to an empty spot). Safe here because
    // claim re-builds a greedy tour FROM ownship: vertex 0 is always the nearest
    // live swimmer ahead, so re-posting never yanks the boat backward.
    if(m_strategy == "claim")
      m_plan_pending = true;
  }
  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailNodeReport()

bool GenRescue::handleMailNodeReport(string str)
{
  string name; double x = 0, y = 0;
  bool ok_n = tokParse(str, "NAME", ',', '=', name);
  bool ok_x = tokParse(str, "X",    ',', '=', x);
  bool ok_y = tokParse(str, "Y",    ',', '=', y);
  if(!ok_n || !ok_x || !ok_y)
    return(false);
  if(name == m_vname)   // ignore our own relayed report
    return(true);
  m_opp_name = name; m_opp_x = x; m_opp_y = y; m_opp_set = true;
  return(true);
}

//---------------------------------------------------------
// Procedure: planPath()

void GenRescue::planPath()
{
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_swimmers.empty()) {
    postNullPath();
    m_plan_pending = false;
    return;
  }
  if(m_strategy == "dev" || m_strategy == "nn") planDev();   // dev == nn (NN collector)
  else if(m_strategy == "claim")  planClaim();
  else if(m_strategy == "adapt")  planAdapt();
  else if(m_strategy == "vor")    planVor();
  else if(m_strategy == "vorx")   planVorx();
  else if(m_strategy == "vori")   planVori();
  else if(m_strategy == "cen")    planCen();
  else if(m_strategy == "auc")    planAuc();
  else if(m_strategy == "snk_near") planSnkNear();
  else if(m_strategy == "snk_fine") planSnkFine();
  else if(m_strategy == "snk_wide") planSnkWide();
  else if(m_strategy == "snk_rand") planSnkRand();
  else if(m_strategy == "champ1") planChamp1();  // frozen hall-of-fame opponent
  else if(m_strategy == "greedy") planGreedy();
  else if(m_strategy == "random") planRandom();
  else                            planSnake();   // default/frozen
  m_plan_pending   = false;
  m_last_plan_time = m_curr_time;
}

//---------------------------------------------------------
// Procedure: postPath()

void GenRescue::postPath(const XYSegList& path)
{
  m_path = path;
  m_path.set_label("one");
  Notify("VIEW_SEGLIST", m_path.get_spec());
  // Drive at the helm-domain max speed instead of the stem behaviour's 1.2 m/s.
  // The rescue helm domain is speed:0:1.6, so 1.6 is the fastest the helm can
  // command -- a free ~33% boost to collection rate in a race that ends when the
  // field is empty. Sent as a BHV_Waypoint update ('#'-separated params) next to
  // the points, so the same SURVEY_UPDATE sets both the tour and its speed.
  string upd = "speed=" + doubleToStringX(m_transit_speed,2) +
               " # points = " + m_path.get_spec_pts();
  Notify("SURVEY_UPDATE", upd);
  reportEvent("SURVEY_UPDATE " + upd);
}

//---------------------------------------------------------
// Procedure: planSnake()

void GenRescue::planSnake()
{
  std::vector<XYPoint> pts;
  std::map<int, XYPoint>::iterator p;
  double ymin = 0; bool first = true;
  for(p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    pts.push_back(p->second);
    if(first || (p->second.y() < ymin)) { ymin = p->second.y(); first = false; }
  }

  // ...and order them as a boustrophedon (snake) scan: bin into horizontal
  // lanes ~2*rescue_range tall, sweep +x along even lanes and -x along odd
  // ones. Gentle, sweep-like turns keep the boat from corner-cutting and
  // slipping past swimmers (slip_radius 8m > rescue range 5m) the way a
  // jagged nearest-neighbour tour does -- while every vertex still sits on
  // a real swimmer.
  double lane_h = 10.0;
  std::sort(pts.begin(), pts.end(),
    [&](const XYPoint &a, const XYPoint &b) {
      int la = (int)floor((a.y() - ymin) / lane_h);
      int lb = (int)floor((b.y() - ymin) / lane_h);
      if(la != lb) return la < lb;
      return (la % 2 == 0) ? (a.x() < b.x()) : (a.x() > b.x());
    });

  XYSegList path;
  for(size_t i = 0; i < pts.size(); i++)
    path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}

//---------------------------------------------------------
// Procedure stubs: planGreedy(), planRandom(), planDev()

void GenRescue::planGreedy()
{
  std::vector<XYPoint> remaining;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++)
    remaining.push_back(p->second);

  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!remaining.empty()) {
    size_t best = 0; double bestd = -1;
    for(size_t i = 0; i < remaining.size(); i++) {
      double d = hypot(remaining[i].x() - cx, remaining[i].y() - cy);
      if(bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    cx = remaining[best].x(); cy = remaining[best].y();
    path.add_vertex(cx, cy);
    remaining.erase(remaining.begin() + best);
  }
  postPath(path);
}

void GenRescue::planRandom()
{
  std::vector<XYPoint> pts;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++)
    pts.push_back(p->second);
  for(size_t i = pts.size(); i > 1; i--) {   // Fisher-Yates
    size_t j = (size_t)(rand() % i);
    std::swap(pts[i-1], pts[j]);
  }
  XYSegList path;
  for(size_t i = 0; i < pts.size(); i++)
    path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}

void GenRescue::planDev()
{
  // Executor architecture: pGenRescue does the THINKING (choose the next
  // target); the custom BHV_Rescue behaviour does the DRIVING. We publish the
  // chosen target as RESCUE_TGT="x,y"; BHV_Rescue builds a course+speed
  // objective toward it each tick with no waypoint index, so we can republish
  // every Iterate (keeping the target fresh after each rescue) with zero thrash.
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_swimmers.empty()) {
    Notify("RESCUE_TGT", "");      // nothing left -> behaviour idles
    return;
  }

  // CHAMPION: plain aggressive nearest-neighbour tour over ALL known swimmers
  // from ownship -> BHV_Waypoint, re-planned on each new swimmer. In a slow-boat,
  // time-limited race, front-loading nearby rescues beats every "clever" variant
  // tested head-to-head on identical scenarios: 2-opt path (worse), opponent-aware
  // ordering S1 (within noise), centrality S4 (lost 3-11/6-7), auction+preemption
  // S6 (0.278 vs NN 0.389). Detours cost rescues; plain NN wins.
  std::vector<XYPoint> rem;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++)
    rem.push_back(p->second);
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!rem.empty()) {
    size_t bi = 0; double bd = -1;
    for(size_t i = 0; i < rem.size(); i++) {
      double d = hypot(rem[i].x()-cx, rem[i].y()-cy);
      if(bd < 0 || d < bd) { bd = d; bi = i; }
    }
    cx = rem[bi].x(); cy = rem[bi].y();
    path.add_vertex(cx, cy);
    rem.erase(rem.begin() + bi);
  }
  postPath(path);
}

//---------------------------------------------------------
// Procedure: planClaim()
//   CONTESTED-RACE champion. The competition runs until the field is empty and
//   every swimmer goes to whoever reaches it first, so the score is OUR claims,
//   not total path length. Three levers, in order of impact:
//     1) MAX SPEED  -- postPath() drives at the helm-domain max (1.6 vs the stem
//        1.2), a free ~33% collection-rate boost.
//     2) FRONT-LOAD -- greedy nearest-neighbour FROM ownship secures the easy,
//        nearby claims first (a min-total-length 2-opt tour would defer nearby
//        swimmers and let them get stolen; front-loading beats it in a race).
//     3) OPPONENT-AWARE -- visit swimmers we can plausibly win first (our dist
//        within a contest margin of the opponent's); concede only clearly-lost
//        ones to the end (still collected if the opponent skips them).
//   Re-planned on every swimmer-set change (new alert OR a claim by either boat;
//   see handleMailFoundSwimmer) so we never drive to an already-rescued spot.
//   Re-planning a greedy tour FROM ownship cannot thrash: vertex 0 is always the
//   nearest live swimmer ahead, never a backward jump to vertex 0 of a fixed lane.

void GenRescue::planClaim()
{
  const double margin = 10.0;   // contest the middle; concede only clearly-lost swimmers
  std::vector<XYPoint> ours, theirs;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    if(m_opp_set) {
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      double od = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      if(ud <= od + margin) ours.push_back(p->second); else theirs.push_back(p->second);
    } else ours.push_back(p->second);
  }
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  for(int phase = 0; phase < 2; phase++) {           // phase 0 = winnable, phase 1 = conceded mop-up
    std::vector<XYPoint>& set = (phase==0) ? ours : theirs;
    while(!set.empty()) {
      size_t bi=0; double bd=-1;
      for(size_t i=0;i<set.size();i++){ double d=hypot(set[i].x()-cx,set[i].y()-cy); if(bd<0||d<bd){bd=d;bi=i;} }
      cx=set[bi].x(); cy=set[bi].y(); path.add_vertex(cx,cy); set.erase(set.begin()+bi);
    }
  }
  postPath(path);
}

//---------------------------------------------------------
// Procedure: planDevB()
//   PHASE 2: feed the custom BHV_Rescue behaviour. Publish a single target point
//   RESCUE_TGT="x,y" and commit to it until rescued so the boat doesn't oscillate.

void GenRescue::planDevB()
{
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_swimmers.empty()) {
    Notify("RESCUE_TGT", "");
    m_cur_target_id = -1;
    return;
  }
  if((m_cur_target_id < 0) || (m_swimmers.count(m_cur_target_id) == 0)) {
    double bestd = -1; int bid = -1;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
        p != m_swimmers.end(); p++) {
      double d = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      if(bestd < 0 || d < bestd) { bestd = d; bid = p->first; }
    }
    m_cur_target_id = bid;
  }
  XYPoint t = m_swimmers[m_cur_target_id];
  Notify("RESCUE_TGT", doubleToStringX(t.x(),2) + "," + doubleToStringX(t.y(),2));
}

//---------------------------------------------------------
// TOURNAMENT contenders (frozen). Each is a complete strategy that competes
// head-to-head against the others in the round-robin. nn == planDev.

void GenRescue::planVor()   // opponent-aware: our-Voronoi swimmers first, then mop up
{
  std::vector<XYPoint> ours, theirs;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    if(m_opp_set) {
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      double od = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      if(ud <= od) ours.push_back(p->second); else theirs.push_back(p->second);
    } else ours.push_back(p->second);
  }
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  for(int phase = 0; phase < 2; phase++) {
    std::vector<XYPoint>& set = (phase==0) ? ours : theirs;
    while(!set.empty()) {
      size_t bi=0; double bd=-1;
      for(size_t i=0;i<set.size();i++){ double d=hypot(set[i].x()-cx,set[i].y()-cy); if(bd<0||d<bd){bd=d;bi=i;} }
      cx=set[bi].x(); cy=set[bi].y(); path.add_vertex(cx,cy); set.erase(set.begin()+bi);
    }
  }
  postPath(path);
}

void GenRescue::planVorx()  // vor with a generous claim margin (contest the middle)
{
  const double margin = 15.0;
  std::vector<XYPoint> ours, theirs;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    if(m_opp_set) {
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      double od = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      if(ud <= od + margin) ours.push_back(p->second); else theirs.push_back(p->second);
    } else ours.push_back(p->second);
  }
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  for(int phase=0; phase<2; phase++){
    std::vector<XYPoint>& set = (phase==0) ? ours : theirs;
    while(!set.empty()){
      size_t bi=0; double bd=-1;
      for(size_t i=0;i<set.size();i++){ double d=hypot(set[i].x()-cx,set[i].y()-cy); if(bd<0||d<bd){bd=d;bi=i;} }
      cx=set[bi].x(); cy=set[bi].y(); path.add_vertex(cx,cy); set.erase(set.begin()+bi);
    }
  }
  postPath(path);
}

void GenRescue::planVori()  // vor + interception: within ours, grab most-threatened first (bounded detour)
{
  const double THREAT = 40.0, STEAL_FACTOR = 1.3;
  std::vector<XYPoint> ours, theirs;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++) {
    if(m_opp_set) {
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      double od = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      if(ud <= od) ours.push_back(p->second); else theirs.push_back(p->second);
    } else ours.push_back(p->second);
  }
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  // ours: prefer the most-threatened (opponent nearest) swimmer if within a bounded detour, else nearest.
  while(!ours.empty()){
    double ndist=-1;
    for(size_t i=0;i<ours.size();i++){ double d=hypot(ours[i].x()-cx,ours[i].y()-cy); if(ndist<0||d<ndist)ndist=d; }
    size_t bi=0; double bd=-1; size_t ci=0; double cthreat=-1; bool contested=false;
    for(size_t i=0;i<ours.size();i++){
      double ud=hypot(ours[i].x()-cx,ours[i].y()-cy);
      if(bd<0||ud<bd){ bd=ud; bi=i; }
      if(m_opp_set){
        double od=hypot(ours[i].x()-m_opp_x,ours[i].y()-m_opp_y);
        if((od<THREAT)&&(ud<=STEAL_FACTOR*ndist)){ if(cthreat<0||od<cthreat){ cthreat=od; ci=i; contested=true; } }
      }
    }
    size_t pick=contested?ci:bi;
    cx=ours[pick].x(); cy=ours[pick].y(); path.add_vertex(cx,cy); ours.erase(ours.begin()+pick);
  }
  while(!theirs.empty()){
    size_t bi=0; double bd=-1;
    for(size_t i=0;i<theirs.size();i++){ double d=hypot(theirs[i].x()-cx,theirs[i].y()-cy); if(bd<0||d<bd){bd=d;bi=i;} }
    cx=theirs[bi].x(); cy=theirs[bi].y(); path.add_vertex(cx,cy); theirs.erase(theirs.begin()+bi);
  }
  postPath(path);
}

void GenRescue::planCen()   // centrality-weighted NN
{
  const double W = 0.5;
  std::vector<XYPoint> rem;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++)
    rem.push_back(p->second);
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!rem.empty()) {
    double sx=0, sy=0;
    for(size_t i=0;i<rem.size();i++){ sx+=rem[i].x(); sy+=rem[i].y(); }
    double gx=sx/rem.size(), gy=sy/rem.size();
    size_t bi=0; double bs=-1;
    for(size_t i=0;i<rem.size();i++){
      double s=hypot(rem[i].x()-cx,rem[i].y()-cy)+W*hypot(rem[i].x()-gx,rem[i].y()-gy);
      if(bs<0||s<bs){ bs=s; bi=i; }
    }
    cx=rem[bi].x(); cy=rem[bi].y(); path.add_vertex(cx,cy); rem.erase(rem.begin()+bi);
  }
  postPath(path);
}

void GenRescue::planAuc()   // auction/preemption: steal contested-winnable (bounded detour)
{
  const double THREAT = 40.0, STEAL_FACTOR = 1.3;
  std::vector<XYPoint> rem;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++)
    rem.push_back(p->second);
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!rem.empty()) {
    double ndist=-1;
    for(size_t i=0;i<rem.size();i++){ double d=hypot(rem[i].x()-cx,rem[i].y()-cy); if(ndist<0||d<ndist)ndist=d; }
    size_t bi=0; double bd=-1; size_t ci=0; double cd=-1; bool contested=false;
    for(size_t i=0;i<rem.size();i++){
      double ud=hypot(rem[i].x()-cx,rem[i].y()-cy);
      if(bd<0||ud<bd){ bd=ud; bi=i; }
      if(m_opp_set){
        double od=hypot(rem[i].x()-m_opp_x,rem[i].y()-m_opp_y);
        if((ud<=od)&&(od<THREAT)&&(ud<=STEAL_FACTOR*ndist)){ if(cd<0||ud<cd){ cd=ud; ci=i; contested=true; } }
      }
    }
    size_t pick = contested?ci:bi;
    cx=rem[pick].x(); cy=rem[pick].y(); path.add_vertex(cx,cy); rem.erase(rem.begin()+pick);
  }
  postPath(path);
}

// Shared boustrophedon builder for snake variants. lane_h = lane height;
// near_start = begin the sweep from the field end nearest ownship (less wasted
// initial travel than always starting at the bottom lane).
void GenRescue::snakeOrder(double lane_h, bool near_start)
{
  std::vector<XYPoint> pts;
  double ymin=0, ymax=0; bool first=true;
  for(std::map<int,XYPoint>::iterator p=m_swimmers.begin(); p!=m_swimmers.end(); p++){
    pts.push_back(p->second);
    double y=p->second.y();
    if(first){ ymin=ymax=y; first=false; } else { if(y<ymin)ymin=y; if(y>ymax)ymax=y; }
  }
  double y0=ymin;
  std::sort(pts.begin(), pts.end(),
    [&](const XYPoint&a, const XYPoint&b){
      int la=(int)floor((a.y()-y0)/lane_h), lb=(int)floor((b.y()-y0)/lane_h);
      if(la!=lb) return la<lb;
      return (la%2==0)?(a.x()<b.x()):(a.x()>b.x());
    });
  if(near_start && ((m_nav_y - ymin) > (ymax - m_nav_y)))
    std::reverse(pts.begin(), pts.end());   // start from the end nearest ownship
  XYSegList path;
  for(size_t i=0;i<pts.size();i++) path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}
void GenRescue::planSnkNear(){ snakeOrder(10.0, true);  }
void GenRescue::planSnkFine(){ snakeOrder( 6.0, false); }
void GenRescue::planSnkWide(){ snakeOrder(15.0, false); }

// Randomized snake: orientation (lane direction + x-direction) chosen ONCE per
// game so the boat's path is unpredictable -- a deterministic snake could be
// exploited by a smart opponent that anticipates where you'll be.
void GenRescue::planSnkRand()
{
  if(m_snk_dir < 0){ m_snk_dir = rand()%2; m_snk_xflip = rand()%2; }
  std::vector<XYPoint> pts; double ymin=0; bool first=true;
  for(std::map<int,XYPoint>::iterator p=m_swimmers.begin(); p!=m_swimmers.end(); p++){
    pts.push_back(p->second);
    if(first || p->second.y()<ymin){ ymin=p->second.y(); first=false; }
  }
  double y0=ymin, lane_h=10.0; int xf=m_snk_xflip;
  std::sort(pts.begin(), pts.end(),
    [&](const XYPoint&a, const XYPoint&b){
      int la=(int)floor((a.y()-y0)/lane_h), lb=(int)floor((b.y()-y0)/lane_h);
      if(la!=lb) return la<lb;
      bool asc = ((la%2==0) != (xf==1));   // random x-direction per lane
      return asc ? (a.x()<b.x()) : (a.x()>b.x());
    });
  if(m_snk_dir) std::reverse(pts.begin(), pts.end());   // random lane direction
  XYSegList path;
  for(size_t i=0;i<pts.size();i++) path.add_vertex(pts[i].x(), pts[i].y());
  postPath(path);
}

//---------------------------------------------------------
// Procedure: planAdapt() -- ROBUST across distributions AND dynamic reveal.
//   Detect clustering via mean nearest-neighbour distance (small => tight
//   clusters => greedy nearest; large => spread => snake sweep; clustered ~8-10m,
//   uniform ~16-20m, threshold 13m). CRITICAL: decide the mode ONCE and COMMIT --
//   under dynamic reveal the known set keeps changing, and re-deciding every plan
//   made the boat flip snake<->greedy mid-game (path churn, lost swimmers). We
//   wait until enough swimmers are known to trust the metric (default snake until
//   then), then lock the mode for the rest of the game.

void GenRescue::planAdapt()
{
  if(m_adapt_mode < 0) {
    std::vector<XYPoint> pts;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++)
      pts.push_back(p->second);
    if(pts.size() < 5) { planSnake(); return; }   // too few to judge -> default snake, don't commit
    double sum = 0;
    for(size_t i=0;i<pts.size();i++){
      double md=-1;
      for(size_t j=0;j<pts.size();j++) if(i!=j){
        double d=hypot(pts[i].x()-pts[j].x(), pts[i].y()-pts[j].y());
        if(md<0||d<md) md=d;
      }
      sum += md;
    }
    double meanNN = sum/pts.size();
    m_adapt_mode = (meanNN < 13.0) ? 1 : 0;        // 1=clustered(greedy), 0=spread(snake) -- LOCKED
  }
  if(m_adapt_mode == 1) planDev();
  else                  planSnake();
}

//---------------------------------------------------------
// Procedure: planHunt() -- ADVERSARY (reactive, drives BHV_Rescue via RESCUE_TGT)
//   Each tick, target the swimmer the OPPONENT is closest to that WE can still
//   reach first (us_d <= opp_d) -- i.e. snipe the opponent's imminent catch. If
//   nothing is stealable, grab our own nearest. Designed to beat predictable
//   collectors (like snake) by denying their next pickups.

void GenRescue::planHunt()
{
  if(!m_nav_x_set || !m_nav_y_set)
    return;
  if(m_swimmers.empty()) { Notify("RESCUE_TGT", ""); return; }

  int tgt = -1;
  if(m_opp_set) {
    double best_oppd = -1;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++){
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      double od = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      if(ud <= od) {                              // we can reach it before the opponent
        if(best_oppd < 0 || od < best_oppd) {     // the one the opponent is about to grab
          best_oppd = od; tgt = p->first;
        }
      }
    }
  }
  if(tgt < 0) {                                   // nothing stealable -> our nearest
    double bd = -1;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin(); p != m_swimmers.end(); p++){
      double ud = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      if(bd < 0 || ud < bd) { bd = ud; tgt = p->first; }
    }
  }
  XYPoint t = m_swimmers[tgt];
  Notify("RESCUE_TGT", doubleToStringX(t.x(),2) + "," + doubleToStringX(t.y(),2));
}

//---------------------------------------------------------
// Procedure: planChamp1()
//   FROZEN hall-of-fame opponent. This is the baseline winner (win-rate ~0.417
//   vs {random,greedy,snake}): opponent-aware claim with steal=8 + nearest-
//   neighbour tour. New dev candidates must beat THIS too, not just the static
//   reference panel. Do not edit -- it is a fixed benchmark.

void GenRescue::planChamp1()
{
  const double steal = 8.0;
  std::vector<XYPoint> claimed;
  for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
      p != m_swimmers.end(); p++) {
    double our_d = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
    bool mine = true;
    if(m_opp_set) {
      double opp_d = hypot(p->second.x()-m_opp_x, p->second.y()-m_opp_y);
      mine = (our_d <= opp_d + steal);
    }
    if(mine) claimed.push_back(p->second);
  }
  if(claimed.empty()) {
    double bestd = -1; XYPoint best;
    for(std::map<int,XYPoint>::iterator p = m_swimmers.begin();
        p != m_swimmers.end(); p++) {
      double d = hypot(p->second.x()-m_nav_x, p->second.y()-m_nav_y);
      if(bestd < 0 || d < bestd) { bestd = d; best = p->second; }
    }
    claimed.push_back(best);
  }
  double cx = m_nav_x, cy = m_nav_y;
  XYSegList path;
  while(!claimed.empty()) {
    size_t bi = 0; double bd = -1;
    for(size_t i = 0; i < claimed.size(); i++) {
      double d = hypot(claimed[i].x()-cx, claimed[i].y()-cy);
      if(bd < 0 || d < bd) { bd = d; bi = i; }
    }
    cx = claimed[bi].x(); cy = claimed[bi].y();
    path.add_vertex(cx, cy);
    claimed.erase(claimed.begin() + bi);
  }
  postPath(path);
}

//---------------------------------------------------------
// Procedure: postNullPath()
//   Purpose: If a found swimmer represents the last swimmer
//            to be found, then post a survey update essentially
// 

void GenRescue::postNullPath()
{
  // All known swimmers rescued: post a single-point "path" at ownship's
  // current position so the survey behaviour has nothing left to chase.
  if(!m_nav_x_set || !m_nav_y_set)
    return;

  XYSegList segl;
  segl.add_vertex(m_nav_x, m_nav_y);
  segl.set_label("one");
  Notify("VIEW_SEGLIST", segl.get_spec());

  string update_str = "points = " + segl.get_spec_pts();
  Notify("SURVEY_UPDATE", update_str);
  reportEvent("SURVEY_UPDATE=" + update_str + " (all rescued)");
}


//---------------------------------------------------------
// Procedure: buildReport()

bool GenRescue::buildReport()
{
  m_msgs << "Swimmers known (unrescued): " << m_swimmers.size() << endl;
  m_msgs << "Swimmers rescued:           " << m_rescued_count   << endl;
  m_msgs << "Planned path waypoints:     " << m_path.size()     << endl;
  m_msgs << "Re-plan pending:            " << (m_plan_pending ? "yes" : "no") << endl;
  m_msgs << "Opponent:                   "
         << (m_opp_set ? (m_opp_name + " @ " + doubleToStringX(m_opp_x,1) + "," +
                          doubleToStringX(m_opp_y,1)) : "unknown") << endl;
  return(true);
}
