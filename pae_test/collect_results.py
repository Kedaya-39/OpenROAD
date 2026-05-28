#!/usr/bin/env python3
import json
import re
import sys
import os

def hms_to_seconds(hms_str):
    try:
        h, m, s = map(int, hms_str.split(':'))
        return h * 3600 + m * 60 + s
    except:
        return 0

def collect(round_num, phase, case_name, work_dir, output_csv):
    metrics_file = os.path.join(work_dir, "openroad.metrics")
    log_file = os.path.join(work_dir, "openroad.log")
    
    data = {
        "Round": round_num,
        "Phase": phase,
        "Case_Name": case_name,
        "DRC_Count": "N/A",
        "Wirelength": "N/A",
        "Via_Count": "N/A",
        "Route_Iter_Count": "N/A",
        "Runtime_s": "N/A",
        "Memory_MB": "N/A",
        "Peak_Mem_MB": "N/A"
    }

    # 1. Parse Metrics (JSON)
    if os.path.exists(metrics_file):
        with open(metrics_file, 'r') as f:
            try:
                m = json.load(f)
                data["DRC_Count"] = m.get("dr__violations", m.get("route__drc_errors", "0"))
                data["Wirelength"] = m.get("route__wirelength", "0")
                data["Via_Count"] = m.get("route__vias", "0")
                
                # Extract last iter index
                # OpenROAD metrics often store it as route__wirelength__iter:1, route__wirelength__iter:2...
                # or as a list in route__wirelength__iter
                iters = [k for k in m.keys() if "route__wirelength__iter" in k]
                if iters:
                    # Try to get the max index from keys like "route__wirelength__iter:idx"
                    idx_list = []
                    for k in iters:
                        parts = k.split(':')
                        if len(parts) > 1 and parts[-1].isdigit():
                            idx_list.append(int(parts[-1]))
                    if idx_list:
                        data["Route_Iter_Count"] = max(idx_list)
                    elif isinstance(m.get("route__wirelength__iter"), list):
                        data["Route_Iter_Count"] = len(m["route__wirelength__iter"])
            except:
                pass

    # 2. Parse Log (Regex)
    if os.path.exists(log_file):
        with open(log_file, 'r') as f:
            lines = f.readlines()
            for line in reversed(lines):
                if "[INFO DRT-0267]" in line:
                    # Example: [INFO DRT-0267] cpu time = 01:02:03, elapsed time = 01:02:03, memory = 5000.01 (MB), peak = 6000.01 (MB)
                    match = re.search(r"elapsed time = (\d+:\d+:\d+), memory = ([\d\.]+) \(MB\), peak = ([\d\.]+) \(MB\)", line)
                    if match:
                        data["Runtime_s"] = hms_to_seconds(match.group(1))
                        data["Memory_MB"] = match.group(2)
                        data["Peak_Mem_MB"] = match.group(3)
                        break

    # 3. Write to CSV
    file_exists = os.path.isfile(output_csv)
    with open(output_csv, 'a') as f:
        if not file_exists:
            f.write("Round,Phase,Case_Name,DRC_Count,Wirelength,Via_Count,Route_Iter_Count,Runtime_s,Memory_MB,Peak_Mem_MB\n")
        f.write(f"{data['Round']},{data['Phase']},{data['Case_Name']},{data['DRC_Count']},{data['Wirelength']},{data['Via_Count']},{data['Route_Iter_Count']},{data['Runtime_s']},{data['Memory_MB']},{data['Peak_Mem_MB']}\n")

if __name__ == "__main__":
    if len(sys.argv) < 6:
        print("Usage: collect_results.py <round> <phase> <case_name> <work_dir> <output_csv>")
        sys.exit(1)
    collect(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
