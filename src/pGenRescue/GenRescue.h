/************************************************************/
/*    NAME: Mike Benjamin                                   */
/*    ORGN: MIT                                             */
/*    FILE: GenRescue.h                                     */
/*    DATE: April 18th, 2022                                */
/************************************************************/

#ifndef P_GEN_RESCUE_HEADER
#define P_GEN_RESCUE_HEADER

#include <vector>
#include <string>
#include <map>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "XYPoint.h"
#include "XYPolygon.h"

class GenRescue : public AppCastingMOOSApp
{
 public:
   GenRescue();
   ~GenRescue() {};

 protected:
  bool OnNewMail(MOOSMSG_LIST &NewMail);
  bool Iterate();
  bool OnConnectToServer();
  bool OnStartUp();
  bool buildReport();
  void RegisterVariables();
  
 protected:
  bool handleMailNewSwimmer(std::string);
  bool handleMailFoundSwimmer(std::string);
  bool handleMailRescueRegion(std::string);
  void postShortestPath();
  void postNullPath();

 private: // Config variables
  std::string m_vname;
  
 private: // State variables
  XYSegList  m_path;
  double     m_nav_x;
  double     m_nav_y;
  bool       m_nav_x_set;
  bool       m_nav_y_set;

  // Swimmer-aware state: real swimmer positions keyed by id.
  // Re-broadcast SWIMMER_ALERTs overwrite the same key (auto-dedupe);
  // a RESCUED/FOUND swimmer is erased. m_plan_pending forces a re-plan
  // whenever this set changes.
  std::map<int, XYPoint> m_swimmers;
  bool                   m_plan_pending;
  unsigned int           m_rescued_count;

  // Periodic re-plan throttle. We re-issue the survey path every
  // m_replan_interval game-seconds while swimmers remain so the
  // BHV_Waypoint list never runs out (which fired endflag RETURN=true and
  // parked the vehicle early). m_last_plan_time is the game-time of the
  // last SURVEY_UPDATE we posted.
  double                 m_last_plan_time;
  double                 m_replan_interval;
};

#endif 
