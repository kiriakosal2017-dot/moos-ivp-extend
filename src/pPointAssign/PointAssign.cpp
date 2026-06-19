/************************************************************/
/*    NAME: Kiriakos Alexiou                                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: PointAssign.cpp                                        */
/*    DATE: December 29th, 1963                             */
/************************************************************/

#include <iterator>
#include "MBUtils.h"
#include "ACTable.h"
#include "XYPoint.h"
#include "PointAssign.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

PointAssign::PointAssign()
{
  m_assign_by_region = false;
  m_points_total     = 0;
  m_first_received   = false;
  m_last_received    = false;
  m_next_index       = 0;
  m_script_started   = false;
}

//---------------------------------------------------------
// Destructor

PointAssign::~PointAssign()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool PointAssign::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

  if(key == "VISIT_POINT") {
    string point = msg.GetString();          // η τιμή: "firstpoint" / "lastpoint" / "x=..,y=..,id=.."

    // --- Περίπτωση 1: cue πληρότητας -> relay σε ΟΛΑ τα vehicles ---
    if((point == "firstpoint") || (point == "lastpoint")) {
      for(unsigned int i=0; i<m_vnames.size(); i++) {
        string moosvar = "VISIT_POINT_" + toupper(m_vnames[i]);
        Notify(moosvar, point);
      }
      if(point == "firstpoint")
        m_first_received = true;
      else
        m_last_received = true;
    }

    // --- Περίπτωση 2: κανονικό σημείο -> διάλεξε ΕΝΑ vehicle (εναλλάξ) ---
    else if(m_vnames.size() > 0) {
      // βγάλε x, y, id από το σημείο (ως αριθμούς / κείμενο)
      double x = 0, y = 0;
      string id;
      tokParse(point, "x",  ',', '=', x);
      tokParse(point, "y",  ',', '=', y);
      tokParse(point, "id", ',', '=', id);

      // διάλεξε vehicle
      string vehicle;
      if(m_assign_by_region) {
        // east-west: σύγκρινε το x με το μέσο της περιοχής (-25..200 -> 87.5)
        if(x < 87.5)
          vehicle = m_vnames[0];   // δυτικά
        else
          vehicle = m_vnames[1];   // ανατολικά
      }
      else {
        // alternating (round-robin)
        vehicle      = m_vnames[m_next_index];
        m_next_index = (m_next_index + 1) % m_vnames.size();
      }

      string moosvar = "VISIT_POINT_" + toupper(vehicle);      // φτιάξε το όνομα
      Notify(moosvar, point);                                  // στείλε ΜΟΝΟ σ' αυτόν

      m_points_total++;        // +1 στο συνολικό
      m_vpoints[vehicle]++;    // +1 στον μετρητή αυτού του vehicle

      // visual: χρώμα ανά vehicle, μοναδικό label = id
      string color = "dodger_blue";
      if(vehicle == m_vnames[0])
        color = "yellow";
      postViewPoint(x, y, id, color);
    }
  }
  else if(key == "NODE_REPORT") {
    // ένα vehicle εμφανίστηκε -> κράτα το όνομά του
    string report = msg.GetString();
    string vname;
    tokParse(report, "NAME", ',', '=', vname);
    if(vname != "")
      m_vehicles_seen.insert(vname);
  }
  else if(key != "APPCAST_REQ")
    reportRunWarning("Unhandled Mail: " + key);
  }

  return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool PointAssign::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool PointAssign::Iterate()
{
  AppCastingMOOSApp::Iterate();

  // Handshake: ξύπνα το timer script ΜΙΑ φορά, ΜΟΝΟ όταν έχουν εμφανιστεί
  // όλα τα vehicles (γέφυρες έτοιμες), ώστε να φτάσει το burst και σ' αυτά.
  if(!m_script_started && (m_vnames.size() > 0) &&
     (m_vehicles_seen.size() >= m_vnames.size())) {
    Notify("UTS_PAUSE", "false");
    m_script_started = true;
  }

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool PointAssign::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if(param == "vname") {
      m_vnames.push_back(value);
      handled = true;
    } 
    else if(param == "assign_by_region") {
      handled = setBooleanOnString(m_assign_by_region, value);
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);

  }
  
  registerVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void PointAssign::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
    Register("VISIT_POINT", 0);
    Register("NODE_REPORT", 0);
}

//------------------------------------------------------------
// Procedure: postViewPoint()
//   Ζωγραφίζει ένα σημείο στο pMarineViewer μέσω VIEW_POINT

void PointAssign::postViewPoint(double x, double y, string label, string color)
{
  XYPoint point(x, y);
  point.set_label(label);            // μοναδικό όνομα -> δεν σβήνεται από το επόμενο
  point.set_color("vertex", color);  // χρώμα κουκίδας
  point.set_param("vertex_size", "4");

  string spec = point.get_spec();    // string που καταλαβαίνει ο viewer
  Notify("VIEW_POINT", spec);
}


//------------------------------------------------------------
// Procedure: buildReport()

bool PointAssign::buildReport() 
{
  m_msgs << "============================================" << endl;
  m_msgs << "Vehicles configured: " << m_vnames.size() << endl;
  for(unsigned int i=0; i<m_vnames.size(); i++)
    m_msgs << "  - " << m_vnames[i] << endl;
  m_msgs << "assign_by_region: " << boolToString(m_assign_by_region) << endl;
  m_msgs << "Points received:  " << m_points_total << endl;

  m_msgs << "First received: " << boolToString(m_first_received) << endl;
  m_msgs << "Last received:  " << boolToString(m_last_received) << endl;
  for(unsigned int i=0; i<m_vnames.size(); i++) {
    string v = m_vnames[i];
    m_msgs << "  " << v << ": " << m_vpoints[v] << " points" << endl;
  }
  
  return(true);
}




