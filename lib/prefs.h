// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#ifndef BOINC_PREFS_H
#define BOINC_PREFS_H

#include <cstdio>

#include "miofile.h"
#include "parse.h"

// global prefs are maintained as follows:
// 1) a "global_prefs.xml" file, which stores the "network" prefs;
//      it's maintained by communication with scheduling servers
//      or project managers
// 2) a "global_prefs_override.xml" file, which can be edited manually
//      or via a GUI.
//      For the prefs that it specifies, it overrides the network prefs.

// A struct with one bool per pref.
// This is passed in GUI RPCs (get/set_global_prefs_override_struct)
// to indicate which prefs are (or should be) specified in the override file
//
struct GLOBAL_PREFS_MASK {
    bool battery_charge_min_pct;
    bool battery_max_temperature;
    bool confirm_before_connecting;
    bool cpu_scheduling_period_minutes;
    bool cpu_usage_limit;
    bool daily_xfer_limit_mb;
    bool daily_xfer_period_days;
    bool disk_interval;
    bool disk_max_used_gb;
    bool disk_max_used_pct;
    bool disk_min_free_gb;
    bool dont_verify_images;
    bool end_hour;
    bool hangup_if_dialed;
    bool idle_time_to_run;
    bool leave_apps_in_memory;
    bool max_bytes_sec_down;
    bool max_bytes_sec_up;
    bool max_ncpus;
    bool max_ncpus_pct;
    bool net_end_hour;
    bool net_start_hour;
    bool network_wifi_only;
    bool ram_max_used_busy_frac;
    bool ram_max_used_idle_frac;
    bool run_if_user_active;
    bool run_gpu_if_user_active;
    bool run_on_batteries;
    bool start_hour;
    bool suspend_cpu_usage;
    bool suspend_if_no_recent_input;
    bool vm_max_used_frac;
    bool work_buf_additional_days;
    bool work_buf_min_days;

    GLOBAL_PREFS_MASK();
    void clear();
    bool are_prefs_set();
    bool are_simple_prefs_set();
    void set_all();
};


// 0..24
// run always if start==end or start==0, end=24
// don't run at all if start=24, end=0
//
struct TIME_SPAN {
    bool present;
    double start_hour;
    double end_hour;

    enum TimeMode {
        Always = 7000,
        Never,
        Between
    };
    TIME_SPAN() : present(false), start_hour(0), end_hour(0) {}
    TIME_SPAN(double start, double end) : present(false), start_hour(start), end_hour(end) {}

    bool suspended(double hour) const;
    TimeMode mode() const;
};


struct WEEK_PREFS {
    TIME_SPAN days[7];

    void clear() {
        memset(this, 0, sizeof(WEEK_PREFS));
    }
    WEEK_PREFS() {
        clear();
    }

    void set(int day, double start, double end);
    void set(int day, TIME_SPAN* time);
    void unset(int day);

protected:
    void copy(const WEEK_PREFS& original);
};


struct TIME_PREFS : public TIME_SPAN {
    WEEK_PREFS week;

    TIME_PREFS() {}
    TIME_PREFS(double start, double end) {
        start_hour = start;
        end_hour = end;
    }
    
    void clear();
    bool suspended(double t);
    
};


struct GLOBAL_PREFS {
    double mod_time;

    double battery_charge_min_pct;
    double battery_max_temperature;
    bool confirm_before_connecting;
    double cpu_scheduling_period_minutes;
        // length of a time slice.
        // scheduling happens more often.
    TIME_PREFS cpu_times;
    double cpu_usage_limit;
        // for CPU throttling.  This is a percentage 0..100
    double daily_xfer_limit_mb;
    int daily_xfer_period_days;
    double disk_interval;
    double disk_max_used_gb;
    double disk_max_used_pct;
    double disk_min_free_gb;
    bool dont_verify_images;
    bool hangup_if_dialed;
    double idle_time_to_run;
    bool leave_apps_in_memory;
    double max_bytes_sec_down;
    double max_bytes_sec_up;
    int max_ncpus;
    double max_ncpus_pct;
    TIME_PREFS net_times;
    bool network_wifi_only;
        // introduced with Android. Do network communication only when on Wifi,
        // not on public cell networks.
        // CAUTION: this only applies to file transfers.
        // scheduler RPCs are made regardless of this preference.
    double ram_max_used_busy_frac;
    double ram_max_used_idle_frac;
    bool run_gpu_if_user_active;
    bool run_if_user_active;
    bool run_on_batteries;
        // poorly named; what it really means is:
        // if false, suspend while on batteries
    double suspend_cpu_usage;
    double suspend_if_no_recent_input;
    double vm_max_used_frac;
    double work_buf_additional_days;
    double work_buf_min_days;

    char source_project[256];
    char source_scheduler[256];
    bool host_specific;
        // an account manager can set this; if set, don't propagate
    bool override_file_present;

    GLOBAL_PREFS();
    void defaults();
    void enabled_defaults();
    void init();
    void init_bools();
    int parse(XML_PARSER&, const char* venue, bool& found_venue, GLOBAL_PREFS_MASK& mask);
    int parse_day(XML_PARSER&);
    int parse_override(XML_PARSER&, const char* venue, bool& found_venue, GLOBAL_PREFS_MASK& mask);
    int parse_file(const char* filename, const char* venue, bool& found_venue);
    int write(MIOFILE&);
    int write_subset(MIOFILE&, GLOBAL_PREFS_MASK&);
    void write_day_prefs(MIOFILE&);
    inline double cpu_scheduling_period() {
        return cpu_scheduling_period_minutes*60;
    }
};

#endif
