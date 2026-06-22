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

  // Speed objective: peak at desired speed.
  ZAIC_PEAK spd_zaic(m_domain, "speed");
  spd_zaic.setSummit(m_desired_speed);
  spd_zaic.setPeakWidth(0.5);
  spd_zaic.setBaseWidth(1.0);
  spd_zaic.setSummitDelta(0.8);
  if(spd_zaic.stateOK() == false) {
    postWMessage("BHV_Rescue speed ZAIC: " + spd_zaic.getWarnings());
    return(0);
  }

  // Course objective: peak at the bearing to the target.
  double ang = relAng(m_osx, m_osy, tgtx, tgty);
  ZAIC_PEAK crs_zaic(m_domain, "course");
  crs_zaic.setSummit(ang);
  crs_zaic.setPeakWidth(0);
  crs_zaic.setBaseWidth(180.0);
  crs_zaic.setSummitDelta(0);
  crs_zaic.setValueWrap(true);
  if(crs_zaic.stateOK() == false) {
    postWMessage("BHV_Rescue course ZAIC: " + crs_zaic.getWarnings());
    return(0);
  }

  IvPFunction *spd_ipf = spd_zaic.extractIvPFunction();
  IvPFunction *crs_ipf = crs_zaic.extractIvPFunction();

  OF_Coupler coupler;
  IvPFunction *ipf = coupler.couple(crs_ipf, spd_ipf, 50, 50);
  if(ipf)
    ipf->setPWT(m_priority_wt);
  return(ipf);
}
