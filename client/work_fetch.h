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

// Work fetch logic for CPU, GPU, and other processing resources.
// See http://boinc.berkeley.edu/trac/wiki/GpuWorkFetch

#ifndef BOINC_WORK_FETCH_H
#define BOINC_WORK_FETCH_H

#include <vector>
#include <deque>

#define RSC_TYPE_ANY    -1
#define RSC_TYPE_CPU    0

// reasons for not fetching work from a project
//
#define CANT_FETCH_WORK_NON_CPU_INTENSIVE           1
#define CANT_FETCH_WORK_SUSPENDED_VIA_GUI           2
#define CANT_FETCH_WORK_MASTER_URL_FETCH_PENDING    3
#define CANT_FETCH_WORK_MIN_RPC_TIME                4
#define CANT_FETCH_WORK_DONT_REQUEST_MORE_WORK      5
#define CANT_FETCH_WORK_DOWNLOAD_STALLED            6
#define CANT_FETCH_WORK_RESULT_SUSPENDED            7
#define CANT_FETCH_WORK_TOO_MANY_UPLOADS            8
#define CANT_FETCH_WORK_NOT_HIGHEST_PRIORITY        9
#define CANT_FETCH_WORK_DONT_NEED                   10
#define CANT_FETCH_WORK_TOO_MANY_RUNNABLE           11

// in case of DONT_NEED, per-resource reason
//
#define DONT_FETCH_GPUS_NOT_USABLE                  1
#define DONT_FETCH_PREFS                            2
#define DONT_FETCH_CONFIG                           3
#define DONT_FETCH_NO_APPS                          4
#define DONT_FETCH_AMS                              5
#define DONT_FETCH_ZERO_SHARE                       7
#define DONT_FETCH_BUFFER_FULL                      8
#define DONT_FETCH_NOT_HIGHEST_PRIO                 9
#define DONT_FETCH_BACKED_OFF                       10
#define DONT_FETCH_DEFER_SCHED                      11

struct PROJECT;
struct RESULT;
struct ACTIVE_TASK;
struct RSC_WORK_FETCH;
struct SCHEDULER_REPLY;
struct APP_VERSION;

typedef long long COPROC_INSTANCE_BITMAP;
    // should be at least MAX_COPROC_INSTANCES (64) bits

// state per (resource, project) pair
//
struct RSC_PROJECT_WORK_FETCH {
    // the following are persistent (saved in state file)
    double backoff_time;
    double backoff_interval;

    // the following used by REC accounting
    double secs_this_rec_interval;

    double queue_est;
        // an estimate of instance-secs of queued work;
    bool anonymous_platform_no_apps;
        // set if this project is anonymous platform
        // and it has no app version that uses this resource
    double fetchable_share;
        // this project's share relative to projects from which
        // we could probably get work for this resource;
        // determines how many instances this project deserves
    int n_runnable_jobs;
    double sim_nused;
        // # of instances used at this point in the simulation
    double nused_total;     // sum of instances over all runnable jobs
    int ncoprocs_excluded;
        // number of excluded instances
    COPROC_INSTANCE_BITMAP non_excluded_instances;
        // bitmap of non-excluded instances
        // (i.e. instances this project's jobs can run on)
    int deadlines_missed;
    int deadlines_missed_copy;
        // copy of the above used during schedule_cpus()
    std::deque<RESULT*> pending;
    std::deque<RESULT*>::iterator pending_iter;
    bool has_deferred_job;
        // This project has a coproc job of the given type for which
        // the job is deferred because of a temporary_exit() call.
        // Don't fetch more jobs of this type; they might have same problem
    int rsc_project_reason;
        // If zero, it's OK to ask this project for this type of work.
        // If nonzero, the reason why it's not OK

    RSC_PROJECT_WORK_FETCH() {
        backoff_time = 0;
        backoff_interval = 0;
        secs_this_rec_interval = 0;
        queue_est = 0;
        anonymous_platform_no_apps = false;
        fetchable_share = 0;
        n_runnable_jobs = 0;
        sim_nused = 0;
        nused_total = 0;
        ncoprocs_excluded = 0;
        non_excluded_instances = 0;
        deadlines_missed = 0;
        deadlines_missed_copy = 0;
        pending.clear();
        has_deferred_job = false;
        rsc_project_reason = 0;
    }

    inline void reset() {
        backoff_time = 0;
        backoff_interval = 0;
    }

    inline void reset_rec_accounting() {
        secs_this_rec_interval = 0;
    }
    int compute_rsc_project_reason(PROJECT*, int rsc_type);
    void resource_backoff(PROJECT*, const char*);
    void rr_init();
    void clear_backoff() {
        backoff_time = 0;
        backoff_interval = 0;
    }
};

// estimate the time a resource will be saturated
// with high-priority jobs.
//
struct BUSY_TIME_ESTIMATOR {
    std::vector<double> busy_time;
    int ninstances;
    inline void reset() {
        for (int i=0; i<ninstances; i++) {
            busy_time[i] = 0;
        }
    }
    inline void init(int n) {
        ninstances = n;
        busy_time.resize(n);
        reset();
    }
    // called for each high-priority job.
    // Find the least-busy instance, and put this job
    // on that and following instances
    //
    inline void update(double dur, double nused) {
        if (ninstances==0) return;
        int i, j;
        if (nused < 1) return;
        double best = 0;
        int ibest = 0;
        for (i=0; i<ninstances; i++) {
            if (!i || busy_time[i] < best) {
                best = busy_time[i];
                ibest = i;
            }
        }
        int inused = (int) nused;     // ignore fractional usage
        for (i=0; i<inused; i++) {
            j = (ibest + i) % ninstances;
            busy_time[j] += dur;
        }
    }

    // the overall busy time is the busy time of
    // the least busy instance
    //
    inline double get_busy_time() {
        double best = 0;
        for (int i=0; i<ninstances; i++) {
            if (!i || busy_time[i] < best) {
                best = busy_time[i];
            }
        }
        return best;
    }
};

// per-resource state
//
struct RSC_WORK_FETCH {
    int rsc_type;
    int ninstances;
    double relative_speed;   // total FLOPS relative to CPU total FLOPS
    bool has_exclusions;

    // the following used/set by rr_simulation():
    //
    double shortfall;
        // seconds of idle instances between now and now+work_buf_total()
    double nidle_now;
    double sim_nused;
    COPROC_INSTANCE_BITMAP sim_used_instances;
        // bitmap of instances used in simulation,
        // taking into account GPU exclusions
    COPROC_INSTANCE_BITMAP sim_excluded_instances;
        // bitmap of instances not used (i.e. starved because of exclusion)
    double total_fetchable_share;
        // total RS of projects from which we could fetch jobs for this device
    double saturated_time;
        // estimated time until resource is not saturated
        // used to calculate work request
    double deadline_missed_instances;
        // instance count for jobs that miss deadline
    BUSY_TIME_ESTIMATOR busy_time_estimator;
    int dont_fetch_reason;
#ifdef SIM
    double estimated_delay;
#endif
    // the following specify the work request for this resource
    //
    double req_secs;
    double req_instances;
    // REC accounting
    double secs_this_rec_interval;
    // temp in choose_project()
    PROJECT* found_project;     // a project able to ask for this work

    void init(int t, int n, double sp) {
        rsc_type = t;
        ninstances = n;
        relative_speed = sp;
        busy_time_estimator.init(n);
    }
    void rr_init();
    void update_stats(double sim_now, double dt, double buf_end);
    void update_busy_time(double dur, double nused);
    void supplement(PROJECT*);
    RSC_PROJECT_WORK_FETCH& project_state(PROJECT*);
    void print_state(const char*);
    void clear_request();
    void set_request(PROJECT*);
    void copy_request(COPROC&);
    void set_request_excluded(PROJECT*);
    bool may_have_work(PROJECT*);
    int cant_fetch(PROJECT*);
    bool backed_off(PROJECT*);
    bool uses_starved_excluded_instances(PROJECT*);
    inline void reset_rec_accounting() {
        this->secs_this_rec_interval = 0;
    }
    RSC_WORK_FETCH() {
        rsc_type = 0;
        ninstances = 0;
        relative_speed = 0;
        has_exclusions = false;
        shortfall = 0;
        nidle_now = 0;
        sim_nused = 0;
        sim_used_instances = 0;
        sim_excluded_instances = 0;
        total_fetchable_share = 0;
        saturated_time = 0;
        deadline_missed_instances = 0;
        busy_time_estimator.init(0);
        dont_fetch_reason = 0;
#ifdef SIM
        estimated_delay = 0.0;
#endif
        req_secs = 0.0;
        req_instances = 0.0;
        secs_this_rec_interval = 0.0;
        found_project = NULL;
    }
};


// per project state
//
struct PROJECT_WORK_FETCH {
    double rec;
        // recent estimated credit
    double rec_time;
        // when it was last updated
    double rec_temp;
        // temporary copy used during schedule_cpus() and work fetch
    double rec_temp_save;
        // temporary used during RR simulation
    int project_reason;
    int compute_project_reason(PROJECT*);
    int n_runnable_jobs;
    bool request_if_idle_and_uploading;
        // Set when a job finishes.
        // If we're uploading but a resource is idle, make a work request.
        // If this succeeds, clear the flag.
    PROJECT_WORK_FETCH() {
        memset(this, 0, sizeof(*this));
    }
    void reset(PROJECT*);
    void rr_init(PROJECT*);
    void print_state(PROJECT*);
};

// global work fetch state
//
struct WORK_FETCH {
    std::vector<PROJECT*> projects_sorted;
        // projects in decreasing priority order
    void setup();
    PROJECT* choose_project();
        // Find a project to ask for work.
    PROJECT* non_cpu_intensive_project_needing_work();
    void piggyback_work_request(PROJECT*);
        // we're going to contact this project anyway;
        // piggyback a work request if appropriate.
    void accumulate_inst_sec(ACTIVE_TASK*, double dt);
    void write_request(FILE*, PROJECT*);
    void handle_reply(
        PROJECT*, SCHEDULER_REPLY*, std::vector<RESULT*>new_results
    );
    void set_initial_work_request(PROJECT*);
    void set_all_requests(PROJECT*);
    void set_all_requests_hyst(PROJECT*, int rsc_type);
    void print_state();
    void init();
    void rr_init();
    void clear_request();
    void compute_shares();
    void clear_backoffs(APP_VERSION&);
    void request_string(char*, int);
    bool requested_work();
    void copy_requests();
};

extern RSC_WORK_FETCH rsc_work_fetch[MAX_RSC];
extern WORK_FETCH work_fetch;

extern void project_priority_init(bool for_work_fetch);
extern double project_priority(PROJECT*);
extern void adjust_rec_sched(RESULT*);
extern void adjust_rec_work_fetch(RESULT*);

extern double total_peak_flops();
extern const char* project_reason_string(PROJECT* p, char* buf, int len);
extern const char* rsc_project_reason_string(int);

#endif
