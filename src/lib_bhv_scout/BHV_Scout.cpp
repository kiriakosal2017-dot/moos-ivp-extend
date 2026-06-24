/*****************************************************************/
/*    NAME: M.Benjamin                                           */
/*    ORGN: Dept of Mechanical Eng / CSAIL, MIT Cambridge MA     */
/*    FILE: BHV_Scout.cpp                                        */
/*    DATE: April 30th 2022                                      */
/*****************************************************************/

#include <cstdlib>
#include <math.h>
#include <vector>
#include "BHV_Scout.h"
#include "MBUtils.h"
#include "AngleUtils.h"
#include "BuildUtils.h"
#include "GeomUtils.h"
#include "ZAIC_PEAK.h"
#include "OF_Coupler.h"
#include "XYFormatUtilsPoly.h"

using namespace std;

//-----------------------------------------------------------
// Constructor()

BHV_Scout::BHV_Scout(IvPDomain gdomain) :
  IvPBehavior(gdomain)
{
  IvPBehavior::setParam("name", "scout");

  // Default values for behavior state variables
  m_osx  = 0;
  m_osy  = 0;

  // All distances are in meters, all speed in meters per second
  // Default values for configuration parameters
  m_desired_speed  = 1;
  m_capture_radius = 10;

  m_pt_set = false;

  addInfoVars("NAV_X, NAV_Y");
  addInfoVars("RESCUE_REGION");
  addInfoVars("SCOUTED_SWIMMER");
  addInfoVars("SWIMMER_ALERT");
  addInfoVars("NODE_REPORT");
}

//---------------------------------------------------------------
// Procedure: setParam() - handle behavior configuration parameters

bool BHV_Scout::setParam(string param, string val)
{
  // Convert the parameter to lower case for more general matching
  param = tolower(param);

  bool handled = true;
  if(param == "capture_radius")
    handled = setPosDoubleOnString(m_capture_radius, val);
  else if(param == "desired_speed")
    handled = setPosDoubleOnString(m_desired_speed, val);
  else if(param == "tmate")
    handled = setNonWhiteVarOnString(m_tmate, val);
  else
    handled = false;

  srand(time(NULL));

  return(handled);
}

//-----------------------------------------------------------
// Procedure: onEveryState()

void BHV_Scout::onEveryState(string str)
{
  // Collect ALL registered swimmers from this cycle's SWIMMER_ALERT burst.
  // uFldRescueMgr posts ~11 of them at once every 15s; getBufferStringVector
  // returns the full vector (getBufferStringVal would give only the last one).
  bool ok_vec;
  vector<string> alerts = getBufferStringVector("SWIMMER_ALERT", ok_vec);
  for(unsigned int i=0; i<alerts.size(); i++) {
    double x, y;
    string id;
    bool ok_x  = tokParse(alerts[i], "x",  ',', '=', x);
    bool ok_y  = tokParse(alerts[i], "y",  ',', '=', y);
    bool ok_id = tokParse(alerts[i], "id", ',', '=', id);
    if(ok_x && ok_y && ok_id) {
      XYPoint pt(x, y);
      pt.set_label(id);
      m_swimmers[id] = pt;  // keyed by id => repeats overwrite, no growth
    }
  }
  if(alerts.size() > 0)
    postEventMessage("Swimmers known: " + uintToString(m_swimmers.size()));

  // Track the teammate (rescue) vehicle from shared NODE_REPORT messages,
  // so we can scout AWAY from where it goes (it rescues swimmers it passes).
  bool ok_nr;
  vector<string> reports = getBufferStringVector("NODE_REPORT", ok_nr);
  for(unsigned int i=0; i<reports.size(); i++) {
    string name;
    if(!tokParse(reports[i], "NAME", ',', '=', name) || (name != m_tmate))
      continue;
    double mx, my;
    bool okx = tokParse(reports[i], "X", ',', '=', mx);
    bool oky = tokParse(reports[i], "Y", ',', '=', my);
    if(okx && oky) {
      // Sub-sample into a trail: only record once the mate has moved ~8m,
      // so the trail stays compact over a long mission.
      if(m_mate_trail.empty() ||
         hypot(mx - m_mate_trail.back().get_vx(),
               my - m_mate_trail.back().get_vy()) > 8.0)
        m_mate_trail.push_back(XYPoint(mx, my));
    }
  }

  if(!getBufferVarUpdated("SCOUTED_SWIMMER"))
    return;

  string report = getBufferStringVal("SCOUTED_SWIMMER");
  if(report == "")
    return;

  if(m_tmate == "") {
    postWMessage("Mandatory Teammate name is null");
    return;
  }
  postOffboardMessage(m_tmate, "SWIMMER_ALERT", report);
}

//-----------------------------------------------------------
// Procedure: onIdleState()

void BHV_Scout::onIdleState()
{
  m_curr_time = getBufferCurrTime();
}

//-----------------------------------------------------------
// Procedure: onRunState()

IvPFunction *BHV_Scout::onRunState()
{
  // Part 1: Get vehicle position from InfoBuffer and post a
  // warning if problem is encountered
  bool ok1, ok2;
  m_osx = getBufferDoubleVal("NAV_X", ok1);
  m_osy = getBufferDoubleVal("NAV_Y", ok2);
  if(!ok1 || !ok2) {
    postWMessage("No ownship X/Y info in info_buffer.");
    return(0);
  }

  // Part 2: Determine if the vehicle has reached the destination
  // point and if so, declare completion.
  updateScoutPoint();
  double dist = hypot((m_ptx-m_osx), (m_pty-m_osy));
  //postEventMessage("Dist=" + doubleToStringX(dist,1));
  if(dist <= m_capture_radius) {
    m_visited.push_back(XYPoint(m_ptx, m_pty));  // remember this area is covered
    m_pt_set = false;
    postViewPoint(false);
    return(0);
  }

  // Part 3: Post the waypoint as a string for consumption by
  // a viewer application.
  postViewPoint(true);

  // Part 4: Build the IvP function
  IvPFunction *ipf = buildFunction();
  if(ipf == 0)
    postWMessage("Problem Creating the IvP Function");

  return(ipf);
}

//-----------------------------------------------------------
// Procedure: updateScoutPoint()

void BHV_Scout::updateScoutPoint()
{
  if(m_pt_set)
    return;

  string region_str = getBufferStringVal("RESCUE_REGION");
  if(region_str == "")
    postWMessage("Unknown RESCUE_REGION");
  else
    postRetractWMessage("Unknown RESCUE_REGION");

  XYPolygon region = string2Poly(region_str);
  if(!region.is_convex()) {
    postWMessage("Badly formed RESCUE_REGION");
    return;
  }
  m_rescue_region = region;

  double best_x = 0;
  double best_y = 0;

  // If we don't know any swimmers yet, fall back to a plain random point
  // (this only happens in the first second, before the first alert burst).
  if(m_swimmers.empty()) {
    bool ok = randPointInPoly(m_rescue_region, best_x, best_y);
    if(!ok) {
      postWMessage("Unable to generate scout point");
      return;
    }
  }
  else {
    // Strategy (b): throw N random candidate points, keep the best one.
    // score = (distance to nearest known swimmer) - w_near*(distance from me)
    //   - high "distance to nearest swimmer" => deep in an empty gap, where
    //     the rescue vehicle won't naturally go (so unregistered swimmers
    //     hide there).
    //   - subtracting w_near*(distance from me) discourages flying across the
    //     whole map for a marginally emptier spot.
    unsigned int num_candidates = 20;
    double       w_near         = 0.5;
    double       w_explore      = 0.5;
    double       w_mate         = 1.0;
    double       best_score     = -1e9;

    for(unsigned int c=0; c<num_candidates; c++) {
      double cx = 0, cy = 0;
      if(!randPointInPoly(m_rescue_region, cx, cy))
        continue;

      // distance from this candidate to the nearest known swimmer
      double nearest = 1e9;
      map<string, XYPoint>::iterator p;
      for(p=m_swimmers.begin(); p!=m_swimmers.end(); p++) {
        double d = hypot(cx - p->second.get_vx(), cy - p->second.get_vy());
        if(d < nearest)
          nearest = d;
      }

      // distance from this candidate to the nearest place we've already
      // covered (big => unexplored area). 0 if we've been nowhere yet.
      double nearest_visited = 1e9;
      for(unsigned int v=0; v<m_visited.size(); v++) {
        double dv = hypot(cx - m_visited[v].get_vx(), cy - m_visited[v].get_vy());
        if(dv < nearest_visited)
          nearest_visited = dv;
      }
      double explore = m_visited.empty() ? 0.0 : nearest_visited;

      // distance from this candidate to the teammate's (rescue's) trail.
      // Big => the rescue hasn't been there, so it's ours to cover.
      double nearest_mate = 1e9;
      for(unsigned int m=0; m<m_mate_trail.size(); m++) {
        double dm = hypot(cx - m_mate_trail[m].get_vx(), cy - m_mate_trail[m].get_vy());
        if(dm < nearest_mate)
          nearest_mate = dm;
      }
      double mate_term = m_mate_trail.empty() ? 0.0 : nearest_mate;

      double dist_from_me = hypot(cx - m_osx, cy - m_osy);
      double score = nearest
                   - (w_near    * dist_from_me)
                   + (w_explore * explore)
                   + (w_mate    * mate_term);

      if(score > best_score) {
        best_score = score;
        best_x = cx;
        best_y = cy;
      }
    }
  }

  m_ptx = best_x;
  m_pty = best_y;
  m_pt_set = true;
  string msg = "New pt: " + doubleToStringX(best_x) + "," + doubleToStringX(best_y);
  postEventMessage(msg);
}

//-----------------------------------------------------------
// Procedure: postViewPoint()

void BHV_Scout::postViewPoint(bool viewable)
{

  XYPoint pt(m_ptx, m_pty);
  pt.set_vertex_size(5);
  pt.set_vertex_color("orange");
  pt.set_label(m_us_name + "'s next waypoint");

  string point_spec;
  if(viewable)
    point_spec = pt.get_spec("active=true");
  else
    point_spec = pt.get_spec("active=false");
  postMessage("VIEW_POINT", point_spec);
}


//-----------------------------------------------------------
// Procedure: buildFunction()

IvPFunction *BHV_Scout::buildFunction()
{
  if(!m_pt_set)
    return(0);

  ZAIC_PEAK spd_zaic(m_domain, "speed");
  spd_zaic.setSummit(m_desired_speed);
  spd_zaic.setPeakWidth(0.5);
  spd_zaic.setBaseWidth(1.0);
  spd_zaic.setSummitDelta(0.8);
  if(spd_zaic.stateOK() == false) {
    string warnings = "Speed ZAIC problems " + spd_zaic.getWarnings();
    postWMessage(warnings);
    return(0);
  }

  double rel_ang_to_wpt = relAng(m_osx, m_osy, m_ptx, m_pty);
  ZAIC_PEAK crs_zaic(m_domain, "course");
  crs_zaic.setSummit(rel_ang_to_wpt);
  crs_zaic.setPeakWidth(0);
  crs_zaic.setBaseWidth(180.0);
  crs_zaic.setSummitDelta(0);
  crs_zaic.setValueWrap(true);
  if(crs_zaic.stateOK() == false) {
    string warnings = "Course ZAIC problems " + crs_zaic.getWarnings();
    postWMessage(warnings);
    return(0);
  }

  IvPFunction *spd_ipf = spd_zaic.extractIvPFunction();
  IvPFunction *crs_ipf = crs_zaic.extractIvPFunction();

  OF_Coupler coupler;
  IvPFunction *ivp_function = coupler.couple(crs_ipf, spd_ipf, 50, 50);

  return(ivp_function);
}
