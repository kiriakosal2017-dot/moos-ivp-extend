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
  bool handleMailNodeReport(std::string);
  void planPath();               // dispatch on m_strategy
  void planSnake();              // frozen R9 boustrophedon (was postShortestPath)
  void planGreedy();             // frozen nearest-neighbour tour
  void planRandom();             // frozen random-points tour
  void planDev();                // evolving brain (autoresearch edits THIS)
  void planDevB();               // PHASE 2: publishes RESCUE_TGT for the custom BHV_Rescue behaviour
  void planHunt();               // ADVERSARY: reactively steal the swimmer the opponent is going for
  void planAdapt();              // ROBUST: detect clustering (mean-NN dist) -> greedy(nn) if clustered, snake if spread
  void planChamp1();             // FROZEN hall-of-fame: baseline winner (NN tour + claim steal=8, wr~0.417)
  // --- TOURNAMENT contenders (frozen; compete round-robin against each other) ---
  // nn == planDev (aggressive nearest-neighbour collector, current champion)
  void planVor();                // opponent-aware: visit OUR-Voronoi swimmers first, then mop up
  void planVorx();               // vor with a generous claim margin (contest the middle)
  void planVori();               // vor + interception: within ours, grab the most-threatened first
  void planCen();                // centrality-weighted NN (bias toward remaining-centroid)
  void planAuc();                // auction/preemption: steal contested-winnable swimmers (bounded detour)
  void snakeOrder(double lane_h, bool near_start);  // shared boustrophedon builder
  void planSnkNear();            // snake that starts the sweep from the end nearest ownship
  void planSnkFine();            // snake with finer lanes (lane_h=6)
  void planSnkWide();            // snake with wider lanes (lane_h=15)
  void planSnkRand();            // snake with a RANDOM (per-game) sweep orientation -- unpredictable
  void postPath(const XYSegList& path);  // shared: VIEW_SEGLIST + SURVEY_UPDATE
  void postNullPath();

 private: // Config variables
  std::string m_vname;
  std::string m_strategy;        // "dev" | "random" | "greedy" | "snake" | "champ1"
  int         m_cur_target_id;   // dev: committed rescue target id (-1 = none)
  int         m_adapt_mode;      // adapt: locked mode (-1=unset, 0=snake, 1=greedy)
  int         m_snk_dir;         // snk_rand: chosen sweep direction (-1=unset, 0/1)
  int         m_snk_xflip;       // snk_rand: chosen x-direction flip (0/1)
  
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

  // Opponent position (tracked from NODE_REPORT)
  bool        m_opp_set;
  double      m_opp_x;
  double      m_opp_y;
  std::string m_opp_name;

  // Periodic re-plan throttle. We re-issue the survey path every
  // m_replan_interval game-seconds while swimmers remain so the
  // BHV_Waypoint list never runs out (which fired endflag RETURN=true and
  // parked the vehicle early). m_last_plan_time is the game-time of the
  // last SURVEY_UPDATE we posted.
  double                 m_last_plan_time;
  double                 m_replan_interval;
};

#endif 
