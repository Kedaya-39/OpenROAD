// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "db/obj/frMarker.h"
#include "frBaseTypes.h"

namespace odb {
class Point;
class Rect;
class dbTechLayer;
}  // namespace odb

namespace drt {

struct RouterConfiguration
{
  std::string DBPROCESSNODE;
  std::string OUT_MAZE_FILE;
  std::string DRC_RPT_FILE;
  std::optional<int> DRC_RPT_ITER_STEP = std::nullopt;
  std::string CMAP_FILE;
  std::string GUIDE_REPORT_FILE;

  // to be removed
  int OR_SEED = -1;
  double OR_K = 0;

  int MAX_THREADS = 1;
  int BATCHSIZE = 1024;
  int BATCHSIZETA = 8;
  int MTSAFEDIST = 2000;
  int DRCSAFEDIST = 500;
  int VERBOSE = 1;
  int BOTTOM_ROUTING_LAYER = 2;
  int TOP_ROUTING_LAYER = std::numeric_limits<int>::max();
  bool ALLOW_PIN_AS_FEEDTHROUGH = true;
  bool USENONPREFTRACKS = true;
  bool USEMINSPACING_OBS = true;
  bool ENABLE_BOUNDARY_MAR_FIX = true;
  bool ENABLE_VIA_GEN = true;
  bool CLEAN_PATCHES = false;
  bool DO_PA = true;
  bool DO_PAE = false;
  bool DO_PAE_ENHANCE = false;
  bool PAE_ENABLE_NB_CACHE = false;
  bool SINGLE_STEP_DR = false;
  bool SAVE_GUIDE_UPDATES = false;
  int PAE_HASH_SEED = 0;  // Hash seed for PAEPatternKey
  std::string PAE_REPORT_FILE;
  std::string PAE_PARA_FILE;

  // weights and constants for PAE (Pin Access Evaluation)
  double PAE_W1 = 0.1;   // I1: Track Alignment
  double PAE_W2 = 0.1;   // I2: Access Directions
  double PAE_W3 = 0.1;   // I3: Spatial Sparsity
  double PAE_W4 = 0;     // I4: Track Occupation (reserved)
  double PAE_W5 = 0.1;   // I5: Pattern Capacity
  double PAE_W6 = 0.1;  // I6: Pattern Diversity
  double PAE_W7 = 0.5;   // I7: Rip-up Frequency (Dynamic)

  int PAE_N_TH = 10;  // Capacity threshold

  // I1 scoring constants
  int PAE_I1_S11 = 0;   // OnGrid
  int PAE_I1_S12 = 1;   // HalfGrid
  int PAE_I1_S13 = 2;   // Center
  int PAE_I1_S14 = 5;   // EncOpt
  int PAE_I1_S15 = 10;  // Others
  int PAE_I1_S16 = 4;   // Upper layer multiplier

  // I2 scoring constants
  int PAE_I2_S21 = 8;  // Base score
  int PAE_I2_S22 = 4;  // Via access deduction
  int PAE_I2_S23 = 1;  // Planar direction deduction

  // I3 scoring constants
  int PAE_I3_S31 = 1000;  // Variance constant

  // I6 scoring constants
  double PAE_I6_S61 = 0.5;  // Coverage weight
  double PAE_I6_S62 = 0.5;  // Jaccard distance weight

  // I7 scoring constants
  double PAE_I7_S71 = 1.5;     // Rip-up penalty factor
  double PAE_I7_S72 = 1.0;     // Selection bonus factor
  double PAE_I7_S73 = -0.001;  // Sigmoid slope
  double PAE_I7_S74 = 0.5;     // Neighbor rip-up penalty factor
  double PAE_I7_S75 = 10;       // Neighbor search window height expansion multiplier
  double PAE_I7_S76 = 10;       // Neighbor search window width expansion multiplier

  std::string VIAINPIN_BOTTOMLAYER_NAME;
  std::string VIAINPIN_TOPLAYER_NAME;
  frLayerNum VIAINPIN_BOTTOMLAYERNUM = std::numeric_limits<frLayerNum>::max();
  frLayerNum VIAINPIN_TOPLAYERNUM = std::numeric_limits<frLayerNum>::max();

  std::string VIA_ACCESS_LAYER_NAME;
  frLayerNum VIA_ACCESS_LAYERNUM = 2;

  int MINNUMACCESSPOINT_MACROCELLPIN = 3;
  int MINNUMACCESSPOINT_STDCELLPIN = 3;
  int PA_MIN_ON_GRID_CANDIDATES = 3;
  int ACCESS_PATTERN_END_ITERATION_NUM = 10;
  float CONGESTION_THRESHOLD = 0.4;
  int MAX_CLIPSIZE_INCREASE = 18;

  int END_ITERATION = 80;

  int NDR_NETS_RIPUP_HARDINESS = 3;  // max ripup avoids
  int CLOCK_NETS_TRUNK_RIPUP_HARDINESS = 100;
  int CLOCK_NETS_LEAF_RIPUP_HARDINESS = 10;
  bool AUTO_TAPER_NDR_NETS = true;
  int TAPERBOX_RADIUS = 3;
  int NDR_NETS_ABS_PRIORITY = 2;
  int CLOCK_NETS_ABS_PRIORITY = 4;

  frUInt4 TAPINCOST = 4;
  frUInt4 TAALIGNCOST = 4;
  frUInt4 TADRCCOST = 32;
  float TASHAPEBLOATWIDTH = 1.5;

  frUInt4 VIACOST = 4;
  // new cost used
  frUInt4 GRIDCOST = 2;
  frUInt4 ROUTESHAPECOST = 8;
  frUInt4 MARKERCOST = 32;
  frUInt4 MARKERBLOATWIDTH = 1;  // unused
  frUInt4 BLOCKCOST = 32;
  frUInt4 GUIDECOST = 1;      // disabled change getNextPathCost to enable
  float SHAPEBLOATWIDTH = 3;  // unused

  // GR
  int CONGCOST = 8;
  int HISTCOST = 32;

  std::string REPAIR_PDN_LAYER_NAME;
  frLayerNum REPAIR_PDN_LAYER_NUM = -1;
  frLayerNum GC_IGNORE_PDN_LAYER_NUM = -1;

  // unidirectional layers
  std::unordered_set<odb::dbTechLayer*> unidirectional_layers_;
};

constexpr int DIRBITSIZE = 3;
constexpr int WAVEFRONTBUFFERSIZE = 2;
constexpr int WAVEFRONTBITSIZE = (WAVEFRONTBUFFERSIZE * DIRBITSIZE);
constexpr int WAVEFRONTBUFFERHIGHMASK
    = (111 << ((WAVEFRONTBUFFERSIZE - 1) * DIRBITSIZE));

// GR
constexpr int GRWAVEFRONTBUFFERSIZE = 2;
constexpr int GRWAVEFRONTBITSIZE = (GRWAVEFRONTBUFFERSIZE * DIRBITSIZE);
constexpr int GRWAVEFRONTBUFFERHIGHMASK
    = (111 << ((GRWAVEFRONTBUFFERSIZE - 1) * DIRBITSIZE));

constexpr int LARGE_NET_FANOUT_THRESHOLD = 100;

class drConnFig;
class drNet;
class frBPin;
class frBTerm;
class frBlock;
class frBlockObject;
class frConnFig;
class frGuide;
class frInst;
class frInstTerm;
class frMTerm;
class frMaster;
class frNet;
class frPathSeg;
class frPin;
class frPolygon;
class frRect;
class frShape;
class frTerm;
class frViaDef;

// These need to be in the fr namespace to support argument-dependent
// lookup
std::ostream& operator<<(std::ostream& os, const frViaDef& viaDefIn);
std::ostream& operator<<(std::ostream& os, const frBlock& blockIn);
std::ostream& operator<<(std::ostream& os, const frInst& instIn);
std::ostream& operator<<(std::ostream& os, const frInstTerm& instTermIn);
std::ostream& operator<<(std::ostream& os, const frBTerm& termIn);
std::ostream& operator<<(std::ostream& os, const frRect& pinFig);
std::ostream& operator<<(std::ostream& os, const frPolygon& pinFig);
std::ostream& operator<<(std::ostream& os, const drConnFig& fig);
std::ostream& operator<<(std::ostream& os, const frShape& fig);
std::ostream& operator<<(std::ostream& os, const frConnFig& fig);
std::ostream& operator<<(std::ostream& os, const frPathSeg& p);
std::ostream& operator<<(std::ostream& os, const frGuide& p);
std::ostream& operator<<(std::ostream& os, const frBlockObject& fig);
std::ostream& operator<<(std::ostream& os, const frNet& n);
std::ostream& operator<<(std::ostream& os, const drNet& n);
std::ostream& operator<<(std::ostream& os, const frMarker& m);

using utl::format_as;

}  // namespace drt
