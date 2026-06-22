/*****************************************************************/
/*    NAME: kiriakos (adversarial rescue)                        */
/*    ORGN: MIT 2.680 lab09/12 adversarial rescue                */
/*    FILE: BHV_Rescue.cpp                                        */
/*    DATE: 2026-06-22                                            */
/*****************************************************************/

#include <cstdlib>
#include <math.h>
#include "BHV_Rescue.h"
#include "MBUtils.h"
#include "AngleUtils.h"
#include "BuildUtils.h"
#include "ZAIC_PEAK.h"
#include "OF_Coupler.h"
#include "OF_Reflector.h"
#include "AOF_SimpleWaypoint.h"

using namespace std;

//-----------------------------------------------------------
// Constructor

BHV_Rescue::BHV_Rescue(IvPDomain gdomain) : IvPBehavior(gdomain)
{
  IvPBehavior::setParam("name", "rescue");
  m_domain = subDomain(m_domain, "course,speed");

  m_desired_speed = 2.0;     // m/s; overridable via .bhv "speed"
  m_tgt_var       = "RESCUE_TGT";
  m_osx = 0;
  m_osy = 0;

  addInfoVars("NAV_X, NAV_Y");
  addInfoVars(m_tgt_var);
}

//-----------------------------------------------------------
// setParam

bool BHV_Rescue::setParam(string param, string val)
{
  param = tolower(param);
  double dval = atof(val.c_str());
  if((param == "speed") && isNumber(val) && (dval > 0)) {
    m_desired_speed = dval;
    return(true);
  }
  if((param == "target_var") && (val != "")) {
    m_tgt_var = toupper(val);
    addInfoVars(m_tgt_var);
    return(true);
  }
  return(false);
}

//-----------------------------------------------------------
// onRunState: head course+speed toward the current target point.

IvPFunction *BHV_Rescue::onRunState()
{
  bool ok1, ok2;
  m_osx = getBufferDoubleVal("NAV_X", ok1);
  m_osy = getBufferDoubleVal("NAV_Y", ok2);
  if(!ok1 || !ok2) {
    postWMessage("BHV_Rescue: no ownship NAV_X/NAV_Y in info_buffer.");
    return(0);
  }

  // Target is published by pGenRescue as "x,y". No target -> no objective
  // (helm idles / lets a lower-priority behaviour act). Holding no waypoint
  // index means each refresh just redirects smoothly -- never a restart.
  bool ok3;
  string tgt = getBufferStringVal(m_tgt_var, ok3);
  if(!ok3 || (tgt == ""))
    return(0);

  string sx = biteString(tgt, ',');   // tgt now holds the y token
  if((sx == "") || (tgt == ""))
    return(0);
  double tgtx = atof(sx.c_str());
  double tgty = atof(tgt.c_str());

  // Build a RICH coupled course+speed objective via the Reflector over an
  // AOF_SimpleWaypoint (toward the target). This produces a fine 600x500 IvP
  // function -- like BHV_Waypoint -- so it actually competes in the helm against
  // the collision-avoidance (pwt 350) and op-region (pwt 300) behaviours. A
  // coarse ZAIC-only function got out-voted and the boat sat still.
  AOF_SimpleWaypoint aof(m_domain);
  bool ok = true;
  ok = ok && aof.setParam("desired_speed", m_desired_speed);
  ok = ok && aof.setParam("osx", m_osx);
  ok = ok && aof.setParam("osy", m_osy);
  ok = ok && aof.setParam("ptx", tgtx);
  ok = ok && aof.setParam("pty", tgty);
  ok = ok && aof.initialize();
  if(!ok) {
    postWMessage("BHV_Rescue: AOF_SimpleWaypoint init failed.");
    return(0);
  }
  OF_Reflector reflector(&aof);
  reflector.create(600, 500);
  IvPFunction *ipf = reflector.extractIvPFunction();
  if(ipf)
    ipf->setPWT(m_priority_wt);
  return(ipf);
}
