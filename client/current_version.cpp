// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2018 University of California
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

// Stuff related to the mechanism where the client fetches
// http://boinc.berkeley.edu/download.php?xml=1
// every so often to see if there's a newer client version

#include "filesys.h"
#include "str_replace.h"

#include "client_msgs.h"
#include "client_state.h"
#include "file_names.h"

#include "current_version.h"

NVC_CONFIG nvc_config;

NVC_CONFIG::NVC_CONFIG() {
    defaults();
}

// this is called first thing by client right after CC_CONFIG::defaults()
//
void NVC_CONFIG::defaults() {
    client_download_url = "https://boinc.berkeley.edu/download.php";
    client_new_version_name = "";
    client_version_check_url = DEFAULT_VERSION_CHECK_URL;
    network_test_url = "https://www.google.com/";
};

int NVC_CONFIG::parse(FILE* f) {
    MIOFILE mf;
    XML_PARSER xp(&mf);

    mf.init_file(f);
    if (!xp.parse_start("nvc_config")) {
        msg_printf_notice(NULL, false,
            "https://boinc.berkeley.edu/manager_links.php?target=notice&controlid=config",
            "%s",
            _("Missing start tag in nvc_config.xml")
        );
        return ERR_XML_PARSE;
    }
    while (!xp.get_tag()) {
        if (!xp.is_tag) {
            msg_printf_notice(NULL, false,
                "https://boinc.berkeley.edu/manager_links.php?target=notice&controlid=config",
                "%s: %s",
                _("Unexpected text in nvc_config.xml"),
                xp.parsed_tag
            );
            continue;
        }
        if (xp.match_tag("/nvc_config")) {
            notices.remove_notices(NULL, REMOVE_CONFIG_MSG);
            return 0;
        }
        if (xp.parse_string("client_download_url", client_download_url)) {
            downcase_string(client_download_url);
            continue;
        }
        if (xp.parse_string("client_new_version_name", client_new_version_name)) {
            continue;
        }
        if (xp.parse_string("client_version_check_url", client_version_check_url)) {
            downcase_string(client_version_check_url);
            continue;
        }
        if (xp.parse_string("network_test_url", network_test_url)) {
            downcase_string(network_test_url);
            continue;
        }
        msg_printf_notice(NULL, false,
            "https://boinc.berkeley.edu/manager_links.php?target=notice&controlid=config",
            "%s: <%s>",
            _("Unrecognized tag in nvc_config.xml"),
            xp.parsed_tag
        );
        xp.skip_unexpected(true, "NVC_CONFIG.parse");
    }
    msg_printf_notice(NULL, false,
        "https://boinc.berkeley.edu/manager_links.php?target=notice&controlid=config",
        "%s",
        _("Missing end tag in nvc_config.xml")
    );
    return ERR_XML_PARSE;
}

int read_vc_config_file() {
    nvc_config.defaults();
    FILE* f = boinc_fopen(NVC_CONFIG_FILE, "r");
    if (!f) {
        return ERR_FOPEN;
    }
    nvc_config.parse(f);
    fclose(f);
    return 0;
}

int GET_CURRENT_VERSION_OP::do_rpc() {
    int retval;

    retval = gui_http->do_rpc(
        this, nvc_config.client_version_check_url.c_str(),
        GET_CURRENT_VERSION_FILENAME,
        true
    );
    if (retval) {
        error_num = retval;
    } else {
        error_num = ERR_IN_PROGRESS;
    }
    return retval;
}

static bool is_version_newer(const char* p) {
    int maj=0, min=0, rel=0;

    sscanf(p, "%d.%d.%d", &maj, &min, &rel);
    if (maj > gstate.core_client_version.major) return true;
    if (maj < gstate.core_client_version.major) return false;
    if (min > gstate.core_client_version.minor) return true;
    if (min < gstate.core_client_version.minor) return false;
    if (rel > gstate.core_client_version.release) return true;
    return false;
}

// Parse the output of download.php?xml=1.
// If there is a newer version for our primary platform,
// copy it to new_version and return true.
//
static bool parse_version(FILE* f, char* new_version, int len) {
    char buf2[256];
    bool same_platform = false, newer_version_exists = false;

    MIOFILE mf;
    XML_PARSER xp(&mf);
    mf.init_file(f);

    while (!xp.get_tag()) {
        if (xp.match_tag("/version")) {
            return (same_platform && newer_version_exists);
        }
        if (xp.parse_str("dbplatform", buf2, sizeof(buf2))) {
            same_platform = (strcmp(buf2, gstate.get_primary_platform())==0);
        }
        if (xp.parse_str("version_num", buf2, sizeof(buf2))) {
            newer_version_exists = is_version_newer(buf2);
            strlcpy(new_version, buf2, len);
        }
    }
    return false;
}

static void show_newer_version_msg(const char* new_vers) {
    char buf[1024];

    if (nvc_config.client_new_version_name.empty()) {
        msg_printf_notice(0, true,
            "https://boinc.berkeley.edu/manager_links.php?target=notice&controlid=download",
            "%s (%s). <a href=%s>%s</a>",
            _("A new version of BOINC is available"),
            new_vers,
            nvc_config.client_download_url.c_str(),
            _("Download")
        );
    } else {
        snprintf(buf, sizeof(buf), _("A new version of %s is available"), 
            nvc_config.client_new_version_name.c_str()
        );
        msg_printf_notice(0, true, NULL,
            "%s (%s). <a href=%s>%s</a>",
            buf,
            new_vers,
            nvc_config.client_download_url.c_str(),
            _("Download")
        );
    }
}

void GET_CURRENT_VERSION_OP::handle_reply(int http_op_retval) {
    char buf[256], new_version[256];
    if (http_op_retval) {
        error_num = http_op_retval;
        return;
    }
    gstate.new_version_check_time = gstate.now;
    FILE* f = boinc_fopen(GET_CURRENT_VERSION_FILENAME, "r");
    if (!f) return;
    while (fgets(buf, 256, f)) {
        if (match_tag(buf, "<version>")) {
            if (parse_version(f, new_version, sizeof(new_version))) {
                show_newer_version_msg(new_version);
                gstate.newer_version = string(new_version);
                break;
            }
        }
    }
    fclose(f);
}

// called at startup to see if the client state file
// says there's a new version. This must be called after
// read_vc_config_file()
//
void newer_version_startup_check() {
    // If version check URL has changed (perhaps due to installing a build of
    // BOINC with different branding), reset any past new version information
    //
    if (gstate.client_version_check_url != nvc_config.client_version_check_url) {
        gstate.client_version_check_url = nvc_config.client_version_check_url;
        gstate.newer_version = "";
        return;
    }

    if (!gstate.newer_version.empty()) {
        if (is_version_newer(gstate.newer_version.c_str())) {
            show_newer_version_msg(gstate.newer_version.c_str());
        } else {
            gstate.newer_version = "";
        }
    }
}

#define NEW_VERSION_CHECK_PERIOD (14*86400)

void CLIENT_STATE::new_version_check(bool force) {
    if (force || (new_version_check_time == 0) ||
        (now - new_version_check_time > NEW_VERSION_CHECK_PERIOD)) {
            // get_current_version_op.handle_reply()
            // updates new_version_check_time
            //
            get_current_version_op.do_rpc();
        }
}

