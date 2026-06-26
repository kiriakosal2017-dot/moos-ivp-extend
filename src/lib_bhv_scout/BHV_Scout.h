/*****************************************************************/
/*    NAME: M.Benjamin,                                          */
/*    ORGN: Dept of Mechanical Eng / CSAIL, MIT Cambridge MA     */
/*    FILE: BHV_Scout.h                                          */
/*    DATE: April 30th 2022                                      */
/*                                                               */
/* This program is free software; you can redistribute it and/or */
/* modify it under the terms of the GNU General Public License   */
/* as published by the Free Software Foundation; either version  */
/* 2 of the License, or (at your option) any later version.      */
/*                                                               */
/* This program is distributed in the hope that it will be       */
/* useful, but WITHOUT ANY WARRANTY; without even the implied    */
/* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR       */
/* PURPOSE. See the GNU General Public License for more details. */
/*                                                               */
/* You should have received a copy of the GNU General Public     */
/* License along with this program; if not, write to the Free    */
/* Software Foundation, Inc., 59 Temple Place - Suite 330,       */
/* Boston, MA 02111-1307, USA.                                   */
/*****************************************************************/

#ifndef BHV_SCOUT_HEADER
#define BHV_SCOUT_HEADER

#include <string>
#include <map>
#include <vector>
#include <set>
#include "IvPBehavior.h"
#include "XYPoint.h"
#include "XYPolygon.h"

class BHV_Scout : public IvPBehavior {
public:
  BHV_Scout(IvPDomain);
  ~BHV_Scout() {};

  bool         setParam(std::string, std::string);
  void         onIdleState();
  IvPFunction* onRunState();
  void         onEveryState(std::string);

protected:
  IvPFunction* buildFunction();
  void         updateScoutPoint();
  void         generateLawnPath();
  bool         lawnAccept(double x, double y);
  void         postViewPoint(bool viewable=true);
  // Tile-coverage memory: skip lawn points the rescue has already swept (<=5m).
  long         tileKey(double x, double y);
  void         markRescueCovered(double rx, double ry);
  bool         tileCovered(double x, double y);

protected: // State variables
  double   m_osx;
  double   m_osy;
  double   m_curr_time;

  double   m_ptx;
  double   m_pty;
  bool     m_pt_set;

  XYPolygon m_rescue_region;

  // Registered swimmers learned from SWIMMER_ALERT, keyed by id (dedup).
  std::map<std::string, XYPoint> m_swimmers;

  // Scout points already reached - memory for systematic gap coverage.
  std::vector<XYPoint> m_visited;

  // Teammate (rescue) trail from NODE_REPORT - scout the complement of it.
  std::vector<XYPoint> m_mate_trail;

  // Precomputed lawnmower sweep over the swimmer-sparse complement region.
  std::vector<XYPoint> m_lawn_path;
  unsigned int         m_lawn_index;

  // Opponent rescue vehicle (from NODE_REPORT: TYPE=KAYAK, not us/teammate).
  // Used for win-region scouting: only scout where OUR rescue beats theirs.
  double m_opp_x;
  double m_opp_y;
  bool   m_opp_known;

  // Opponent SCOUT (the other heron). Tracked for shadow_mode: trail it so our
  // own sensor copies its discoveries. m_scout_shadowing is sticky once engaged.
  double m_opp_scout_x;
  double m_opp_scout_y;
  bool   m_opp_scout_known;
  bool   m_scout_shadowing;

  // Tile-coverage memory: tile keys the rescue has swept (came within 5m). When
  // tile_avoid is on, the scout skips lawn points in these tiles (no point
  // searching where the rescue already auto-rescued).
  std::set<long> m_covered_tiles;
  double         m_tile_size;

protected: // Config variables
  double m_capture_radius;
  double m_desired_speed;
  bool   m_shadow_mode;          // if true: trail the opponent scout instead of cover-all
  bool   m_tile_avoid;           // if true: skip lawn points the rescue already swept (<=5m)

  std::string m_tmate;
};

#define IVP_EXPORT_FUNCTION
extern "C" {
  IVP_EXPORT_FUNCTION IvPBehavior * createBehavior(std::string name, IvPDomain domain)
  {return new BHV_Scout(domain);}
}
#endif
