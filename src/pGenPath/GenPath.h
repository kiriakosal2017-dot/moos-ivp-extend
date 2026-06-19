/************************************************************/
/*    NAME: Kiriakos Alexiou                                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: GenPath.h                                          */
/*    DATE: December 29th, 1963                             */
/************************************************************/

#ifndef GenPath_HEADER
#define GenPath_HEADER

#include <vector>
#include <string>
#include "XYPoint.h"
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class GenPath : public AppCastingMOOSApp
{
 public:
   GenPath();
   ~GenPath();

 protected: // Standard MOOSApp functions to overload  
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload 
   bool buildReport();

 protected:
   void registerVariables();
   void generatePath();

 private: // Configuration variables
  std::string          m_update_var;      // η MOOS μεταβλητή που ενημερώνει το waypoint behavior
  double               m_visit_radius;    // πόσο κοντά = "επισκέφθηκε" (m)

 private: // State variables
  std::vector<XYPoint> m_points;          // τα σημεία που μαζέψαμε
  std::vector<bool>    m_visited;         // ποιά έχουν επισκεφθεί (παράλληλο στο m_points)
  unsigned int         m_invalid_points;  // σημεία που δεν διαβάστηκαν σωστά
  bool                 m_path_generated;  // φτιάξαμε ήδη τη διαδρομή;
  bool                 m_first_received;
  bool                 m_last_received;
  double               m_nav_x;           // τρέχουσα θέση του καραβιού
  double               m_nav_y;
  bool                 m_nav_received;
};

#endif 
