# master_pae.tcl - Unified entry for ISPD19 PAEtesting
# office env location(in openroad repo location): $ORD_ROOT/pae_test/pae_dr.tcl
# dev env location(executing location): /path/to/proj_orfs/test/pae_dr.tcl

# 1. Parameter Extraction from Environment (AlIl prefixed with PAE_)
set ord_root $env(PAE_ORD_ROOT)
set case_path $env(PAE_BM_CASE)
set do_pae [expr {[info exists env(PAE_DO_PAE)] ? $env(PAE_DO_PAE) : 0}]
set do_enhance [expr {[info exists env(PAE_DO_PAE_ENHANCE)] ? $env(PAE_DO_PAE_ENHANCE): 0}]
set pae_report [expr {[info exists env(PAE_REPORT)] ? $env(PAE_REPORT) : ""}]

set case_name [file tail $case_path]
source "$ord_root/test/helpers.tcl"

# 2. Read Design Data
# Automatically load all LEF files, prioritizing Tech LEF (must be read first)
set lefs [glob -nocomplain "$case_path/*.lef"]
if {[llength $lefs] == 0} {
    puts "Error: No LEF files found in $case_path"
    exit 1
}

set tech_lefs {}
set cell_lefs {}

foreach lef_file $lefs {
    set lower_name [string tolower [file tail $lef_file]]
    if {[string match "*tech*" $lower_name] || [string match "*.tlef" $lower_name]} {
        lappend tech_lefs $lef_file
    } else {
        lappend cell_lefs $lef_file
    }
}

# Read prioritized Tech LEFs first, then Cell LEFs
foreach lef_file [concat $tech_lefs $cell_lefs] {
    read_lef $lef_file
}

# Load DEF files (must have at least one)
set defs [glob -nocomplain "$case_path/*.def"]
if {[llength $defs] == 0} {
    puts "Error: No DEF files found in $case_path"
    exit 1
}
foreach def_file $defs {
    read_def $def_file
}

# Load Guide files (optional but recommended for detailed route)
foreach guide_file [glob -nocomplain "$case_path/*.guide"] {
    read_guides $guide_file
}

# 3. Construct detailed route command
set dr_cmd "detailed_route -output_drc ./drt_output.drc.rpt \
                           -output_maze ./drt_output.maze.log \
                           -output_guide_coverage ./drt_output.coverage.csv \
                           -verbose 2"

if {$do_pae == 1} {
    append dr_cmd " -do_pae"
}
if {$do_enhance == 1} {
    append dr_cmd " -do_pae_enhance"
}
if {$pae_report != ""} {
    append dr_cmd " -pae_report $pae_report"
}

# 4. Execute Routing
puts "Executing: $dr_cmd"
eval $dr_cmd

# 5. Export PAE Results
if {$do_pae == 1 || $do_enhance == 1 || $pae_report != ""} {
    report_pin_acc -file "./PAE.report"
}

# 6. Finalize
set def_file [make_result_file drt_output.def]
write_def $def_file
exit