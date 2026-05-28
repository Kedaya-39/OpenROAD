# Pin Access Evaluation (PAE) Module Design

## 1. Module Objective
The Pin Access Evaluation (PAE) module is designed to provide an automated, quantitative assessment of standard cell pin accessibility within the Design-Technology Co-Optimization (DTCO) flow. 

In advanced process nodes, the "goodness" of a standard cell depends heavily on how easily it can be routed in complex environments. PAE bridges the gap between static library characterization and dynamic routing performance by:
- Enumerating and scoring all feasible **Access Patterns** for each **Unique Class** of a standard cell.
- Monitoring real-time routing events (e.g., rip-ups) to dynamically update accessibility scores.
- Providing feedback to the router to prefer high-scoring patterns and to library developers for cell layout optimization.

## 2. Input/Output
- **Inputs**:
    - **Physical Libraries (LEF)**: Geometry of standard cell pins and blockage.
    - **Technology Rules**: Design rules (spacing, vias, etc.) from the PDK.
    - **Design Context (DEF/Guides)**: Placement of instances and routing guides.
    - **Historical PAE Report (Optional)**: Previously generated scores from compatible technology environments.
- **Outputs**:
    - **In-memory Database**: Scores for `frMaster` (Cell), `UniqueClass`, and `FlexPinAccessPattern`.
    - **Evaluation Report**: A detailed report (`report_pin_acc`) containing accessibility metrics and scores.
    - **Dynamic Guidance**: Updated costs for access points used by the `FlexDR` (Detailed Router).

## 3. User Interface (TCL)

### 3.1 `detailed_route` Command Options
The `detailed_route` command is extended to support PAE operations through the following flags:

- **`-do_pae`**: 
    - Enables the Pin Access Evaluation scoring and analysis functionality.
- **`-do_pae_enhance`**: 
    - Automatically enables `-do_pae`.
    - Activates the "Enhancement Mode": Historical or calculated PAE scores are used to influence the router's decision-making. Specifically, the PAE `final_score` replaces the original cost in the `FlexPA` access pattern selection phase (e.g., in `FlexPA::genInstRowPatternInit`).
- **`-pae_report <filename>`**: 
    - Automatically enables `-do_pae`.
    - Loads historical PAE data from the specified report file. This includes historical scores and the operational parameters (weights and constants) used when the report was generated.
- **`-pae_para <filename>`**: 
    - Automatically enables `-do_pae`.
    - Loads specific PAE scoring parameters from the specified configuration file. includes:
      - weights: $PAE_W1$ to $PAE_W7$
      - constants: $PAE_I1_S11$ to $PAE_I7_S76$
      - threshold: $PAE_N_TH$, $PA_MIN_ON_GRID_CANDIDATES$
      - seed: $PAE_HASH_SEED$


**Parameter Precedence:**
To ensure flexible control over the evaluation logic, PAE parameters are resolved with the following priority (highest to lowest):
1.  **`-pae_para`**: Parameters defined in the provided parameter file.
2.  **`-pae_report`**: Parameters embedded in the imported historical report.
3.  **Default Values**: Internal defaults configured within the `drt` engine.

### 3.2 `report_pin_acc` Command
- **`report_pin_acc [-file <filename>]`**:
    - Triggers a synchronization of the PAE database and exports the results (Cell, Unique Class, and Pattern scores).
    - Includes technical environment metadata for validation.
    - If `-file` is specified, writes the structured report; otherwise, outputs a summary to the logger.

## 4. Core Data Structures

### 4.1 Key Keys
- **PAETechKey**: Validates environment compatibility.
  - `tech_name`: Name of the technology/PDK.
  - `dbu`: Database units per micron.
  - `manufacturing_grid`: Manufacturing grid resolution.

- **PAEUniqueClassKey**: Identifies a unique class.
  - `master_name`: Name of the `frMaster`.
  - `orient`: Orientation (`odb::dbOrientType`).
  - `offsets`: Track offsets (X and Y).

- **PAEPatternKey**: Identifies a access pattern.
  - `uClassKey`: PAEUniqueClassKey， indicates the unique class it belongs to.
  - `aps`: A vector of Access Point descriptors. Sorted in lexicographic order of {x, y, layerNum}. Each descriptor contains:
    - `point`: (X, Y) coordinate.
    - `layerNum`: Metal layer.
    - `directions`: 6 bits representing [E, S, W, N, U, D] access availability.

### 4.2 Evaluation Database
- **PAEPatternMetrics**: Stores static metrics ($I_1$ to $I_4$), dynamic metrics ($I_7$), and calculated scores.
- **PAEUClassMetrics**: Stores aggregate metrics ($I_5, I_6$) and the final score for a Unique Class.
- **PinAccessEvalMgr**: The central manager holding the mappings:
  - `std::unordered_map<FlexPinAccessPattern*, PAEPatternMetrics> pattern_metrics_db_`
  - `std::unordered_map<UniqueClass*, PAEUClassMetrics> uclass_metrics_db_`

## 5. Evaluation Metrics

### 5.1 Access Pattern Metrics
#### 5.1.1 Static Evaluation Indicators ($I_1$ to $I_4$)
$S_{pattern}^{static} = \sum_{k=1}^4 w_k \cdot Norm_k(I_k)$.

**Indicator $I_1$ - Track Alignment (布线轨道对齐度)**
- **Definition**: Evaluates the degree to which AP centers deviate from routing tracks.
- **Calculation**: For each AP in the pattern, calculate a score based on its alignment on its respective layer:
  - `OnGrid`: $s_{11} = 0$
  - `HalfGrid`: $s_{12} = 1$
  - `Center`: $s_{13} = 2$
  - `EncOpt`: $s_{14} = 5$
  - `Others`: $s_{15} = 10$
- **Total AP Score**: $i_{ap}^1 = \text{LowerLayerScore} + s_{16} \cdot \text{UpperLayerScore}$ ($s_{16} = 4$).
- **Pattern Score $I_1$**: Arithmetic mean of all $i_{ap}^1$.
- **Normalization**: $Norm_1(I_1) = I_1 / ((1+s_{16}) \cdot s_{15})$.

**Indicator $I_2$ - Access Directions (可接入方向)**
- **Definition**: Quantifies the lack of entry flexibility for an AP.
- **Calculation**: Start with a base score $s_{21} = 8$ for each AP. Subtract points for each available access direction:
  - If AP supports via access: $-s_{22}$ ($s_{22} = 4$).
  - For each planar direction (N, S, E, W): $-s_{23}$ ($s_{23} = 1$).
- **Pattern Score $I_2$**: Arithmetic mean of all $i_{ap}^2$.
- **Normalization**: $Norm_2(I_2) = I_2 / s_{21}$.

**Indicator $I_3$ - Spatial Sparsity (空间稀疏性)**
- **Definition**: Measures the crowding of APs in the cell layout. Excessive density leads to local congestion.
- **Calculation**: Let $V$ be the variance of all AP coordinates (including layer information) relative to their centroid $c$.
- **Pattern Score $I_3$**: $I_3 = s_{31} / (V + s_{31})$ ($s_{31} = 1000$).
- **Normalization**: $Norm_3(I_3) = I_3$.
- **Rationale**: When $V \to 0$ (extremely crowded), $Norm_3 \to 1$. When $V$ is large (sparse), $Norm_3 \to 0$.

**Indicator $I_4$ - Track Occupation (轨道资源消耗数)**
- **Definition**: Ratio of routing tracks occupied or blocked by the pattern.
- **Calculation**: $I_4 = N_{occ} / N_{cell}$ (Currently reserved, weight $w_4 = 0$).

#### 5.1.2 Dynamic Evaluation Indicators ($I_7$)
The dynamic score is calculated as: $S_{pattern}^{dynamic} = w_7 \cdot Norm_7(I_7)$.

**Indicator $I_7$ - Rip-up Frequency (拆线重布触发次数)**
- **Definition**: Records the penalty intensity related to rip-ups during the routing stage, considering both the pattern's own performance and its impact on the local neighborhood.
- **Calculation**: $I_7 = s_{71} \cdot N_{ripup} - s_{72} \cdot N_{selected} + s_{74} \cdot N_{nbRipup}$
  - $N_{ripup}$: Number of times nets connected to this pattern were ripped up.
  - $N_{selected}$: Number of times this pattern was chosen.
  - $N_{nbRipup}$: Neighbor Rip-up count. Number of times instances located within an expanded search window were ripped up. The search window is the current instance's BBox expanded by $s_{76} \times \text{width}$ on both left and right sides, and $s_{75} \times \text{height}$ on both top and bottom sides.
  - Constants: $s_{71} = 1.5, $s_{72} = 1.0, $s_{74} = 0.5$. Expansion multipliers: $s_{75} = 2$ (Height multiplier), $s_{76} = 1$ (Width multiplier). For example, $s_{75}=2, s_{76}=1$ results in a search box 5x the instance height and 3x the instance width.
- **Normalization**: $Norm_7(I_7) = 1 / (1 + \exp(s_{73} \cdot I_7))$ ($s_{73} = -0.001$).
- **Rationale**: Uses a Sigmoid function to map the unbounded $I_7$ to $(0, 1)$. $I_7 = 0$ results in 0.5; positive values (poor performance) approach 1; negative values (good performance) approach 0.

### 5.2 Unique Class Metrics
A Unique Class aggregates all possible Access Patterns for a specific cell placement scenario.

**Indicator $I_5$ - Access Pattern Capacity (接入模式集容量)**
- **Definition**: Evaluates the scarcity of available patterns for a Unique Class.
- **Calculation**: $I_5 = \max(0, N_{th} - N) / N_{th}$
  - $N$: Number of valid access patterns.
  - $N_{th}$: Threshold constant ($N_{th} = 10$).
- **Normalization**: $Norm_5(I_5) = I_5$.

**Indicator $I_6$ - Access Pattern Diversity (接入模式多样性)**
- **Definition**: Evaluates the redundancy or lack of variety in the pattern set.
- **Calculation**: $I_6 = s_{61} \cdot (1 - I_{cov}) + s_{62} \cdot (1 - I_{jac})$
  - $I_{cov}$: AP coverage rate (percentage of total available APs used across all patterns).
  - $I_{jac}$: Average Jaccard distance between all pattern pairs.
  - Constants: $s_{61} = 0.5, s_{62} = 0.5$.
- **Normalization**: $Norm_6(I_6) = I_6$.

### 5.3 Composite Scores
- **Pattern Final Score**: $S_{pattern} = S_{pattern}^{static} + S_{pattern}^{dynamic}$.
- **Unique Class Score ($S_{uclass}$)**:
  - If $N < N_{th}$: $S_{uclass} = \frac{1}{N} \sum_{i=1}^N S_{pattern_i} + \sum_{k=5}^6 w_k \cdot Norm_k(I_k)$
  - If $N \ge N_{th}$: The summation part only considers the top $N_{th}$ patterns.
- **Standard Cell Score**: Average of all its constituent Unique Class scores.

## 6. Module Flow & Algorithm
1. **Static Analysis**: Run after `FlexPA::prepPatternInst`.
2. **Historical Import**: If `-pae` is used, query history during PAE Analysis.
3. **Dynamic Monitoring**:
   - `countPatternSelection`: Increment $N_{selected}$ in `prepPatternInstRows`.
   - `recordPatternRipup`: In `FlexDRWorker::route_queue_main`, if a net is ripped up, identify the instance's pattern and increment $N_{ripup}$.
4. **Final Update**: at the end of DR or excution of cmd "report_pin_acc", recalculate $S_{pattern}^{dynamic}$ and final scores.

## 7. Interaction with drt Modules

The PAE module is deeply integrated into the TritonRoute (`drt`) flow to capture data at various stages of the physical implementation.

### 7.1 FlexPA (Pin Access Analysis)
- **Pattern Evaluation**: After `FlexPA::prepPatternInst` generates all possible patterns for a Unique Class, PAE performs static analysis to compute $S_{pattern}^{static}$ for each `FlexPinAccessPattern`.
- **Pattern Tracking**: PAE modifies `frInst` to include `paPatternIdx_`. During `prepPatternInstRows`, when the dynamic programming (DP) algorithm selects the "best" pattern for an instance, PAE records the index of this pattern in the instance.
- **Cost Injection**: If a historical report is imported (`DO_PAE_ENHANCE` is true), PAE injects the historical scores into the DP node costs during `genInstRowPatternInit`. This biases the initial assignment toward patterns that performed well in previous runs.

### 7.2 FlexDR (Detailed Routing)
- **Rip-up Monitoring**: PAE hooks into `FlexDRWorker::route_queue_main`. When a net is ripped up due to congestion or DRC violations, PAE identifies the standard cell instances connected to that net.
- **Pattern Attribution**: Using the `paPatternIdx_` stored in `frInst`, PAE attributes the rip-up event to the specific Access Pattern being used by that instance, incrementing its `n_ripup` count.


## 8. PAE Report Management

### 8.1 Purpose
Enables **Accessibility Knowledge Transfer**. Allows different implementation flows using the same PDK/Library to share and benefit from routing accessibility data.
- **Export**: Captures the "learned" accessibility of cells from a completed routing run.
- **Import**: Allows a new run to benefit from previous experience, avoiding patterns that historically caused rip-ups and preferring those that were easy to route.

### 8.2 Report Format & Content
The report is a structured text file designed for both readability and efficient parsing.

#### 8.2.1 Header: PAETechKey
Ensures strict techlib consistency between flows. If any of these differ, the report is rejected:
- `tech_name`, `dbu`, `manufacturing_grid`.

#### 8.2.2 PAE Operational Parameters
Since different flows may use different weighting strategies, the report records the parameters used during its generation:
- `PAE_W1` to `PAE_W7`: Metric weights.
- `PAE_S_CONSTANTS`: Normalization and scoring constants (e.g., $s_{71}, s_{31}$).
- `PAE_N_TH`: Capacity threshold.

#### 8.2.3 Hierarchical Score Data
Matching is performed using the following ID and data structure:

| Level | ID Construction (Key) | Data Columns |
| :--- | :--- | :--- |
| **Cell** | `MasterName` | `Uniqule Class Num`, `Pattern Num`, `FinalScore` |
| **Unique Class** | `PAEUClassKey` | `Master`, `Orient`, `OffX`, `OffY`, `Pattern Num`, `Pattern Avg. Score`, `I5`, `I6`, `FinalScore` |
| **Access Pattern** | `PAEPatternKey` | `PAEUClassKey`, `Master`, `I1`, `I2`, `I3`, `N_selected`, `N_ripup`, `N_nbRipup`, `S_static`, `S_dynamic`, `S_final` |

**ID Implementation Details**:
- **Unique Class ID(PAEUClassKey)**: Combines the Master name, orientation string (e.g., R0, MX), and track offsets (in DBU) to uniquely identify a placement scenario.
- **Pattern ID (PAEPatternKey)**: Since patterns have varying AP counts, the ID is constructed by its unique class `PAEUClassKey` and a deterministic hash of the sorted string representation of its APs. This ensures identical patterns across different design runs get the same ID.

#### 8.2.4 Pattern Detail (Access Points)
Maps a `PatternID` to its physical composition for verification and detailed analysis:
- `PatternID(PAEPatternKey)`, `AP_Index`, `X`, `Y`, `Layer`, `E`, `S`, `W`, `N`, `U`, `D` (6-bit direction availability).

### 8.3 Import & Application Logic

#### 8.3.1 Database Mapping
The manager maintains three separate lookup maps for efficient historical data retrieval:
- `std::unordered_map<String, CellScore> hist_cell_db_`
- `std::unordered_map<String, UCData> hist_uclass_db_` (Key: UC ID)
- `std::unordered_map<String, PatternData> hist_pattern_db_` (Key: Pattern ID)

#### 8.3.2 Historical Enhancement (DO_PAE_ENHANCE)
When a historical match is found during the current flow:

1.  **Access Pattern Level**:
    - **Static Scoring**: $S_{pattern}^{static} = S_{pattern\_hist}^{static}$. Skip recalculating $I_1, I_2, I_3$ as these are design-independent properties of the pattern.
    - **Dynamic Metrics**: Initialize $N_{ripup}$ and $N_{selected}$ with historical values from the report. This allows the evaluation to build upon previous routing experience.
2.  **Unique Class Level**:
    - If the current unique class has the same `PatternCount` as the history: $I_5 = I_{5\_hist}$ and $I_6 = I_{6\_hist}$. These metrics describe the pattern set diversity, which is fixed for a given techlib.
3.  **Flow Timing**:
    - **Step 1**: During `FlexPA` static analysis (after all patterns are generated for a Unique Class), query history for each pattern and UC.
    - **Step 2**: Import $I_7$ counts ($N_{ripup}, N_{selected}, N_{nbRipup}$) into the active metrics database **before** the `countPatternSelection` call (which occurs during row-based pattern assignment).
    - **Step 3**: The dynamic scoring logic will naturally incorporate these "warm" initial values.


## 9. Testing Strategy
- **Validation**: Verify `PAETechKey` rejects reports if `manufacturing_grid` or layer `pitch` changes.
- **Consistency**: Ensure `SignatureHash` generates the same ID for identical AP sets regardless of enumeration order.
- **Convergence**: Compare routing iterations between "cold start" and "warm start" (imported PAE) on identical designs.
