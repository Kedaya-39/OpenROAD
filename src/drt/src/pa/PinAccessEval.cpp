// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "pa/PinAccessEval.h"

#include "yaml-cpp/yaml.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <numeric>
#include <sstream>
#include <set>

#include "utl/timer.h"
#include "db/obj/frInst.h"
#include "db/obj/frMaster.h"
#include "db/obj/frTerm.h"
#include "db/obj/frPin.h"
#include "db/obj/frAccess.h"
#include "frDesign.h"
#include "global.h"
#include "odb/db.h"
#include "pa/FlexPA.h"
#include "pa/FlexPA_unique.h"

namespace drt {

PinAccessEvalMgr::PinAccessEvalMgr(frDesign* design,
                                 odb::dbDatabase* db,
                                 FlexPA* pa,
                                 utl::Logger* logger)
    : design_(design), db_(db), pa_(pa), logger_(logger)
{
}

const RouterConfiguration* PinAccessEvalMgr::getRouterConfig() const
{
  return pa_ ? pa_->router_cfg_ : nullptr;
}

PAEPatternMetrics* PinAccessEvalMgr::ensurePatternMetrics(FlexPinAccessPattern* pattern)
{
  if (!pattern) {
    return nullptr;
  }
  auto it = pattern_metrics_db_.find(pattern);
  if (it == pattern_metrics_db_.end()) {
    auto m = std::make_unique<PAEPatternMetrics>();
    auto ptr = m.get();
    pattern_metrics_db_[pattern] = std::move(m);
    return ptr;
  }
  return it->second.get();
}

PAEPatternMetrics* PinAccessEvalMgr::ensurePatternMetrics(frInst* inst)
{
  if (!pa_ || !pa_->getUniqueInsts()) {
    return nullptr;
  }
  UniqueClass* uc = pa_->getUniqueInsts()->getUniqueClass(inst);
  if (!uc) {
    return nullptr;
  }
  int patternIdx = inst->getPaPatternIdx();
  auto it = pa_->unique_inst_patterns_.find(uc);
  if (it != pa_->unique_inst_patterns_.end() && patternIdx >= 0
      && patternIdx < (int) it->second.size()) {
    return ensurePatternMetrics(it->second[patternIdx].get());
  }
  return nullptr;
}

PAEUClassMetrics* PinAccessEvalMgr::ensureUClassMetrics(UniqueClass* uclass)
{
  if (!uclass) {
    return nullptr;
  }
  auto it = uclass_metrics_db_.find(uclass);
  if (it == uclass_metrics_db_.end()) {
    auto m = std::make_unique<PAEUClassMetrics>();
    auto ptr = m.get();
    uclass_metrics_db_[uclass] = std::move(m);
    return ptr;
  }
  return it->second.get();
}

PAETechKey PinAccessEvalMgr::getPAETechKey() const
{
  PAETechKey key;
  auto tech = db_->getTech();
  key.tech_name = tech->getName();
  key.dbu = tech->getDbUnitsPerMicron();
  key.manufacturing_grid = (double) tech->getManufacturingGrid() / key.dbu;
  return key;
}

std::string PinAccessEvalMgr::getPAEUClassKey(UniqueClass* uclass)
{
  if (auto m = ensureUClassMetrics(uclass)) {
    if (!m->PAEUClassKey.empty()) {
      return m->PAEUClassKey;
    }
    std::ostringstream oss;
    const auto& offsets = uclass->getOffsets();
    oss << "UC_" << uclass->getMaster()->getName() << "_" << uclass->getOrient().getString()
        << "_" << (offsets.size() > 0 ? offsets[0] : 0) << "_"
        << (offsets.size() > 1 ? offsets[1] : 0);
    m->PAEUClassKey = oss.str();
    return m->PAEUClassKey;
  }
  return "";
}

std::string PinAccessEvalMgr::getPAEPatternKey(UniqueClass* uclass,
                                               FlexPinAccessPattern* pattern)
{
  if (auto m = ensurePatternMetrics(pattern)) {
    if (!m->PAEPatternKey.empty()) {
      return m->PAEPatternKey;
    }

    const auto* cfg = getRouterConfig();

    // Sort APs to ensure deterministic hash
    struct APDesc
    {
      int x, y, layer;
      uint8_t dirs;
      bool operator<(const APDesc& other) const
      {
        if (x != other.x)
          return x < other.x;
        if (y != other.y)
          return y < other.y;
        if (layer != other.layer)
          return layer < other.layer;
        return dirs < other.dirs;
      }
    };

    std::vector<APDesc> descs;
    for (auto ap : pattern->getPattern()) {
      if (!ap)
        continue;
      APDesc d;
      d.x = ap->getPoint().getX();
      d.y = ap->getPoint().getY();
      d.layer = ap->getLayerNum();
      d.dirs = 0;
      const auto& acc = ap->getAccess();
      for (int i = 0; i < 6; ++i) {
        if (acc[i])
          d.dirs |= (1 << i);
      }
      descs.push_back(d);
    }
    std::sort(descs.begin(), descs.end());

    // Use a simple and efficient hash combination (similar to boost::hash_combine)
    size_t hash_val = cfg ? (size_t) cfg->PAE_HASH_SEED : 0;
    auto combine = [](size_t& seed, int v) {
      seed ^= std::hash<int>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    for (const auto& d : descs) {
      combine(hash_val, d.x);
      combine(hash_val, d.y);
      combine(hash_val, d.layer);
      combine(hash_val, (int) d.dirs);
    }

    std::string uclass_id = getPAEUClassKey(uclass);
    // Format: {PAEUClassKey}_P_{num of access points}_{hash rst of aps info}
    m->PAEPatternKey = uclass_id + "_P_" + std::to_string(descs.size()) + "_" + std::to_string(hash_val);
    return m->PAEPatternKey;
  }
  return "";
}

void PinAccessEvalMgr::runStaticAnalysis()
{
  const auto* cfg = getRouterConfig();
  if (!cfg || !cfg->DO_PAE) {
    return;
  }

  logger_->info(utl::DRT, 1012, "PAE: Starting pin access static analysis.");
  utl::Timer timer;

  auto& unique_classes = pa_->getUniqueInsts()->getUniqueClasses();
  for (auto& uc_ptr : unique_classes) {
    UniqueClass* uc = uc_ptr.get();
    if (!pa_->isStdCell(uc->getFirstInst())) {
      continue;
    }

    auto it = pa_->unique_inst_patterns_.find(uc);
    if (it == pa_->unique_inst_patterns_.end()) {
      continue;
    }

    for (auto& pattern : it->second) {
      updatePatternStaticScore(uc, pattern.get());
    }
    updateUClassScoreFactor(uc);
  }
  logger_->info(utl::DRT, 1013, "PAE: Finished pin access static analysis ({:.2f}s).", timer.elapsed());
}

void PinAccessEvalMgr::countPatternSelection(frInst* inst)
{
  if (auto m = ensurePatternMetrics(inst)) {
    m->n_selected++;
  }
}

void PinAccessEvalMgr::countPatternRipup(frInst* inst)
{
  if (auto m = ensurePatternMetrics(inst)) {
    m->n_ripup++;
  }

  // const auto* cfg = getRouterConfig();
  // if (!cfg || cfg->PAE_I7_S74 <= 0) {
  //   return;
  // }

  // std::vector<PAEPatternMetrics*> neighbors;
  // bool use_cache = cfg->PAE_ENABLE_NB_CACHE;
  // bool found_in_cache = false;

  // if (use_cache) {
  //   std::lock_guard<std::mutex> lock(cache_mutex_);
  //   auto it = inst_neighbor_metrics_cache_.find(inst);
  //   if (it != inst_neighbor_metrics_cache_.end()) {
  //     neighbors = it->second;
  //     found_in_cache = true;
  //   }
  // }

  // if (!found_in_cache) {
  //   odb::Rect instBox = inst->getBBox();
  //   int h_bloat = (int) (cfg->PAE_I7_S76 * instBox.dx());
  //   int v_bloat = (int) (cfg->PAE_I7_S75 * instBox.dy());

  //   if (h_bloat > 0 || v_bloat > 0) {
  //     odb::Rect queryBox(instBox.xMin() - h_bloat,
  //                        instBox.yMin() - v_bloat,
  //                        instBox.xMax() + h_bloat,
  //                        instBox.yMax() + v_bloat);
  //     std::set<frInst*> unique_neighbors;
  //     for (int i = design_->getTech()->getBottomLayerNum();
  //          i <= design_->getTech()->getTopLayerNum();
  //          i++) {
  //       if (i > cfg->TOP_ROUTING_LAYER) {
  //         break;
  //       }
  //       if (design_->getTech()->getLayer(i)->getType()
  //           != odb::dbTechLayerType::ROUTING) {
  //         continue;
  //       }
  //       frRegionQuery::Objects<frBlockObject> query_result;
  //       design_->getRegionQuery()->query(queryBox, i, query_result);

  //       for (auto& [box, obj] : query_result) {
  //         if (obj->typeId() == frcInstTerm) {
  //           frInst* nb_inst = static_cast<frInstTerm*>(obj)->getInst();
  //           if (nb_inst != inst) {
  //             unique_neighbors.insert(nb_inst);
  //           }
  //         }
  //       }
  //     }

  //     for (auto nb_inst : unique_neighbors) {
  //       if (auto m_nb = ensurePatternMetrics(nb_inst)) {
  //         neighbors.push_back(m_nb);
  //       }
  //     }
  //   }
  //   if (use_cache) {
  //     std::lock_guard<std::mutex> lock(cache_mutex_);
  //     inst_neighbor_metrics_cache_[inst] = neighbors;
  //   }
  // }

  // for (auto m_nb : neighbors) {
  //   m_nb->n_nbRipup++;
  // }
}

void PinAccessEvalMgr::countPatternNbRipup(frInst* inst)
{
  if (auto m = ensurePatternMetrics(inst)) {
    m->n_nbRipup++;
  }
}

void PinAccessEvalMgr::updatePatternScoreFactor(UniqueClass* uclass,
                                                FlexPinAccessPattern* pattern,
                                                PAEPatternMetrics* metrics)
{
  const auto* cfg = getRouterConfig();

  // Historical Import Support
  if (report_imported_) {
    std::string patKey = getPAEPatternKey(uclass, pattern);
    auto hist_it = hist_pattern_db_.find(patKey);
    if (hist_it != hist_pattern_db_.end()) {
      const auto& hist = hist_it->second;
      metrics->i1 = hist.i1;
      metrics->i2 = hist.i2;
      metrics->i3 = hist.i3;
      metrics->i4 = hist.i4;
      metrics->n_selected.store(hist.n_selected);
      metrics->n_ripup.store(hist.n_ripup);
      metrics->n_nbRipup.store(hist.n_nbRipup);
      return;
    }
  }

  double i1_sum = 0, i2_sum = 0;
  double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
  int ap_cnt = 0;
  const auto& aps = pattern->getPattern();

  if (cfg->PAE_W1 > 0 || cfg->PAE_W2 > 0 || cfg->PAE_W3 > 0) {
    for (auto ap : aps) {
      if (!ap)
        continue;
      ap_cnt++;

      if (cfg->PAE_W1 > 0) {
        i1_sum += calculateAPAlignmentScore(ap);
      }
      if (cfg->PAE_W2 > 0) {
        i2_sum += calculateAPDirectionScore(ap);
      }
      if (cfg->PAE_W3 > 0) {
        sum_x += ap->getPoint().getX();
        sum_y += ap->getPoint().getY();
        sum_z += ap->getLayerNum();
      }
    }
  }

  if (ap_cnt == 0)
    return;

  // I1 Normalization
  if (cfg->PAE_W1 > 0) {
    metrics->i1 = (int) ((i1_sum / ap_cnt) / ((1.0 + cfg->PAE_I1_S16) * cfg->PAE_I1_S15)
                   * 1000.0);
  }

  // I2 Normalization
  if (cfg->PAE_W2 > 0) {
    metrics->i2 = (int) ((i2_sum / ap_cnt) / (double) cfg->PAE_I2_S21 * 1000.0);
  }

  // I3: Spatial Sparsity
  if (cfg->PAE_W3 > 0 && ap_cnt > 1) {
    double mean_x = sum_x / ap_cnt;
    double mean_y = sum_y / ap_cnt;
    double mean_z = sum_z / ap_cnt;
    double var = 0;
    for (auto ap : aps) {
      if (ap) {
        var += std::pow(ap->getPoint().getX() - mean_x, 2)
               + std::pow(ap->getPoint().getY() - mean_y, 2)
               + std::pow((double) ap->getLayerNum() - mean_z, 2);
      }
    }
    var /= ap_cnt;
    metrics->i3 = (int) (cfg->PAE_I3_S31 / (var + cfg->PAE_I3_S31) * 1000.0);
  } else if (cfg->PAE_W3 > 0) {
    metrics->i3 = 1000;
  }
}

void PinAccessEvalMgr::updatePatternStaticScore(UniqueClass* uclass, FlexPinAccessPattern* pattern)
{
  const auto& aps = pattern->getPattern();
  if (aps.empty()) {
    return;
  }

  const auto* cfg = getRouterConfig();
  auto m = ensurePatternMetrics(pattern);

  updatePatternScoreFactor(uclass, pattern, m);

  // Calculate Weighted Static Score
  m->static_score = (int)(cfg->PAE_W1 * m->i1 + cfg->PAE_W2 * m->i2
                          + cfg->PAE_W3 * m->i3 + cfg->PAE_W4 * m->i4);
  updatePatternDynamicScore(m);
  m->final_score = m->static_score + m->dynamic_score;
}

double PinAccessEvalMgr::calculateAPAlignmentScore(frAccessPoint* ap)
{
  auto* cfg = getRouterConfig();
  auto getAlignScore = [&](int type) {
    switch (type) {
      case 0:
        return cfg->PAE_I1_S11;
      case 1:
        return cfg->PAE_I1_S12;
      case 2:
        return cfg->PAE_I1_S13;
      case 3:
        return cfg->PAE_I1_S14;
      default:
        return cfg->PAE_I1_S15;
    }
  };
  return getAlignScore((int)ap->getType(true))
         + cfg->PAE_I1_S16 * getAlignScore((int)ap->getType(false));
}

double PinAccessEvalMgr::calculateAPDirectionScore(frAccessPoint* ap)
{
  auto* cfg = getRouterConfig();
  int reduction = ap->hasViaAccess() ? cfg->PAE_I2_S22 : 0;
  const auto& acc = ap->getAccess();
  for (int i = 0; i < 4; ++i) {
    if (acc[i])
      reduction += cfg->PAE_I2_S23;
  }
  return std::max(0, cfg->PAE_I2_S21 - reduction);
}

void PinAccessEvalMgr::runDynamicAnalysis()
{
  const auto* cfg = getRouterConfig();
  if (!cfg || !cfg->DO_PAE)
    return;

  logger_->info(utl::DRT, 1020, "PAE: Starting pin access dynamic analysis.");
  utl::Timer timer;

  for (auto const& [uc, patterns] : pa_->unique_inst_patterns_) {
    for (auto& p : patterns) {
      auto metrics = ensurePatternMetrics(p.get());
      updatePatternDynamicScore(metrics);
    }
    updateUClassScore(uc);
  }
  logger_->info(utl::DRT, 1021, "PAE: Finished pin access dynamic analysis ({:.2f}s).", timer.elapsed());
}

void PinAccessEvalMgr::updatePatternDynamicScore(PAEPatternMetrics* m)
{
  const auto* cfg = getRouterConfig();

  double raw_i7 = cfg->PAE_I7_S71 * m->n_ripup.load()
                  - cfg->PAE_I7_S72 * m->n_selected.load()
                  + cfg->PAE_I7_S74 * m->n_nbRipup.load();
  // i7 Normalization: Sigmoid
  double norm_i7 = 1.0 / (1.0 + std::exp(cfg->PAE_I7_S73 * raw_i7));
  m->i7 = (int)(norm_i7 * 1000.0);

  m->dynamic_score = (int)(cfg->PAE_W7 * m->i7);
  m->final_score = m->static_score + m->dynamic_score;
}

  // Historical Import Support
void PinAccessEvalMgr::updateUClassScoreFactor(UniqueClass* uclass)
{
  const auto* cfg = getRouterConfig();
  auto m = ensureUClassMetrics(uclass);

  // Historical Import Support
  std::string ucKey = getPAEUClassKey(uclass);
  auto hist_it = hist_uclass_db_.find(ucKey);
  if (hist_it != hist_uclass_db_.end()) {
    m->i5 = hist_it->second.i5;
    m->i6 = hist_it->second.i6;
    return;
  }

  auto it = pa_->unique_inst_patterns_.find(uclass);
  if (it == pa_->unique_inst_patterns_.end() || it->second.empty()) {
    return;
  }

  const auto& patterns = it->second;
  int N = patterns.size();

  // I5: Pattern Capacity
  if (cfg->PAE_W5 > 0) {
    m->i5 = (int)(std::max(0.0, (double)(cfg->PAE_N_TH - N) / cfg->PAE_N_TH) * 1000.0);
  }

  // I6: Pattern Diversity
  if (cfg->PAE_W6 > 0) {
    std::set<frAccessPoint*> pattern_aps;
    for (auto& p : patterns) {
      for (auto ap : p->getPattern()) {
        if (ap) {
          pattern_aps.insert(ap);
        }
      }
    }
    
    int total_aps = 0;
    frMaster* master = uclass->getMaster();
    int paIdx = uclass->getPinAccessIdx();
    for (auto& term : master->getTerms()) {
      for (auto& pin : term->getPins()) {
        auto pa = pin->getPinAccess(paIdx);
        if (pa) total_aps += pa->getNumAccessPoints();
      }
    }

    double i_cov = (total_aps > 0) ? ((double)pattern_aps.size() / total_aps) : 0.0;
    double i_jac = 0.0;
    int pair_count = 0;
    int n_for_jac = std::min(N, cfg->PAE_N_TH);

    if (n_for_jac >= 2) {
      for (int i = 0; i < n_for_jac; ++i) {
        for (int j = i + 1; j < n_for_jac; ++j) {
          const auto& pi = patterns[i]->getPattern();
          const auto& pj = patterns[j]->getPattern();
          std::set<frAccessPoint*> si(pi.begin(), pi.end());
          std::set<frAccessPoint*> sj(pj.begin(), pj.end());
          
          std::vector<frAccessPoint*> intersect;
          std::set_intersection(si.begin(), si.end(), sj.begin(), sj.end(), 
                                std::back_inserter(intersect));
          
          size_t union_size = si.size() + sj.size() - intersect.size();
          if (union_size > 0) {
            i_jac += ((double)intersect.size() / union_size);
          }
          pair_count++;
        }
      }
      i_jac /= pair_count;
    }

    double norm_i6 = cfg->PAE_I6_S61 * (1.0 - i_cov) + cfg->PAE_I6_S62 * (1.0 - i_jac);
    m->i6 = (int)(norm_i6 * 1000.0);
  }
}

void PinAccessEvalMgr::updateUClassScore(UniqueClass* uclass)
{
  auto it = pa_->unique_inst_patterns_.find(uclass);
  if (it == pa_->unique_inst_patterns_.end() || it->second.empty()) {
    return;
  }
  const auto* cfg = getRouterConfig();
  auto m = ensureUClassMetrics(uclass);

  // Average of top N_TH pattern scores
  std::vector<int> scores;
  for (auto& p : it->second) {
    scores.push_back(ensurePatternMetrics(p.get())->final_score);
  }
  std::sort(scores.begin(), scores.end(), std::greater<int>());
  
  int num = std::min((int)it->second.size(), cfg->PAE_N_TH);
  double avg_pat = std::accumulate(scores.begin(), scores.begin() + num, 0.0) / num;

  m->final_score = (int)(avg_pat + cfg->PAE_W5 * m->i5 + cfg->PAE_W6 * m->i6);
}

int PinAccessEvalMgr::getPatternStaticScore(FlexPinAccessPattern* pattern) const
{
  auto it = pattern_metrics_db_.find(pattern);
  return it != pattern_metrics_db_.end() ? it->second->static_score : 0;
}

int PinAccessEvalMgr::getPatternFinalScore(FlexPinAccessPattern* pattern) const
{
  auto it = pattern_metrics_db_.find(pattern);
  return it != pattern_metrics_db_.end() ? it->second->final_score : 0;
}

int PinAccessEvalMgr::getUClassScore(UniqueClass* uclass) const
{
  auto it = uclass_metrics_db_.find(uclass);
  return it != uclass_metrics_db_.end() ? it->second->final_score : 0;
}

void PinAccessEvalMgr::report(const std::string& filename)
{
  logger_->info(utl::DRT, 1035, "PAE: Exporting PAE report to {}.", filename);
  utl::Timer timer;
  std::ofstream out(filename);
  if (!out.is_open()) {
    logger_->error(utl::DRT, 1037, "PAE: Failed to open PAE report for writing: {}", filename);
    return;
  }

  const auto* cfg = getRouterConfig();
  auto tech = getPAETechKey();

  // 7.3.1 Header: PAETechKey
  out << "### PAETechKey:\n";
  out << tech.tech_name << "," << tech.dbu << "," << tech.manufacturing_grid << "\n";

  // 7.3.2 PAE Operational Parameters (Weights & Core Constants)
  out << "### PAE PARAMS:\n";
  out << "W1=" << cfg->PAE_W1 << ",W2=" << cfg->PAE_W2 << ",W3=" << cfg->PAE_W3
      << ",W4=" << cfg->PAE_W4 << ",W5=" << cfg->PAE_W5 << ",W6=" << cfg->PAE_W6 << ",W7=" << cfg->PAE_W7
      << ",N_TH=" << cfg->PAE_N_TH << ",SEED=" << cfg->PAE_HASH_SEED << ",ENABLE_NB_CACHE=" << cfg->PAE_ENABLE_NB_CACHE << "\n";

  // Scoring Constants Detail
  out << "I1_S11=" << cfg->PAE_I1_S11 << ",I1_S12=" << cfg->PAE_I1_S12
      << ",I1_S13=" << cfg->PAE_I1_S13 << ",I1_S14=" << cfg->PAE_I1_S14 << ",I1_S15=" << cfg->PAE_I1_S15
      << ",I1_S16=" << cfg->PAE_I1_S16 << ",I2_S21=" << cfg->PAE_I2_S21 << ",I2_S22=" << cfg->PAE_I2_S22
      << ",I2_S23=" << cfg->PAE_I2_S23 << ",I3_S31=" << cfg->PAE_I3_S31 << ",I6_S61=" << cfg->PAE_I6_S61
      << ",I6_S62=" << cfg->PAE_I6_S62 << ",I7_S71=" << cfg->PAE_I7_S71 << ",I7_S72=" << cfg->PAE_I7_S72
      << ",I7_S73=" << cfg->PAE_I7_S73 << ",I7_S74=" << cfg->PAE_I7_S74 << ",I7_S75=" << cfg->PAE_I7_S75
      << ",I7_S76=" << cfg->PAE_I7_S76 << "\n";

  // 7.3.3 Hierarchical Score Data

  // CELL SCORE
  out << "### CELL SCORE:\n";
  out << "CellName,FinalScore,UniqueClassNum,PatternNum\n";
  std::map<frMaster*, std::vector<UniqueClass*>> cell_to_uclasses;
  for (auto const& [uc, metrics] : uclass_metrics_db_) {
    cell_to_uclasses[uc->getMaster()].push_back(uc);
  }
  for (auto const& [master, uclasses] : cell_to_uclasses) {
    double total_score = 0;
    int total_patterns = 0;
    for (auto uc : uclasses) {
      total_score += uclass_metrics_db_.at(uc)->final_score;
      auto it = pa_->unique_inst_patterns_.find(uc);
      if (it != pa_->unique_inst_patterns_.end()) {
        total_patterns += it->second.size();
      }
    }
    int avg_score = uclasses.empty() ? 0 : (int)(total_score / uclasses.size());
    out << master->getName() << "," << avg_score << "," << uclasses.size() << "," << total_patterns << "\n";
  }

  // UNIQUE CLASS SCORE
  out << "### UNIQUE CLASS SCORE:\n";
  out << "PAEUClassKey,Master,Orient,OffX,OffY,PatternNum,AvgPatternScore,I5,I6,FinalScore\n";
  for (auto const& [uc, metrics] : uclass_metrics_db_) {
    auto it = pa_->unique_inst_patterns_.find(uc);
    int pat_num = (it != pa_->unique_inst_patterns_.end()) ? it->second.size() : 0;
    double avg_pat_score = 0;
    if (pat_num > 0) {
      for (auto& p : it->second) {
        avg_pat_score += ensurePatternMetrics(p.get())->final_score;
      }
      avg_pat_score /= pat_num;
    }
    out << getPAEUClassKey(uc) << "," << uc->getMaster()->getName() << "," << uc->getOrient().getString() << ","
        << (uc->getOffsets().size() > 0 ? uc->getOffsets()[0] : 0) << ","
        << (uc->getOffsets().size() > 1 ? uc->getOffsets()[1] : 0) << ","
        << pat_num << "," << (int)avg_pat_score << "," << metrics->i5 << "," << metrics->i6 << "," 
        << metrics->final_score << "\n";
  }

  // ACCESS PATTERN SCORE
  out << "### ACCESS PATTERN SCORE:\n";
  out << "PAEPatternKey,UClassID,Master,I1,I2,I3,N_selected,N_ripup,N_nbRipup,S_static,S_dynamic,FinalScore\n";
  for (auto const& [uc, metrics] : uclass_metrics_db_) {
    std::string uc_id = getPAEUClassKey(uc);
    auto it = pa_->unique_inst_patterns_.find(uc);
    if (it != pa_->unique_inst_patterns_.end()) {
      for (auto& p : it->second) {
        auto pm = ensurePatternMetrics(p.get());
        out << getPAEPatternKey(uc, p.get()) << "," << uc_id << "," << uc->getMaster()->getName() << ","
            << pm->i1 << "," << pm->i2 << "," << pm->i3 << ","
            << pm->n_selected.load() << "," << pm->n_ripup.load() << "," << pm->n_nbRipup.load() << ","
            << pm->static_score << "," << pm->dynamic_score << "," << pm->final_score << "\n";
      }
    }
  }

  // 7.3.4 Pattern Detail (Access Points)
  out << "### PATTERN DETAIL (ACCESS POINTS):\n";
  out << "PatternID,AP_Index,X,Y,Layer,E,S,W,N,U,D\n";
  int pattern_count = 0;
  for (auto const& [uc, metrics] : uclass_metrics_db_) {
    auto it = pa_->unique_inst_patterns_.find(uc);
    if (it != pa_->unique_inst_patterns_.end()) {
      pattern_count += it->second.size();
      for (auto& p : it->second) {
        std::string pat_id = getPAEPatternKey(uc, p.get());
        const auto& aps = p->getPattern();
        for (int i = 0; i < (int)aps.size(); ++i) {
          auto ap = aps[i];
          if (!ap) continue;
          const auto& acc = ap->getAccess();
          out << pat_id << "," << i << "," << ap->getPoint().getX() << "," << ap->getPoint().getY() << "," << ap->getLayerNum();
          for (int d = 0; d < 6; ++d) {
            out << "," << (acc[d] ? 1 : 0);
          }
          out << "\n";
        }
      }
    }
  }
  logger_->info(utl::DRT, 1036, "PAE: Finished exporting PAE report ({:.2f}s). {} master cells, {} unique classes, {} patterns exported.", timer.elapsed(), (int)cell_to_uclasses.size(), (int)uclass_metrics_db_.size(), pattern_count);
}

bool PinAccessEvalMgr::importReport(const std::string& filename)
{
  logger_->info(utl::DRT, 1030, "PAE: Importing PAE report from {}.", filename);
  utl::Timer timer;
  std::ifstream in(filename);
  if (!in.is_open()) {
    logger_->warn(utl::DRT, 1032, "PAE: Failed to open PAE report for reading: {}", filename);
    return false;
  }

  std::string line;
  enum Section { NONE, TECH, PARAMS, CELL, UCLASS, PATTERN, AP };
  Section current_section = NONE;

  auto cfg = const_cast<RouterConfiguration*>(getRouterConfig());

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    
    // Check for section headers first
    if (line.find("### PAETechKey:") != std::string::npos) {
      current_section = TECH;
      continue;
    } else if (line.find("### PAE PARAMS:") != std::string::npos) {
      current_section = PARAMS;
      continue;
    } else if (line.find("### CELL SCORE:") != std::string::npos) {
      current_section = CELL;
      if (!std::getline(in, line)) break; // Skip header line
      continue;
    } else if (line.find("### UNIQUE CLASS SCORE:") != std::string::npos) {
      current_section = UCLASS;
      if (!std::getline(in, line)) break; // Skip header line
      continue;
    } else if (line.find("### ACCESS PATTERN SCORE:") != std::string::npos) {
      current_section = PATTERN;
      if (!std::getline(in, line)) break; // Skip header line
      continue;
    } else if (line.find("### PATTERN DETAIL") != std::string::npos) {
      current_section = AP;
      if (!std::getline(in, line)) break; // Skip header line
      continue;
    }

    // Skip other comments
    if (line[0] == '#') continue;

    if (current_section == TECH) {
      std::stringstream tech_ss(line);
      std::string hist_name, hist_dbu_str, hist_grid_str;
      std::getline(tech_ss, hist_name, ',');
      std::getline(tech_ss, hist_dbu_str, ',');
      std::getline(tech_ss, hist_grid_str, ',');
      
      if (hist_name.empty() || hist_dbu_str.empty() || hist_grid_str.empty()) {
        logger_->warn(utl::DRT, 1033, "PAE: Malformed PAETechKey line, skipping import.");
        return false;
      }

      PAETechKey hist_tech;
      hist_tech.tech_name = hist_name;
        hist_tech.dbu = std::stoi(hist_dbu_str);
        hist_tech.manufacturing_grid = std::stod(hist_grid_str);

      auto current_tech = getPAETechKey();
      if (!(hist_tech == current_tech)) {
        logger_->warn(utl::DRT, 1034, "PAE: PAE Tech mismatch, skipping import. Design: {}, Report: {}", current_tech.tech_name, hist_tech.tech_name);
        return false;
      }
      continue;
    }

    if (current_section == PARAMS && !params_imported_ && cfg) {
      // W1=0.1,W2=0.1... or I1_S11=0...
      std::stringstream ss(line);
      std::string pair;
      while (std::getline(ss, pair, ',')) {
        size_t pos = pair.find('=');
        if (pos == std::string::npos) continue;
        std::string key = pair.substr(0, pos);
        std::string val = pair.substr(pos + 1);
        try {
          if (key == "W1") cfg->PAE_W1 = std::stod(val);
          else if (key == "W2") cfg->PAE_W2 = std::stod(val);
          else if (key == "W3") cfg->PAE_W3 = std::stod(val);
          else if (key == "W4") cfg->PAE_W4 = std::stod(val);
          else if (key == "W5") cfg->PAE_W5 = std::stod(val);
          else if (key == "W6") cfg->PAE_W6 = std::stod(val);
          else if (key == "W7") cfg->PAE_W7 = std::stod(val);
          else if (key == "N_TH") cfg->PAE_N_TH = std::stoi(val);
          else if (key == "SEED") cfg->PAE_HASH_SEED = std::stoi(val);
          else if (key == "ENABLE_NB_CACHE") cfg->PAE_ENABLE_NB_CACHE = (bool) std::stoi(val);
          else if (key == "I1_S11") cfg->PAE_I1_S11 = std::stoi(val);
          else if (key == "I1_S12") cfg->PAE_I1_S12 = std::stoi(val);
          else if (key == "I1_S13") cfg->PAE_I1_S13 = std::stoi(val);
          else if (key == "I1_S14") cfg->PAE_I1_S14 = std::stoi(val);
          else if (key == "I1_S15") cfg->PAE_I1_S15 = std::stoi(val);
          else if (key == "I1_S16") cfg->PAE_I1_S16 = std::stoi(val);
          else if (key == "I2_S21") cfg->PAE_I2_S21 = std::stoi(val);
          else if (key == "I2_S22") cfg->PAE_I2_S22 = std::stoi(val);
          else if (key == "I2_S23") cfg->PAE_I2_S23 = std::stoi(val);
          else if (key == "I3_S31") cfg->PAE_I3_S31 = std::stoi(val);
          else if (key == "I6_S61") cfg->PAE_I6_S61 = std::stod(val);
          else if (key == "I6_S62") cfg->PAE_I6_S62 = std::stod(val);
          else if (key == "I7_S71") cfg->PAE_I7_S71 = std::stod(val);
          else if (key == "I7_S72") cfg->PAE_I7_S72 = std::stod(val);
          else if (key == "I7_S73") cfg->PAE_I7_S73 = std::stod(val);
          else if (key == "I7_S74") cfg->PAE_I7_S74 = std::stod(val);
          else if (key == "I7_S75") cfg->PAE_I7_S75 = std::stod(val);
          else if (key == "I7_S76") cfg->PAE_I7_S76 = std::stod(val);
        } catch (...) {
           continue;
        }
      }
      continue;
    }

    if (current_section == AP || current_section == NONE || current_section == PARAMS) {
      continue;
    }

    std::stringstream ss(line);
    std::string val;
    try {
      if (current_section == CELL) {
        // Data: CELL NAME,FinalScore,UniqueClassNum,PatternNum
        std::string master_name;
        if (!std::getline(ss, master_name, ',')) continue;
        if (!std::getline(ss, val, ',')) continue; // FinalScore
        hist_cell_db_[master_name] = std::stoi(val);
      } else if (current_section == UCLASS) {
        // Data: PAEUClassKey,Master,Orient,OffX,OffY,PatternNum,AvgPatternScore,I5,I6,FinalScore
        std::vector<std::string> parts;
        while (std::getline(ss, val, ',')) parts.push_back(val);
        if (parts.size() >= 10) {
          HistUCData data;
          data.pattern_count = std::stoi(parts[5]);
          data.i5 = std::stoi(parts[7]);
          data.i6 = std::stoi(parts[8]);
          data.final_score = std::stoi(parts[9]);
          hist_uclass_db_[parts[0]] = data;
        }
      } else if (current_section == PATTERN) {
        // Data: PAEPatternKey,UClassID,Master,I1,I2,I3,N_selected,N_ripup,N_nbRipup,S_static,S_dynamic,FinalScore
        std::vector<std::string> parts;
        while (std::getline(ss, val, ',')) parts.push_back(val);
        if (parts.size() >= 12) {
          HistPatternData data;
          data.i1 = std::stoi(parts[3]);
          data.i2 = std::stoi(parts[4]);
          data.i3 = std::stoi(parts[5]);
          data.n_selected = std::stoi(parts[6]);
          data.n_ripup = std::stoi(parts[7]);
          data.n_nbRipup = std::stoi(parts[8]);
          data.s_static = std::stoi(parts[9]);
          data.s_dynamic = std::stoi(parts[10]);
          data.s_final = std::stoi(parts[11]);
          hist_pattern_db_[parts[0]] = data;
        }
      }
    } catch (...) {
      continue;
    }
  }
  logger_->info(utl::DRT, 1031, "PAE: Finished importing PAE report ({:.2f}s). {} patterns, {} unique classes, {} master cells imported.", timer.elapsed(), hist_pattern_db_.size(), hist_uclass_db_.size(), hist_cell_db_.size());
  report_imported_ = true;
  return true;
}

bool PinAccessEvalMgr::importParams(const std::string& filename)
{
  logger_->info(utl::DRT, 1040, "PAE: Importing PAE parameters from {}.", filename);
  try {
    YAML::Node config = YAML::LoadFile(filename);
    auto cfg = const_cast<RouterConfiguration*>(getRouterConfig());
    if (!cfg) return false;

    if (config["PAE_W1"]) cfg->PAE_W1 = config["PAE_W1"].as<double>();
    if (config["PAE_W2"]) cfg->PAE_W2 = config["PAE_W2"].as<double>();
    if (config["PAE_W3"]) cfg->PAE_W3 = config["PAE_W3"].as<double>();
    if (config["PAE_W4"]) cfg->PAE_W4 = config["PAE_W4"].as<double>();
    if (config["PAE_W5"]) cfg->PAE_W5 = config["PAE_W5"].as<double>();
    if (config["PAE_W6"]) cfg->PAE_W6 = config["PAE_W6"].as<double>();
    if (config["PAE_W7"]) cfg->PAE_W7 = config["PAE_W7"].as<double>();
    if (config["PAE_N_TH"]) cfg->PAE_N_TH = config["PAE_N_TH"].as<int>();
    if (config["PAE_HASH_SEED"]) cfg->PAE_HASH_SEED = config["PAE_HASH_SEED"].as<int>();
    if (config["PAE_ENABLE_NB_CACHE"]) cfg->PAE_ENABLE_NB_CACHE = config["PAE_ENABLE_NB_CACHE"].as<bool>();
    if (config["PAE_I1_S11"]) cfg->PAE_I1_S11 = config["PAE_I1_S11"].as<int>();
    if (config["PAE_I1_S12"]) cfg->PAE_I1_S12 = config["PAE_I1_S12"].as<int>();
    if (config["PAE_I1_S13"]) cfg->PAE_I1_S13 = config["PAE_I1_S13"].as<int>();
    if (config["PAE_I1_S14"]) cfg->PAE_I1_S14 = config["PAE_I1_S14"].as<int>();
    if (config["PAE_I1_S15"]) cfg->PAE_I1_S15 = config["PAE_I1_S15"].as<int>();
    if (config["PAE_I1_S16"]) cfg->PAE_I1_S16 = config["PAE_I1_S16"].as<int>();
    if (config["PAE_I2_S21"]) cfg->PAE_I2_S21 = config["PAE_I2_S21"].as<int>();
    if (config["PAE_I2_S22"]) cfg->PAE_I2_S22 = config["PAE_I2_S22"].as<int>();
    if (config["PAE_I2_S23"]) cfg->PAE_I2_S23 = config["PAE_I2_S23"].as<int>();
    if (config["PAE_I3_S31"]) cfg->PAE_I3_S31 = config["PAE_I3_S31"].as<int>();
    if (config["PAE_I6_S61"]) cfg->PAE_I6_S61 = config["PAE_I6_S61"].as<double>();
    if (config["PAE_I6_S62"]) cfg->PAE_I6_S62 = config["PAE_I6_S62"].as<double>();
    if (config["PAE_I7_S71"]) cfg->PAE_I7_S71 = config["PAE_I7_S71"].as<double>();
    if (config["PAE_I7_S72"]) cfg->PAE_I7_S72 = config["PAE_I7_S72"].as<double>();
    if (config["PAE_I7_S73"]) cfg->PAE_I7_S73 = config["PAE_I7_S73"].as<double>();
    if (config["PAE_I7_S74"]) cfg->PAE_I7_S74 = config["PAE_I7_S74"].as<double>();
    if (config["PAE_I7_S75"]) cfg->PAE_I7_S75 = config["PAE_I7_S75"].as<double>();
    if (config["PAE_I7_S76"]) cfg->PAE_I7_S76 = config["PAE_I7_S76"].as<double>();
    if (config["PA_MIN_ON_GRID_CANDIDATES"]) cfg->PA_MIN_ON_GRID_CANDIDATES = config["PA_MIN_ON_GRID_CANDIDATES"].as<int>();

    params_imported_ = true;
    logger_->info(utl::DRT, 1041, "PAE: Finished importing PAE parameters.");
    return true;
  } catch (const std::exception& e) {
    logger_->warn(utl::DRT, 1042, "PAE: Failed to load PAE parameters from {}: {}", filename, e.what());
    return false;
  }
}

int PinAccessEvalMgr::getImportedPatternScore(const std::string& pattern_id) const
{
  auto it = hist_pattern_db_.find(pattern_id);
  return it != hist_pattern_db_.end() ? it->second.s_final : 0;
}

int PinAccessEvalMgr::getImportedUClassScore(const std::string& uclass_id) const
{
  auto it = hist_uclass_db_.find(uclass_id);
  return it != hist_uclass_db_.end() ? it->second.final_score : 0;
}

int PinAccessEvalMgr::getImportedCellScore(const std::string& master_name) const
{
  auto it = hist_cell_db_.find(master_name);
  return it != hist_cell_db_.end() ? it->second : 0;
}

}  // namespace drt
