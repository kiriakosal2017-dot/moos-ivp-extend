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
  m_desired_speed  = 1.1;   // fixed: we tune the algorithm, not the speed
  m_capture_radius = 10;

  m_pt_set = false;
  m_lawn_index = 0;

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

  // Lawnmower coverage: follow a precomputed serpentine that sweeps the
  // region but SKIPS the bands near registered swimmers (the rescue vehicle
  // sweeps those itself). So we systematically cover the complement.
  if(m_lawn_path.empty() && !m_swimmers.empty())
    generateLawnPath();

  if(m_lawn_path.empty()) {
    // Path not ready yet (swimmers not known); use a random point meanwhile.
    double rx = 0, ry = 0;
    if(!randPointInPoly(m_rescue_region, rx, ry)) {
      postWMessage("Unable to generate scout point");
      return;
    }
    m_ptx = rx;
    m_pty = ry;
    m_pt_set = true;
    return;
  }

  // COOPERATION: the rescue rescues hidden swimmers it passes over, so any
  // lawn point near its actual trail is already handled - skip it and move on
  // to a point the rescue has NOT covered. (Real-time, via NODE_REPORT.)
  double mate_clear = 12.0;
  unsigned int checked = 0;
  while(checked < m_lawn_path.size()) {
    if(m_lawn_index >= m_lawn_path.size())
      m_lawn_index = 0;          // wrap around and re-sweep
    double lx = m_lawn_path[m_lawn_index].get_vx();
    double ly = m_lawn_path[m_lawn_index].get_vy();

    bool covered = false;
    for(unsigned int t=0; t<m_mate_trail.size(); t++) {
      if(hypot(lx - m_mate_trail[t].get_vx(),
               ly - m_mate_trail[t].get_vy()) < mate_clear) {
        covered = true;
        break;
      }
    }
    if(!covered)
      break;                     // this point still needs us
    m_lawn_index++;              // rescue already swept here - skip ahead
    checked++;
  }

  if(m_lawn_index >= m_lawn_path.size())
    m_lawn_index = 0;

  m_ptx = m_lawn_path[m_lawn_index].get_vx();
  m_pty = m_lawn_path[m_lawn_index].get_vy();
  m_lawn_index++;
  m_pt_set = true;
  postEventMessage("Lawn " + uintToString(m_lawn_index) + "/" +
                   uintToString((unsigned int)m_lawn_path.size()));
}

//-----------------------------------------------------------
// Procedure: lawnAccept() - keep a lawnmower point only if it is inside the
//            region AND not within "clearance" of a registered swimmer
//            (those areas the rescue vehicle covers on its own).

bool BHV_Scout::lawnAccept(double x, double y)
{
  // Keep every point inside the region. We cover the whole area and let the
  // real-time abe-trail skip (in updateScoutPoint) handle what the rescue
  // actually sweeps - a far more accurate filter than a static swimmer radius.
  return m_rescue_region.contains(x, y);
}

//-----------------------------------------------------------
// Procedure: generateLawnPath() - build a boustrophedon (back-and-forth)
//            sweep over the region's bounding box, clipped to the complement
//            of the registered-swimmer bands.

void BHV_Scout::generateLawnPath()
{
  m_lawn_path.clear();
  m_lawn_index = 0;

  double minx = m_rescue_region.get_min_x();
  double maxx = m_rescue_region.get_max_x();
  double miny = m_rescue_region.get_min_y();
  double maxy = m_rescue_region.get_max_y();

  // Lane spacing MUST match the sensor swath (~6m), else we miss swimmers
  // between lanes. 8m lanes give near-full coverage within the time budget.
  double lane_gap = 8.0;   // spacing between sweep rows (m)
  double step     = 10.0;  // sampling distance along each row (m)

  int row = 0;
  for(double y = miny + (lane_gap/2.0); y <= maxy; y += lane_gap) {
    if((row % 2) == 0) {
      for(double x = minx; x <= maxx; x += step)
        if(lawnAccept(x, y))
          m_lawn_path.push_back(XYPoint(x, y));
    }
    else {
      for(double x = maxx; x >= minx; x -= step)
        if(lawnAccept(x, y))
          m_lawn_path.push_back(XYPoint(x, y));
    }
    row++;
  }

  // Start at the lawn point nearest the scout, to avoid a long transit to a
  // far corner before any useful sweeping begins.
  double bestd = 1e9;
  for(unsigned int i=0; i<m_lawn_path.size(); i++) {
    double d = hypot(m_lawn_path[i].get_vx() - m_osx,
                     m_lawn_path[i].get_vy() - m_osy);
    if(d < bestd) {
      bestd = d;
      m_lawn_index = i;
    }
  }

  postEventMessage("Lawn path built: " +
                   uintToString((unsigned int)m_lawn_path.size()) + " pts");
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
