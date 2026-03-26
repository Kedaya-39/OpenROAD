// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "odb/geom.h"
#include "pa/FlexPA_unique.h"
#include "utl/Logger.h"

namespace drt {

class FlexPA;
class FlexPinAccessPattern;
class frInst;
class frDesign;
class frMaster;
class frAccessPoint;
struct RouterConfiguration;

/**
 * @brief Technical environment key for validation to ensure compatibility between
 * different design flows.
 */
struct PAETechKey
{
  std::string tech_name;
  int dbu;
  double manufacturing_grid;

  bool operator==(const PAETechKey& other) const
  {
    return tech_name == other.tech_name && dbu == other.dbu
           && std::abs(manufacturing_grid - other.manufacturing_grid) < 1e-6;
  }
};

/**
 * @brief Historical data for an Access Pattern, imported from previous runs.
 * Scores and indicators are stored as integers (normalized value * 1000).
 */
struct HistPatternData
{
  int i1, i2, i3, i4, i7;
  int n_selected, n_ripup, n_nbRipup;
  int s_static, s_dynamic, s_final;
};

/**
 * @brief Historical data for a Unique Class, imported from previous runs.
 * Scores and indicators are stored as integers (normalized value * 1000).
 */
struct HistUCData
{
  int pattern_count;
  int i5, i6, final_score;
};

/**
 * @brief Metrics and scores for an individual access pattern.
 * Uses atomics for dynamic metrics to ensure thread-safety during parallel
 * routing. Indicators and scores are stored as integers (normalized value * 1000).
 */
struct PAEPatternMetrics
{
  std::string PAEPatternKey;  // Cache for getPAEPatternKey

  // Static indicators (Stored as int: normalized * 1000)
  int i1{0};  // Track Alignment
  int i2{0};  // Access Directions
  int i3{0};  // Spatial Sparsity
  int i4{0};  // Track Occupation

  // Dynamic metrics
  std::atomic<int> n_ripup{0};
  std::atomic<int> n_selected{0};
  std::atomic<int> n_nbRipup{0};
  int i7{0};  // Rip-up Frequency (Dynamic penalty)

  int static_score{0};
  int dynamic_score{0};
  int final_score{0};

  PAEPatternMetrics() = default;
  // Atomics are non-copyable; use unique_ptr in storage maps.
  PAEPatternMetrics(const PAEPatternMetrics&) = delete;
  PAEPatternMetrics& operator=(const PAEPatternMetrics&) = delete;
};

/**
 * @brief Aggregate metrics and score for a Unique Class (a set of patterns for a
 * cell placement).
 * Scores and indicators are stored as integers (normalized value * 1000).
 */
struct PAEUClassMetrics
{
  std::string PAEUClassKey;  // Cache for getPAEUClassKey
  int i5{0};  // Access Pattern Capacity
  int i6{0};  // Access Pattern Diversity
  int final_score{0};
};

/**
 * @brief Central manager for Pin Access Evaluation (PAE).
 * Handles static analysis, dynamic monitoring of routing events, scoring,
 * and historical data management (Knowledge Transfer).
 */
class PinAccessEvalMgr
{
 public:
  PinAccessEvalMgr(frDesign* design, odb::dbDatabase* db, FlexPA* pa, utl::Logger* logger);
  ~PinAccessEvalMgr() = default;

  // --- Core Lifecycle Methods ---

  /**
   * @brief Performs static analysis of all generated access patterns.
   * Invoked after initial pattern generation in FlexPA.
   */
  void runStaticAnalysis();

  void setPA(FlexPA* pa) { pa_ = pa; }

  /**
   * @brief Records that the pattern currently assigned to an instance was selected.
   * @param inst The cell instance.
   */
  void countPatternSelection(frInst* inst);

  /**
   * @brief Attributes a rip-up event to the pattern currently used by the
   * instance.
   * @param inst The cell instance connected to the ripped-up net.
   */
  void countPatternRipup(frInst* inst);
  void countPatternNbRipup(frInst* inst);

  /**
   * @brief Calculates final scores and dynamic metrics.
   * Typically called after the routing stage or before generating a report.
   */
  void runDynamicAnalysis();

  void calculateScores() { runDynamicAnalysis(); }

  // --- Data Accessors ---

  int getPatternStaticScore(FlexPinAccessPattern* pattern) const;
  int getPatternFinalScore(FlexPinAccessPattern* pattern) const;
  int getUClassScore(UniqueClass* uclass) const;

  // --- Historical Management (Knowledge Transfer) ---

  /**
   * @brief Exports the current PAE database to a report file.
   */
  void report(const std::string& filename);

  /**
   * @brief Imports historical PAE scores from a report file.
   * Validates tech environment before importing.
   */
  bool importReport(const std::string& filename);

  /**
   * @brief Imports PAE scoring parameters from a YAML file.
   */
  bool importParams(const std::string& filename);

  int getImportedPatternScore(const std::string& pattern_id) const;
  int getImportedUClassScore(const std::string& uclass_id) const;
  int getImportedCellScore(const std::string& master_name) const;

  bool hasImportedReport() const { return !hist_pattern_db_.empty(); }

  // --- Identifier Generation Helpers ---

  /**
   * @brief Generates a deterministic signature hash for a pattern.
   * ID format: P_{UClassID}_{SignatureHash}
   */
  std::string getPAEPatternKey(UniqueClass* uclass, FlexPinAccessPattern* pattern);

  /**
   * @brief Generates a unique ID for a placement scenario.
   * ID format: UC_{Master}_{Orient}_{OffX}_{OffY}
   */
  std::string getPAEUClassKey(UniqueClass* uclass);

 private:
  frDesign* design_;
  odb::dbDatabase* db_;
  FlexPA* pa_;
  utl::Logger* logger_;

  bool params_imported_{false};
  bool report_imported_{false};

  // Active Database: Maps standard objects to their calculated metrics.
  // Using unique_ptr to handle non-copyable atomic members in metrics structs.
  std::unordered_map<FlexPinAccessPattern*, std::unique_ptr<PAEPatternMetrics>>
      pattern_metrics_db_;
  std::unordered_map<UniqueClass*, std::unique_ptr<PAEUClassMetrics>>
      uclass_metrics_db_;

  // Cache for neighbor metrics to accelerate dynamic monitoring
  std::unordered_map<frInst*, std::vector<PAEPatternMetrics*>> inst_neighbor_metrics_cache_;
  mutable std::mutex cache_mutex_;

  // Historical Database: Maps string IDs from reports to historical data.
  std::unordered_map<std::string, int> hist_cell_db_;
  std::unordered_map<std::string, HistUCData> hist_uclass_db_;
  std::unordered_map<std::string, HistPatternData> hist_pattern_db_;

  // Internal Score Calculation Logic
  void updatePatternScoreFactor(UniqueClass* uclass,
                                FlexPinAccessPattern* pattern,
                                PAEPatternMetrics* metrics);
  void updatePatternStaticScore(UniqueClass* uclass, FlexPinAccessPattern* pattern);
  void updatePatternDynamicScore(PAEPatternMetrics* m);
  void updateUClassScoreFactor(UniqueClass* uclass);
  void updateUClassScore(UniqueClass* uclass);

  double calculateAPAlignmentScore(frAccessPoint* ap);
  double calculateAPDirectionScore(frAccessPoint* ap);

  const RouterConfiguration* getRouterConfig() const;
  PAEPatternMetrics* ensurePatternMetrics(FlexPinAccessPattern* pattern);
  PAEPatternMetrics* ensurePatternMetrics(frInst* inst);
  PAEUClassMetrics* ensureUClassMetrics(UniqueClass* uclass);

  PAETechKey getPAETechKey() const;
};

}  // namespace drt
