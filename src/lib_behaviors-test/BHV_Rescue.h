/*****************************************************************/
/*    NAME: kiriakos (adversarial rescue)                        */
/*    ORGN: MIT 2.680 lab09/12 adversarial rescue                */
/*    FILE: BHV_Rescue.h                                          */
/*    DATE: 2026-06-22                                            */
/*                                                               */
/*  Reactive rescue executor. Reads a single target point from a */
/*  MOOS variable (default RESCUE_TGT, "x,y") published by        */
/*  pGenRescue each iteration, and drives course+speed toward it. */
/*  Because it holds no waypoint index, re-publishing the target  */
/*  never resets/restarts the boat -> no BHV_Waypoint thrash.     */
/*****************************************************************/

#ifndef BHV_RESCUE_HEADER
#define BHV_RESCUE_HEADER

#include <string>
#include "IvPBehavior.h"

class BHV_Rescue : public IvPBehavior {
public:
  BHV_Rescue(IvPDomain);
  ~BHV_Rescue() {};

  bool         setParam(std::string, std::string);
  IvPFunction* onRunState();

protected: // Configuration parameters
  double       m_desired_speed;
  std::string  m_tgt_var;

protected: // State variables
  double       m_osx;
  double       m_osy;
};

#ifdef WIN32
   #define IVP_EXPORT_FUNCTION __declspec(dllexport)
#else
   #define IVP_EXPORT_FUNCTION
#endif

extern "C" {
  IVP_EXPORT_FUNCTION IvPBehavior * createBehavior(std::string name, IvPDomain domain)
  {return new BHV_Rescue(domain);}
}
#endif
