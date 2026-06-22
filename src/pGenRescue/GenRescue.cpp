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
  m_strategy = "snake";   // default; harness injects per-vehicle via targ patch

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
  if(have_nav && m_plan_pending)
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
    // NOTE: deliberately do NOT re-plan here. Re-posting SURVEY_UPDATE resets
    // BHV_Waypoint's index to vertex 0 (the bottom lane), so re-planning on
    // every rescue kept yanking the boat back to the bottom -- it yo-yo'd in
    // the lower lanes and never reached the top-edge swimmers (all 5 misses on
    // athens_02 had y in [-10,-2], the LAST lane). One uninterrupted snake from
    // bottom to top fits easily in the time budget (~500m path vs 1.2 m/s x
    // ~1140s). Keeping the erase (above) lets m_swimmers empty out when all are
    // rescued, which releases the RETURN guard so the boat heads home cleanly.
  }
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
  if(m_strategy == "dev")         planDev();
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
  Notify("SURVEY_UPDATE", "points = " + m_path.get_spec_pts());
  reportEvent("SURVEY_UPDATE points=" + m_path.get_spec_pts());
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

void GenRescue::planDev()    { planSnake(); }  // replaced in Task 4

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
  return(true);
}
