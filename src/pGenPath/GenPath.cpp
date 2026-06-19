/************************************************************/
/*    NAME: Kiriakos Alexiou                                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: GenPath.cpp                                        */
/*    DATE: December 29th, 1963                             */
/************************************************************/

#include <iterator>
#include "MBUtils.h"
#include "ACTable.h"
#include "XYSegList.h"
#include "GenPath.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

GenPath::GenPath()
{
  m_first_received = false;
  m_last_received  = false;
  m_nav_x          = 0;
  m_nav_y          = 0;
  m_nav_received   = false;
  m_update_var     = "WPT_UPDATE";
  m_visit_radius   = 3;
  m_invalid_points = 0;
  m_path_generated = false;
}

//---------------------------------------------------------
// Destructor

GenPath::~GenPath()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool GenPath::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

    if(key == "VISIT_POINT") {
      string point = msg.GetString();
      if(point == "firstpoint")
        m_first_received = true;
      else if(point == "lastpoint")
        m_last_received = true;
      else {
        // κανονικό σημείο -> βγάλε x,y· αν διαβαστεί σωστά, βάλ' το στη λίστα
        double x = 0, y = 0;
        bool ok_x = tokParse(point, "x", ',', '=', x);
        bool ok_y = tokParse(point, "y", ',', '=', y);
        if(ok_x && ok_y) {
          m_points.push_back(XYPoint(x, y));
          m_visited.push_back(false);      // ξεκινά μη-επισκεφθέν
        }
        else
          m_invalid_points++;
      }
    }
    else if(key == "NAV_X") {
      m_nav_x        = msg.GetDouble();
      m_nav_received = true;
    }
    else if(key == "NAV_Y")
      m_nav_y = msg.GetDouble();

    else if(key != "APPCAST_REQ")
      reportRunWarning("Unhandled Mail: " + key);
   }
	
   return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool GenPath::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool GenPath::Iterate()
{
  AppCastingMOOSApp::Iterate();

  // Όταν έχουμε ΟΛΑ τα σημεία + ξέρουμε τη θέση μας, φτιάξε διαδρομή (μία φορά)
  if(!m_path_generated && m_last_received && m_nav_received && (m_points.size() > 0))
    generatePath();

  // Σημείωσε ως "visited" όποιο σημείο είναι εντός visit_radius από το καράβι
  if(m_nav_received) {
    for(unsigned int i=0; i<m_points.size(); i++) {
      if(!m_visited[i]) {
        double dx = m_points[i].x() - m_nav_x;
        double dy = m_points[i].y() - m_nav_y;
        if((dx*dx + dy*dy) <= (m_visit_radius * m_visit_radius))
          m_visited[i] = true;
      }
    }
  }

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool GenPath::OnStartUp()
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
    if(param == "update_var") {
      m_update_var = value;
      handled = true;
    }
    else if(param == "visit_radius") {
      m_visit_radius = atof(value.c_str());
      handled = true;
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);

  }
  
  registerVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void GenPath::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("VISIT_POINT", 0);
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
}

//------------------------------------------------------------
// Procedure: generatePath()
//   Greedy nearest-neighbor από την τρέχουσα θέση του καραβιού.
//   Φτιάχνει μια XYSegList και την στέλνει στο waypoint behavior.

void GenPath::generatePath()
{
  XYSegList seglist;

  // αντίγραφο των σημείων που θα "καταναλώνουμε" καθώς τα επιλέγουμε
  vector<XYPoint> remaining = m_points;

  // το "τρέχον" σημείο ξεκινά από τη θέση του καραβιού
  double cx = m_nav_x;
  double cy = m_nav_y;

  while(remaining.size() > 0) {
    // βρες το πλησιέστερο σημείο στο (cx,cy)
    unsigned int best_i    = 0;
    double       best_dist = -1;
    for(unsigned int i=0; i<remaining.size(); i++) {
      double dx   = remaining[i].x() - cx;
      double dy   = remaining[i].y() - cy;
      double dist = (dx*dx) + (dy*dy);     // τετράγωνο απόστασης (αρκεί για σύγκριση)
      if((best_dist < 0) || (dist < best_dist)) {
        best_dist = dist;
        best_i    = i;
      }
    }

    // πρόσθεσέ το στη διαδρομή, κάν' το νέο "τρέχον", βγάλ' το από τα remaining
    XYPoint chosen = remaining[best_i];
    seglist.add_vertex(chosen.x(), chosen.y());
    cx = chosen.x();
    cy = chosen.y();
    remaining.erase(remaining.begin() + best_i);
  }

  // στείλε τη διαδρομή στο waypoint behavior
  string update = "points=" + seglist.get_spec();
  Notify(m_update_var, update);
  m_path_generated = true;
}


//------------------------------------------------------------
// Procedure: buildReport()

bool GenPath::buildReport() 
{
  // μέτρα πόσα έχουν επισκεφθεί
  unsigned int visited = 0;
  for(unsigned int i=0; i<m_visited.size(); i++)
    if(m_visited[i])
      visited++;

  m_msgs << "Visit Radius:            " << m_visit_radius << endl;
  m_msgs << "Total Points Received:   " << m_points.size() << endl;
  m_msgs << "Invalid Points Received: " << m_invalid_points << endl;
  m_msgs << "First Point Received:    " << boolToString(m_first_received) << endl;
  m_msgs << "Last Point Received:     " << boolToString(m_last_received) << endl;
  m_msgs << "NAV_X/Y Received:        " << boolToString(m_nav_received) << endl;
  m_msgs << "Path Generated:          " << boolToString(m_path_generated) << endl;
  m_msgs << endl;
  m_msgs << "Tour Status" << endl;
  m_msgs << "------------------------" << endl;
  m_msgs << "   Points Visited:       " << visited << endl;
  m_msgs << "   Points Unvisited:     " << (m_points.size() - visited) << endl;

  return(true);
}




