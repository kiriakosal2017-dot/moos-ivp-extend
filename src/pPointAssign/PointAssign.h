/************************************************************/
/*    NAME: Kiriakos Alexiou                                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: PointAssign.h                                          */
/*    DATE: December 29th, 1963                             */
/************************************************************/

#ifndef PointAssign_HEADER
#define PointAssign_HEADER

#include <vector>
#include <string>
#include <map>
#include <set>

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class PointAssign : public AppCastingMOOSApp
{
 public:
   PointAssign();
   ~PointAssign();

 protected: // Standard MOOSApp functions to overload  
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload 
   bool buildReport();

 protected:
   void registerVariables();
   void postViewPoint(double x, double y, std::string label, std::string color);

 private: // Configuration variables
  std::vector<std::string> m_vnames;
  bool m_assign_by_region;

 private: // State variables
  unsigned int                       m_points_total;
  std::map<std::string,unsigned int> m_vpoints;
  bool                               m_first_received;
  bool                               m_last_received;
  unsigned int                       m_next_index;
  bool                               m_script_started;   // έχουμε ξυπνήσει το timer script;
  std::set<std::string>              m_vehicles_seen;    // ποια vehicles έχουν εμφανιστεί
};

#endif 
