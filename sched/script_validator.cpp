// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2014 University of California
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

// A validator that runs scripts to check and compare results,
// so that you can do your validation in Python, PHP, Perl, bash, etc.
//
// cmdline args to this program:
// --init_script "scriptname arg1 ... argn"
// --compare_script "scriptname arg1 ... argn"
//
// The init script checks the validity of a result,
// e.g. that the output files have the proper format.
// It returns zero if the files are valid
//
// The compare script compares two results.
// If returns zero if the output files are equivalent.
//
// arg1 ... argn represent cmdline args to be passed to the scripts.
// The options for init_script are:
//
// files        list of paths of output files of the result
// result_id    result ID
// runtime      task runtime
//
// Additional options for compare_script, for the second result:
// files2       list of paths of output files
// result_id2   result ID
// runtime2     task runtime
//
// "arg1 ... argn" can be omitted,
// in which case only the output file paths are passed to the scripts.

#include <sys/param.h>

#include "validate_util2.h"
#include "error_numbers.h"
#include "boinc_db.h"
#include "sched_util.h"
#include "validate_util.h"
#include "validator.h"

using std::string;
using std::vector;

vector<string> init_script, compare_script;
    // first element is script path, other elements are args

int validate_handler_init(int argc, char** argv) {
    // handle project specific arguments here
    for (int i=1; i<argc; i++) {
        if (is_arg(argv[i], "init_script")) {
            init_script = split(argv[++i], ' ');
            if (init_script.size() == 1) {
                init_script.push_back(string("files"));
            }
        } else if (is_arg(argv[i], "compare_script")) {
            compare_script = split(argv[++i], ' ');
            if (compare_script.size() == 1) {
                compare_script.push_back("files");
                compare_script.push_back("files2");
            }
        }
    }

    if (!init_script.size() || !compare_script.size()) {
        log_messages.printf(MSG_CRITICAL,
            "init_script and/or compare_script names are missing from command line\n"
        );
        return 1;
    }
    return 0;
}

void validate_handler_usage() {
    // describe the project specific arguments here
    fprintf(stderr,
        "  A validator that runs scripts to check and compare results, \n"
        "  so that you can do your validation in Python, PHP, Perl, bash, etc.\n"
        "    Custom options:\n"
        "    --init_script \"scriptname arg1 ... argn\"    checks the validity of a task,\n"
        "        e.g. that the output files have the proper format. Needs to exit with zero if the files are valid.\n"
        "    --compare_script \"scriptname arg1 ... argn\" compares two tasks. \n"
        "        Needs to return zero if the output files are equivalent.\n"
        "    See script_validator.cpp for more usage information.\n"
    );
}

int init_result(RESULT& result, void*&) {
    unsigned int i, j;
    char buf[256];

    vector<string> paths;
    int retval;
    retval = get_output_file_paths(result, paths);
    if (retval) {
        fprintf(stderr, "get_output_file_paths() returned %d\n", retval);
        return retval;
    }

    if (init_script.size() == 0) {
        fprintf(stderr, "init_result() failed: init_script parameter was not specified\n");
        return 1;
    }

    char cmd[4096];
    sprintf(cmd, "../bin/%s", init_script[0].c_str());
    for (i=1; i<init_script.size(); i++) {
        string& s = init_script[i];
        if (s == "files") {
            for (j=0; j<paths.size(); j++) {
                strcat(cmd, " ");
                strcat(cmd, paths[j].c_str());
            }
        } else if (s == "runtime") {
            sprintf(buf, " %f", result.elapsed_time);
            strcat(cmd, buf);
        } else if (s == "result_id") {
            sprintf(buf, " %lu", result.id);
            strcat(cmd, buf);
        }
    }
    retval = system(cmd);
    if (retval) {
        return retval;
    }
    return 0;
}

int compare_results(RESULT& r1, void*, RESULT const& r2, void*, bool& match) {
    unsigned int i, j;
    char buf[256];

    vector<string> paths1, paths2;
    int retval;
    retval = get_output_file_paths(r1, paths1);
    if (retval) {
        fprintf(stderr, "get_output_file_paths() returned %d\n", retval);
        return retval;
    }
    retval = get_output_file_paths(r2, paths2);
    if (retval) {
        fprintf(stderr, "get_output_file_paths() returned %d\n", retval);
        return retval;
    }

    if (compare_script.size() == 0) {
        fprintf(stderr, "compare_results() failed: compare_script parameter was not specified\n");
        return 1;
    }

    char cmd[4096];
    sprintf(cmd, "../bin/%s", compare_script[0].c_str());
    for (i=1; i<compare_script.size(); i++) {
        string& s = compare_script[i];
        if (s == "files") {
            for (j=0; j<paths1.size(); j++) {
                strcat(cmd, " ");
                strcat(cmd, paths1[j].c_str());
            }
        } else if (s == "files2") {
            for (j=0; j<paths2.size(); j++) {
                strcat(cmd, " ");
                strcat(cmd, paths2[j].c_str());
            }
        } else if (s == "runtime") {
            sprintf(buf, " %f", r1.elapsed_time);
            strcat(cmd, buf);
        } else if (s == "result_id") {
            sprintf(buf, " %lu", r1.id);
            strcat(cmd, buf);
        } else if (s == "runtime2") {
            sprintf(buf, " %f", r2.elapsed_time);
            strcat(cmd, buf);
        } else if (s == "result_id2") {
            sprintf(buf, " %lu", r2.id);
            strcat(cmd, buf);
        }
    }
    retval = system(cmd);
    if (retval) {
        match = false;
    } else {
        match = true;
    }
    return 0;
}

int cleanup_result(RESULT const&, void*) {
    return 0;
}

