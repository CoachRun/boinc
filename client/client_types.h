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

// If you change anything, make sure you also change:
// client_types.C         (to write and parse it)
// client_state.C  (to cross-link objects)
//

#ifndef BOINC_CLIENT_TYPES_H
#define BOINC_CLIENT_TYPES_H

#include "cpp.h"

#if !defined(_WIN32) || defined(__CYGWIN32__)
#include <cstdio>
#include <sys/time.h>
#include <sys/param.h>
#endif

#include "cc_config.h"
#include "str_replace.h"
#include "common_defs.h"
#include "coproc.h"
#include "cert_sig.h"
#include "filesys.h"
#include "hostinfo.h"
#include "keyword.h"
#include "md5_file.h"
#include "miofile.h"

#include "cs_notice.h"
#include "cs_trickle.h"
#include "rr_sim.h"
#include "work_fetch.h"

#ifdef SIM
#include "sim.h"
#endif

#define MAX_FILE_INFO_LEN   4096
#define MAX_SIGNATURE_LEN   4096
#define MAX_KEY_LEN         4096

#define MAX_COPROCS_PER_JOB 8
    // max # of instances of a GPU that a job can use

extern int rsc_index(const char*);
extern const char* rsc_name(int);
extern const char* rsc_name_long(int);
extern COPROCS coprocs;

struct FILE_INFO;
struct ASYNC_VERIFY;

// represents a list of URLs (e.g. to download a file)
// and a current position in that list
//
struct URL_LIST {
    std::vector<std::string> urls;
    int start_index;
    int current_index;

    URL_LIST() {
        clear();
    }

    void clear() {
        urls.clear();
        start_index = -1;
        current_index = -1;
    }
    bool empty() {return urls.empty();}
    const char* get_init_url();
    const char* get_next_url();
    const char* get_current_url(FILE_INFO&);
    inline void add(std::string url) {
        urls.push_back(url);
    }
    void replace(URL_LIST& ul) {
        clear();
        for (unsigned int i=0; i<ul.urls.size(); i++) {
            add(ul.urls[i]);
        }
    }
};

struct FILE_INFO {
    char name[256];
    char md5_cksum[MD5_LEN];
    double max_nbytes;
    double nbytes;
    double gzipped_nbytes;  // defined if download_gzipped is true
    double upload_offset;
    int status;             // see above
    bool executable;        // change file protections to make executable
    bool uploaded;          // file has been uploaded
    bool sticky;            // don't delete unless instructed to do so
    double sticky_lifetime;
        // how long file should stay sticky.
        // passed from the server;
        // used by client to calculate sticky_expire_time.
    double sticky_expire_time;
        // if nonzero, when sticky status expires
    bool signature_required;    // true iff associated with app version
    bool is_user_file;
    bool is_project_file;
    bool is_auto_update_file;
    bool anonymous_platform_file;
    bool gzip_when_done;
        // for output files: gzip file when done, and append .gz to its name
    class PERS_FILE_XFER* pers_file_xfer;
        // nonzero if in the process of being up/downloaded
    RESULT* result;
        // for upload files (to authenticate)
    PROJECT* project;
    int ref_cnt;
    URL_LIST download_urls;
    URL_LIST upload_urls;
    bool download_gzipped;
        // if set, download NAME.gz and gunzip it to NAME
    char xml_signature[MAX_SIGNATURE_LEN];
        // the upload signature
    char file_signature[MAX_SIGNATURE_LEN];
        // if the file itself is signed (for executable files)
        // this is the signature
    std::string error_msg;
        // if permanent error occurs during file xfer, it's recorded here
    CERT_SIGS* cert_sigs;
    ASYNC_VERIFY* async_verify;

    FILE_INFO();
    ~FILE_INFO();
    void reset();
    int set_permissions(const char* path=0);
    int parse(XML_PARSER&);
    int write(MIOFILE&, bool to_server);
    int write_gui(MIOFILE&);
    int delete_file();
        // attempt to delete the underlying file
    bool had_failure(int& failnum);
    void failure_message(std::string&);
    int merge_info(FILE_INFO&);
    int verify_file(bool, bool, bool);
    bool verify_file_certs();
    int gzip();
        // gzip file and add .gz to name
    int gunzip(char*);
        // unzip file and remove .gz from filename.
        // optionally compute MD5 also
    inline bool uploadable() {
        return !upload_urls.empty();
    }
    inline bool downloadable() {
        return !download_urls.empty();
    }
    inline URL_LIST& get_url_list(bool is_upload) {
        return is_upload?upload_urls:download_urls;
    }
};

// Describes a connection between a file and a workunit, result, or app version
//
struct FILE_REF {
    char file_name[256];
        // physical name
    char open_name[256];
        // logical name
    bool main_program;
    FILE_INFO* file_info;
    bool copy_file;
        // if true, core client will copy the file instead of linking
    bool optional;
        // for output files: app may not generate file;
        // don't treat as error if file is missing.
    int parse(XML_PARSER&);
    int write(MIOFILE&);
};

// file xfer backoff state for a project and direction (up/down)
// if file_xfer_failures exceeds FILE_XFER_FAILURE_LIMIT,
// we switch from a per-file to a project-wide backoff policy
// (separately for the up/down directions)
// NOTE: this refers to transient failures, not permanent.
//
#define FILE_XFER_FAILURE_LIMIT 3
struct FILE_XFER_BACKOFF {
    int file_xfer_failures;
        // count of consecutive failures
    double next_xfer_time;
        // when to start trying again
    bool ok_to_transfer();
    void file_xfer_failed(PROJECT*);
    void file_xfer_succeeded();

    FILE_XFER_BACKOFF() {
        file_xfer_failures = 0;
        next_xfer_time = 0;
    }

    // clear backoff but maintain failure count;
    // called when network becomes available
    //
    void clear_temporary() {
        next_xfer_time = 0;
    }
};

// statistics at a specific day

struct DAILY_STATS {
    double user_total_credit;
    double user_expavg_credit;
    double host_total_credit;
    double host_expavg_credit;
    double day;

    void clear();
    DAILY_STATS() {clear();}
    int parse(FILE*);
};
bool operator < (const DAILY_STATS&, const DAILY_STATS&);

// base class for PROJECT and ACCT_MGR_INFO
//
struct PROJ_AM {
    char master_url[256];
    char project_name[256];
        // descriptive.  not unique
    std::vector<RSS_FEED> proj_feeds;
    inline char *get_project_name() {
        if (strlen(project_name)) {
            return project_name;
        } else {
            return master_url;
        }
    }
};

struct APP {
    char name[256];
    char user_friendly_name[256];
    bool non_cpu_intensive;
    bool fraction_done_exact;
    PROJECT* project;
    bool report_results_immediately;
    int max_concurrent;
        // Limit on # of concurrent jobs of this app; 0 if none
        // Specified in app_config.xml
        // Can also specify in client_state.xml (for client emulator)
    int n_concurrent;
        // temp during job scheduling, to enforce max_concurrent
    COPROC_INSTANCE_BITMAP non_excluded_instances[MAX_RSC];
        // for each resource type, bitmap of the non-excluded instances
#ifdef SIM
    double latency_bound;
    double fpops_est;
    NORMAL_DIST fpops;
    NORMAL_DIST checkpoint_period;
    double working_set;
    double weight;
    bool ignore;
#endif

    APP() {memset(this, 0, sizeof(APP));}
    int parse(XML_PARSER&);
    int write(MIOFILE&);
};

struct GPU_USAGE {
    int rsc_type;   // index into COPROCS array
    double usage;
};

struct APP_VERSION {
    char app_name[256];
    int version_num;
    char platform[256];
    char plan_class[64];
    char api_version[16];
    double avg_ncpus;
    GPU_USAGE gpu_usage;    // can only use 1 GPU type
    double gpu_ram;
    double flops;
    char cmdline[256];
        // additional cmdline args
    char file_prefix[256];
        // prepend this to input/output file logical names
        // (e.g. "share" for VM apps)
    bool needs_network;

    APP* app;
    PROJECT* project;
    std::vector<FILE_REF> app_files;
    int ref_cnt;
    char graphics_exec_path[MAXPATHLEN];
    char graphics_exec_file[256];
    double max_working_set_size;
        // max working set of tasks using this app version.
        // unstarted jobs using this app version are assumed
        // to use this much RAM,
        // so that we don't run a long sequence of jobs,
        // each of which turns out not to fit in available RAM
    bool missing_coproc;
    double missing_coproc_usage;
    char missing_coproc_name[256];
    bool dont_throttle;
        // jobs of this app version are exempt from CPU throttling
        // Set for coprocessor apps
    bool is_vm_app;
        // currently this set if plan class includes "vbox" (kludge)
    bool is_wrapper;
        // the main program is a wrapper; run it above idle priority

    int index;  // temp var for make_scheduler_request()
#ifdef SIM
    bool dont_use;
#endif

    APP_VERSION() {
        init();
    }
    ~APP_VERSION(){}
    void init();
    int parse(XML_PARSER&);
    int write(MIOFILE&, bool write_file_info = true);
    bool had_download_failure(int& failnum);
    void get_file_errors(std::string&);
    void clear_errors();
    bool api_version_at_least(int major, int minor);
    inline bool uses_coproc(int rt) {
        return (gpu_usage.rsc_type == rt);
    }
    inline int rsc_type() {
        return gpu_usage.rsc_type;
    }
    inline bool is_opencl() {
        return (strstr(plan_class, "opencl") != NULL);
    }
};

struct WORKUNIT {
    char name[256];
    char app_name[256];
    int version_num;
        // Deprecated, but need to keep around to let people revert
        // to versions before multi-platform support
    std::string command_line;
    std::vector<FILE_REF> input_files;
    PROJECT* project;
    APP* app;
    int ref_cnt;
    double rsc_fpops_est;
    double rsc_fpops_bound;
    double rsc_memory_bound;
    double rsc_disk_bound;
    JOB_KEYWORD_IDS job_keyword_ids;

    WORKUNIT(){
        safe_strcpy(name, "");
        safe_strcpy(app_name, "");
        version_num = 0;
        command_line = "";
        input_files.clear();
        job_keyword_ids.clear();
        project = NULL;
        app = NULL;
        ref_cnt = 0;
        rsc_fpops_est = 0.0;
        rsc_fpops_bound = 0.0;
        rsc_memory_bound = 0.0;
        rsc_disk_bound = 0.0;
    }
    ~WORKUNIT(){}
    int parse(XML_PARSER&);
    int write(MIOFILE&, bool gui);
    bool had_download_failure(int& failnum);
    void get_file_errors(std::string&);
    void clear_errors();
};

// represents an always/auto/never value, possibly temporarily overridden

struct RUN_MODE {
    int perm_mode;
    int temp_mode;
    int prev_mode;
    double temp_timeout;
    RUN_MODE();
    void set(int mode, double duration);
    void set_prev(int mode);
    int get_perm();
    int get_prev();
    int get_current();
    double delay();
};

// a platform supported by the client.

struct PLATFORM {
    std::string name;
};

extern int parse_project_files(XML_PARSER&, std::vector<FILE_REF>&);

#endif
