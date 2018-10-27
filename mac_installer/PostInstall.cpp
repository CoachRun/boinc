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

/* PostInstall.cpp */

// Notes on command-line installation to a remote Mac:
//
// When the installer is run from the Finder, this Postinstall.app will 
// display up to two dialogs, asking the user whether or not to:
//  [1] allow non-administrative users to run the BOINC Manager
//      (asked only if this Mac has any non-administrative users)
//  [2] set BOINC as the screensaver for all users who can run BOINC 
//      (asked only if BOINC screensaver is not already set for them)
//
// The installer can also be run from the command line.  This is useful 
//  for installation on remote Macs.  However, there is no way to respond 
//  to dialogs during a command-line install.
//
// Apple's command-line installer sets the following environment variable: 
//     COMMAND_LINE_INSTALL=1
// The postinstall script, postupgrade script, and this Postinstall.app 
//   detect this environment variable and do the following:
//  * Redirect the Postinstall.app log output to a file 
//      /tmp/BOINCInstallLog.txt
//  * Suppress the 2 dialogs
//  * test for the existence of a file /tmp/nonadminusersok.txt; if the 
//     file exists, allow non-administrative users to run BOINC Manager
//  * test for the existence of a file /tmp/setboincsaver.txt; if the 
//     file exists, set BOINC as the screensaver for all BOINC users. 
//
// The BOINC installer package to be used for command line installs can
// be found embedded inside the GUI BOINC Installer application at:
// "..../BOINC Installer.app/Contents/Resources/BOINC.pkg"
//
// Example: To install on a remote Mac from the command line, allowing 
//   non-admin users to run the BOINC Manager and setting BOINC as the 
//   screensaver:
//  * First SCP the "BOINC.pkg" to the remote Mac's /tmp
//     directory, then SSh into the remote Mac and enter the following
//  $ touch /tmp/nonadminusersok.txt
//  $ touch /tmp/setboincsaver.txt
//  $ sudo installer -pkg /tmp/BOINC.pkg -tgt /
//  $ sudo reboot
//

#define VERBOSE_TEST 0  /* for debugging callPosixSpawn */
#if VERBOSE_TEST
#define CREATE_LOG 1    /* for debugging */
#else
#define CREATE_LOG 0    /* for debugging */
#endif

#define USE_OSASCRIPT_FOR_ALL_LOGGED_IN_USERS false

#include <Carbon/Carbon.h>
#include <grp.h>

#include <unistd.h>	// getlogin
#include <sys/types.h>	// getpwname, getpwuid, getuid
#include <pwd.h>	// getpwname, getpwuid, getuid
#include <grp.h>        // getgrnam
#include <sys/wait.h>	// waitpid
#include <dirent.h>
#include <sys/param.h>  // for MAXPATHLEN
#include <sys/stat.h>   // for chmod
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <cerrno>
#include <time.h>       // for time()
#include <vector>
#include <string>
#define DLOPEN_NO_WARN
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include "url.h"

#include "mac_branding.h"

using std::vector;
using std::string;

#include "mac_util.h"
#include "SetupSecurity.h"
#include "translate.h"
#include "file_names.h"
#include "util.h"


#define admin_group_name "admin"
#define boinc_master_user_name "boinc_master"
#define boinc_master_group_name "boinc_master"
#define boinc_project_user_name "boinc_project"
#define boinc_project_group_name "boinc_project"


OSErr Initialize(void);	/* function prototypes */
Boolean myFilterProc(DialogRef theDialog, EventRecord *theEvent, DialogItemIndex *itemHit);
int DeleteReceipt(void);
Boolean IsRestartNeeded();
void CheckUserAndGroupConflicts();
Boolean SetLoginItemOSAScript(long brandID, Boolean deleteLogInItem, char *userName);
Boolean SetLoginItemLaunchAgent(long brandID, long oldBrandID, Boolean deleteLogInItem, passwd *pw);
OSErr GetCurrentScreenSaverSelection(passwd *pw, char *moduleName, size_t maxLen);
OSErr SetScreenSaverSelection(char *moduleName, char *modulePath, int type);
void SetSkinInUserPrefs(char *userName, char *nameOfSkin);
Boolean CheckDeleteFile(char *name);
static void FixLaunchServicesDataBase(uid_t userID, long brandID);
void SetEUIDBackToUser (void);
static char * PersistentFGets(char *buf, size_t buflen, FILE *f);
static void LoadPreferredLanguages();
static Boolean ShowMessage(Boolean askYesNo, const char *format, ...);
Boolean IsUserMemberOfGroup(const char *userName, const char *groupName);
int CountGroupMembershipEntries(const char *userName, const char *groupName);
OSErr UpdateAllVisibleUsers(long brandID, long oldBrandID);
static Boolean IsUserLoggedIn(const char *userName);
void FindAllVisibleUsers(void);
long GetBrandID(char *path);
int TestRPCBind(void);
pid_t FindProcessPID(char* name, pid_t thePID);
static void SleepSeconds(double seconds);
static OSErr QuitAppleEventHandler(const AppleEvent *appleEvt, AppleEvent* reply, UInt32 refcon);
int callPosixSpawn(const char *cmd);
void print_to_log(const char *format, ...);
void strip_cr(char *buf);
void CopyPreviousErrorsToLog(void);

extern int check_security(
    char *bundlePath, char *dataPath, 
    int use_sandbox, int isManager, 
    char* path_to_error, int len
);

/* BEGIN TEMPORARY ITEMS TO ALLOW TRANSLATORS TO START WORK */
void notused() {
    ShowMessage(true, (char *)_("Yes"));
    ShowMessage(true, (char *)_("No"));
    // Future feature
    ShowMessage(true, (char *)_("Should BOINC run even when no user is logged in?"));
}
/* END TEMPORARY ITEMS TO ALLOW TRANSLATORS TO START WORK */

#define MAX_LANGUAGES_TO_TRY 5

static char * Catalog_Name = (char *)"BOINC-Setup";
static char * Catalogs_Dir = (char *)"/Library/Application Support/BOINC Data/locale/";

#define REPORT_ERROR(isError) if (isError) print_to_log("BOINC PostInstall error at line %d", __LINE__);

/* globals */
static Boolean                  gCommandLineInstall = false;
static Boolean                  gQuitFlag = false;
static Boolean                  currentUserCanRunBOINC = false;
static char                     loginName[256];
static char                     tempDirName[MAXPATHLEN];
static time_t                   waitPermissionsStartTime;
static vector<string>           human_user_names;
static vector<uid_t>            human_user_IDs;


enum { launchWhenDone,
        logoutRequired,
        restartRequired,
        nothingrequired
    };

/******************************************************************
***                                                             ***
***                         NOTE:                               ***
***                                                             ***
*** On entry, the postinstall or postupgrade script has set the ***
*** current directory to the top level of our installer package ***
***                                                             ***
******************************************************************/

int main(int argc, char *argv[])
{
    Boolean                 Success;
    long                    brandID = 0;
    long                    oldBrandID = 0;
    int                     i;
    pid_t                   installerPID = 0, coreClientPID = 0;
    OSStatus                err;
    FILE                    *f;
    char                    s[2048];
    char                    path[MAXPATHLEN];
    
#ifndef SANDBOX
    group                   *grp;
#endif  // SANDBOX

    printf("\nStarting PostInstall app %s\n\n", argv[1]);
    fflush(stdout);

    if (!check_branding_arrays(s, sizeof(s))) {
        ShowMessage(false, (char *)_("Branding array has too few entries: %s"), s);
        return -1;
    }

    // getlogin() gives unreliable results under OS 10.6.2, so use environment
    strncpy(loginName, getenv("USER"), sizeof(loginName)-1);
    if (loginName[0] == '\0') {
        ShowMessage(false, (char *)_("Could not get user login name"));
        return 0;
    }

    printf("login name = %s\n", loginName);
    fflush(stdout);

    snprintf(tempDirName, sizeof(tempDirName), "InstallBOINC-%s", loginName);
    
    CopyPreviousErrorsToLog();

    if (getenv("COMMAND_LINE_INSTALL") != NULL) {
        gCommandLineInstall = true;
        puts("command-line install\n");
        fflush(stdout);
    }

    for (i=0; i<argc; i++) {
        if (strcmp(argv[i], "-part2") == 0)
            return DeleteReceipt();
    }

    if (Initialize() != noErr) {
        REPORT_ERROR(true);
        return 0;
    }

    for (i=0; i< NUMBRANDS; i++) {
        snprintf(s, sizeof(s), "killall \"%s\"", appName[i]);
        callPosixSpawn (s);
    }
    sleep(2);

    // Core Client may still be running if it was started without Manager
    coreClientPID = FindProcessPID("boinc", 0);
    if (coreClientPID)
        kill(coreClientPID, SIGTERM);   // boinc catches SIGTERM & exits gracefully

    installerPID = getPidIfRunning("com.apple.installer");

    // BOINC Installer.app wrote a file to tell us the previously installed branding, if any
    snprintf(s, sizeof(s), "/tmp/%s/OldBranding", tempDirName);
    oldBrandID = GetBrandID(s);
    printf("oldBrandID = %ld\n", oldBrandID);    
    fflush(stdout);

    // The new branding (if any) is in the resources of this PostInstall.app
    getPathToThisApp(s, sizeof(s));
    strncat(s, "/Contents/Resources/Branding", sizeof(s)-1);
    printf("path to new BrandID = %s\n", s);    
    fflush(stdout);
    brandID = GetBrandID(s);
    printf("new BrandID = %ld\n", brandID);    
    fflush(stdout);

    LoadPreferredLanguages();

    if (compareOSVersionTo(10, 6) < 0) {
        BringAppToFront();
        // Remove everything we've installed
        // "\pSorry, this version of GridRepublic requires system 10.6 or higher."
        ShowMessage(false, "Sorry, this version of %s requires system 10.6 or higher.", brandName[brandID]);

        // "rm -rf \"/Applications/GridRepublic Desktop.app\""
        sprintf(s, "rm -rf \"%s\"", appPath[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
        
        // "rm -rf \"/Library/Screen Savers/GridRepublic.saver\""
        sprintf(s, "rm -rf \"/Library/Screen Savers/%s.saver\"", saverName[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
        
        // "rm -rf /Library/Receipts/GridRepublic.pkg"
        sprintf(s, "rm -rf \"%s\"", receiptName[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);

        // We don't customize BOINC Data directory name for branding
        err = callPosixSpawn ("rm -rf \"/Library/Application Support/BOINC Data\"");
        REPORT_ERROR(err);

        err = kill(installerPID, SIGKILL);

        return 0;
    }
    
    sleep (2);

    // OS 10.6 and OS10.7 require screensavers built with Garbage Collection, but
    // Xcode 5.0.2 was the last version of Xcode which supported building with
    // Garbage Collection, so we have saved the screensaver executable with GC as
    // a binary. The installer build script added it to the screen saver, which
    // now contains both the code with Garbage Collection and the code with
    // Automatic Reference Counting. Determine the correct one to use for this
    // version of OS X and remove the other one.
    if (compareOSVersionTo(10, 8) < 0) {
        // "rm -rf \"/Library/Screen Savers/GridRepublic.saver/Contents/MacOS/BOINCSaver\""
        sprintf(s, "rm -f \"/Library/Screen Savers/%s.saver/Contents/MacOS/%s\"", saverName[brandID], saverName[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
        sprintf(s, "mv -f \"/Library/Screen Savers/%s.saver/Contents/MacOS/BOINCSaver_MacOS10_6_7\" \"/Library/Screen Savers/%s.saver/Contents/MacOS/%s\"",
            saverName[brandID], saverName[brandID], saverName[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
    } else {
        // "rm -rf \"/Library/Screen Savers/GridRepublic.saver/Contents/MacOS/BOINCSaver_MacOS10_6_7\""
        sprintf(s, "rm -f \"/Library/Screen Savers/%s.saver/Contents/MacOS/BOINCSaver_MacOS10_6_7\"", saverName[brandID]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
    }

    // Install all_projects_list.xml file, but only if one doesn't 
    // already exist, since a pre-existing one is probably newer.
    f = fopen("/Library/Application Support/BOINC Data/all_projects_list.xml", "r");
    if (f) {
        fclose(f);      // Already exists
        unlink("/Library/Application Support/BOINC Data/installer_projects_list.xml");
    } else {
        unlink("/Library/Application Support/BOINC Data/all_projects_list.xml");
        rename("/Library/Application Support/BOINC Data/installer_projects_list.xml",
                "/Library/Application Support/BOINC Data/all_projects_list.xml");
    }
    
    // BOINC Installer.app wrote the account data file (if any) in temp dirctory
    // Copy it into the BOINC Data directory if it exists
    snprintf(s, sizeof(s), "mv -f \"/tmp/%s/%s\" \"/Library/Application Support/BOINC Data/\"",
        tempDirName, ACCOUNT_DATA_FILENAME);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);
    
    Success = false;
    
#ifdef SANDBOX

#define RETRY_LIMIT 5

    CheckUserAndGroupConflicts();
    for (i=0; i<RETRY_LIMIT; ++i) {
        err = CreateBOINCUsersAndGroups();
        if (err != noErr) {
            printf("CreateBOINCUsersAndGroups returned %d (repetition=%d)", err, i);
            fflush(stdout);
            REPORT_ERROR(i >= RETRY_LIMIT);
            continue;
        }

        // err = SetBOINCAppOwnersGroupsAndPermissions("/Applications/GridRepublic Desktop.app");
        err = SetBOINCAppOwnersGroupsAndPermissions(appPath[brandID]);
        
        if (err != noErr) {
            printf("SetBOINCAppOwnersGroupsAndPermissions returned %d (repetition=%d)", err, i);
            fflush(stdout);
            REPORT_ERROR(i >= RETRY_LIMIT);
            continue;
        }

        err = SetBOINCDataOwnersGroupsAndPermissions();
        if (err != noErr) {
            printf("SetBOINCDataOwnersGroupsAndPermissions returned %d (repetition=%d)", err, i);
            fflush(stdout);
            REPORT_ERROR(i >= RETRY_LIMIT);
            continue;
        }
        
        err = check_security(
            appPath[brandID],
            "/Library/Application Support/BOINC Data",
            true, false, NULL, 0
        );
        if (err != noErr) {
            printf("check_security returned %d (repetition=%d)", err, i);
            fflush(stdout);
            REPORT_ERROR(i >= RETRY_LIMIT);
        } else {
            break;
        }
    }
    
#else   // ! defined(SANDBOX)

    // The BOINC Manager and Core Client have the set-user-ID-on-execution 
    // flag set, so their ownership is important and must match the 
    // ownership of the BOINC Data directory.
    
    // Find an appropriate admin user to set as owner of installed files
    // First, try the user currently logged in
    grp = getgrnam(admin_group_name);
    i = 0;
    while ((p = grp->gr_mem[i]) != NULL) {   // Step through all users in group admin
        if (strcmp(p, loginName) == 0) {
            Success = true;     // Logged in user is a member of group admin
            break;
        }
        ++i;
    }
    
    // If currently logged in user is not admin, use first non-root admin user
    if (!Success) {
        i = 0;
        while ((p = grp->gr_mem[i]) != NULL) {   // Step through all users in group admin
            if (strcmp(p, "root") != 0)
                break;
            ++i;
        }
    }

    // Set owner of branded BOINCManager and contents, including core client
    // "chown -Rf username \"/Applications/GridRepublic Desktop.app\""
    sprintf(s, "chown -Rf %s \"%s\"", p, appPath[brandID]);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);

    // Set owner of BOINC Screen Saver
    // "chown -Rf username \"/Library/Screen Savers/GridRepublic.saver\""
    sprintf(s, "chown -Rf %s \"/Library/Screen Savers/%s.saver\"", p, saverName[brandID]);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);

    //  We don't customize BOINC Data directory name for branding
    // "chown -Rf username \"/Library/Application Support/BOINC Data\""
    sprintf(s, "chown -Rf %s \"/Library/Application Support/BOINC Data\"", p);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);

    // "chmod -R a+s \"/Applications/GridRepublic Desktop.app\""
    sprintf(s, "chmod -R a+s \"%s\"", appPath[brandID]);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);

#endif   // ! defined(SANDBOX)

    // Remove any branded versions of BOINC other than ours (i.e., old versions) 
    for (i=0; i< NUMBRANDS; i++) {
        if (i == brandID) continue;
        
        // "rm -rf \"/Applications/GridRepublic Desktop.app\""
        sprintf(s, "rm -rf \"%s\"", appPath[i]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
        
        // "rm -rf \"/Library/Screen Savers/GridRepublic.saver\""
        sprintf(s, "rm -rf \"/Library/Screen Savers/%s.saver\"", saverName[i]);
        err = callPosixSpawn (s);
        REPORT_ERROR(err);
    }
    
   if (brandID == 0) {  // Installing generic BOINC
        err = callPosixSpawn ("rm -f \"/Library/Application Support/BOINC Data/Branding\"");
        REPORT_ERROR(err);
    }
    
    CFStringRef CFAppPath = CFStringCreateWithCString(kCFAllocatorDefault, appPath[brandID],
                                                    kCFStringEncodingUTF8);
    if (CFAppPath) {
    // urlref = CFURLCreateWithFileSystemPath(NULL, "/Applications/GridRepublic Desktop.app", kCFURLPOSIXPathStyle, true);
        CFURLRef urlref = CFURLCreateWithFileSystemPath(NULL, CFAppPath, kCFURLPOSIXPathStyle, true);
        CFRelease(CFAppPath);
        if (urlref) {
            err = LSRegisterURL(urlref, true);
            CFRelease(urlref);
            REPORT_ERROR(err);
        }
    }

    if (compareOSVersionTo(10, 13) >= 0) {
        getPathToThisApp(path, sizeof(path));
        strncat(path, "/Contents/Resources/boinc_Finish_Install", sizeof(s)-1);
        snprintf(s, sizeof(s), "cp -f \"%s\" \"/Library/Application Support/BOINC Data/%s_Finish_Install\"", path, appName[brandID]);
        err = callPosixSpawn(s);
        REPORT_ERROR(err);
        if (err) {
            printf("Command %s returned error %d\n", s, err);
            fflush(stdout);
        }

        snprintf(s, sizeof(s), "/Library/Application Support/BOINC Data/%s_Finish_Install\"</string>\n", appName[brandID]);
        chmod(s, 0755);
#ifdef SANDBOX
        group *bmgrp = getgrnam(boinc_master_group_name);
        passwd *bmpw = getpwnam(boinc_master_user_name);
        if (bmgrp && bmpw) {
            chown(s, bmpw->pw_uid, bmgrp->gr_gid);
        }
#endif
    }

    err = UpdateAllVisibleUsers(brandID, oldBrandID);
    if (err != noErr) {
        REPORT_ERROR(true);
        return err;
    }
    
#if 0   // WaitPermissions is not needed when using wrapper
#ifdef SANDBOX
    pid_t                   waitPermissionsPID = 0;
    uid_t                   saved_euid, saved_uid, b_m_uid;
    passwd                  *pw;
    Boolean                 restartNeeded;
    DialogRef               theWin;

    restartNeeded = IsRestartNeeded();
    printf("IsRestartNeeded() returned %d\n", (int)restartNeeded);
    fflush(stdout);
    
    if (!restartNeeded) {

        // Wait for BOINC's RPC socket address to become available to user boinc_master, in
        // case we are upgrading from a version which did not run as user boinc_master.
        saved_uid = getuid();
        saved_euid = geteuid();
        
        pw = getpwnam(boinc_master_user_name);
        b_m_uid = pw->pw_uid;
        seteuid(b_m_uid);
        
        for (i=0; i<120; i++) {
            err = TestRPCBind();
            if (err == noErr)
                break;
            
            sleep(1);
        }
        
        seteuid(saved_euid);
            
        // When we first create the boinc_master group and add the current user to the
        // new group, there is a delay before the new group membership is recognized.  
        // If we launch the BOINC Manager too soon, it will fail with a -1037 permissions 
        // error, so we wait until the current user can access the switcher application.
        // Apparently, in order to get the changed permissions / group membership, we must 
        // launch a new process belonging to the user.  It may also need to be in a new 
        // process group or new session. Neither system() nor popen() works, even after 
        // setting the uid and euid back to the logged in user, but LSOpenFSRef() does.
        // The WaitPermissions application loops until it can access the switcher 
        // application.
        CFStringRef CFAppPath = CFStringCreateWithCString(kCFAllocatorDefault,
                                        "/Library/Application Support/BOINC Data/WaitPermissions.app",
                                                    kCFStringEncodingUTF8);
        if (CFAppPath) {
            // urlref = CFURLCreateWithFileSystemPath(NULL, "/Applications/GridRepublic Desktop.app", kCFURLPOSIXPathStyle, true);
            CFURLRef urlref = CFURLCreateWithFileSystemPath(NULL, CFAppPath, kCFURLPOSIXPathStyle, true);
            CFRelease(CFAppPath);
            if (urlref) {
                err = LSOpenCFURLRef(urlref, NULL);
                CFRelease(urlref);
            }
        }

        if (err) {
            printf("LSOpenCFURLRef(WaitPermissions) returned error %ld\n", err);
            fflush(stdout);
        }
        waitPermissionsStartTime = time(NULL);
        for (i=0; i<15; i++) {     // Show "Please wait..." alert after 15 seconds
            waitPermissionsPID = FindProcessPID("WaitPermissions", 0);
            if (waitPermissionsPID == 0) {
                return 0;
            }
            sleep(1);
        }
        
        if (gCommandLineInstall) {
            printf("Finishing install.  Please wait ...\n");
            printf("This may take a few more minutes.\n");
            fflush(stdout);
        } else {
            CreateStandardAlert(kAlertNoteAlert, CFSTR("Finishing install.  Please wait ..."), CFSTR("This may take a few more minutes."), NULL, &theWin);
            HideDialogItem(theWin, kStdOkItemIndex);
            RemoveDialogItems(theWin, kStdOkItemIndex, 1, false);
            RunStandardAlert(theWin, &myFilterProc, &itemHit);
        }
    }
#endif   // SANDBOX
#endif  // WaitPermissions is not needed when using wrapper

    
    return 0;
}


Boolean myFilterProc(DialogRef theDialog, EventRecord *theEvent, DialogItemIndex *itemHit) {
    static time_t       lastCheckTime = 0;
    time_t              now = time(NULL);
    pid_t               waitPermissionsPID = 0;

    if (now != lastCheckTime) {
            waitPermissionsPID = FindProcessPID("WaitPermissions", 0);
            if (waitPermissionsPID == 0) {
                *itemHit = kStdOkItemIndex;
                return true;
            }
        lastCheckTime = now;
        // Limit delay to 3 minutes
        if ((now - waitPermissionsStartTime) > 180) {
            *itemHit = kStdOkItemIndex;
            return true;
        }
    }

return false;
}


// After installation has completed, delete the installer receipt.  
// If we don't need to logout the user, also launch BOINC Manager.
int DeleteReceipt()
{
    long                    brandID = 0;
    int                     i;
    pid_t                   installerPID = 0;
    OSStatus                err;
    Boolean                 restartNeeded = true;
    char                    s[MAXPATHLEN];
    struct stat             sbuf;
    passwd                  *pw;
    Boolean                 launchForThisUser;

    if (Initialize() != noErr) {
        REPORT_ERROR(true);
        return 0;
    }

    restartNeeded = IsRestartNeeded();
    printf("IsRestartNeeded() returned %d\n", (int)restartNeeded);
    fflush(stdout);
    
    // The new branding (if any) is in the resources of this PostInstall.app
    getPathToThisApp(s, sizeof(s));
    strncat(s, "/Contents/Resources/Branding", sizeof(s)-1);
    brandID = GetBrandID(s);

    // Remove installer package receipt so we can run installer again if needed to fix permissions
    // "rm -rf /Library/Receipts/GridRepublic.pkg"
    sprintf(s, "rm -rf \"%s\"", receiptName[brandID]);
    err = callPosixSpawn (s);
    REPORT_ERROR(err);

    if (!restartNeeded) {
        // If system is set up to run BOINC Client as a daemon using launchd, launch it 
        //  as a daemon and allow time for client to start before launching BOINC Manager.
        err = stat("/Library/LaunchDaemons/edu.berkeley.boinc.plist", &sbuf);
        if (err == noErr) {
            callPosixSpawn("launchctl unload /Library/LaunchDaemons/edu.berkeley.boinc.plist");
            i = callPosixSpawn("launchctl load /Library/LaunchDaemons/edu.berkeley.boinc.plist");
            if (i == 0) sleep (2);
        }

#ifdef SANDBOX
        pw = getpwnam(loginName);
        REPORT_ERROR(!pw);
        if (pw) {
            Boolean isBMGroupMember = IsUserMemberOfGroup(pw->pw_name, boinc_master_group_name);
            if (!isBMGroupMember){
                return 0;   // Current user is not authorized to run BOINC Manager
            }
        }
#endif

        installerPID = getPidIfRunning("com.apple.installer");
        if (installerPID) {
           // Launch BOINC Manager when user closes installer or after 15 seconds
            for (i=0; i<15; i++) { // Wait 15 seconds max for installer to quit
                sleep (1);
                if (FindProcessPID(NULL, installerPID) == 0) {
                    break;
                }
            }
        }

        CFStringRef CFAppPath = CFStringCreateWithCString(kCFAllocatorDefault, appPath[brandID],
                                                    kCFStringEncodingUTF8);
        if (CFAppPath) {
            // urlref = CFURLCreateWithFileSystemPath(NULL, "/Applications/GridRepublic Desktop.app", kCFURLPOSIXPathStyle, true);
            CFURLRef urlref = CFURLCreateWithFileSystemPath(NULL, CFAppPath, kCFURLPOSIXPathStyle, true);
            if (urlref) {
                err = LSOpenCFURLRef(urlref, NULL);
                REPORT_ERROR(err);
                CFRelease(urlref);
                CFRelease(CFAppPath);
            }
        }

        boinc_sleep(10);    // Allow time for current user's Manager to launch client'
        
        FindAllVisibleUsers();
        
        for (i=0; i<(int)human_user_IDs.size(); ++i) {
            pw = getpwuid(human_user_IDs[i]);
            if (pw == NULL) {
                continue;
            }
            if (strcmp(loginName, pw->pw_name) == 0) continue;
#ifdef SANDBOX
            launchForThisUser = false;
            if (IsUserLoggedIn(pw->pw_name)) {
                launchForThisUser = (IsUserMemberOfGroup(pw->pw_name, admin_group_name)
                                        || IsUserMemberOfGroup(pw->pw_name, boinc_master_group_name));
            }
#else   // SANDBOX
            launchForThisUser = true;
#endif  // SANDBOX

            if (launchForThisUser) {
                // Launch Manager hidden (in background, without opening windows)
                sprintf(s, "su -l \"%s\" -c 'open -jg \"%s\" --args -s'", pw->pw_name, appPath[brandID]);
                err = callPosixSpawn(s);
                printf("command: %s returned error %d\n", s, err);
           }
        }
    }

    return 0;
}


// BOINC Installer.app wrote a file to tell us whether a restart is required
Boolean IsRestartNeeded() {
    char s[MAXPATHLEN];
    FILE *restartNeededFile;
    int value;

    snprintf(s, sizeof(s), "/tmp/%s/BOINC_restart_flag", tempDirName);
    restartNeededFile = fopen(s, "r");
    if (restartNeededFile) {
        fscanf(restartNeededFile,"%d", &value);
        fclose(restartNeededFile);
        return (value != 0);
    }
    
    return true;
}


// Some newer versions of the OS define users and groups which may conflict with 
// our previously created boinc_master or boinc_project user or group.  This could 
// also happen when the user installs new software.  So we must check for such 
// duplicate UserIDs and groupIDs; if found, we delete our user or group so that 
// the PostInstall application will create a new one that does not conflict.
//
// Older versions of the installer created our users and groups at the first
// unused IDs at or above 25.  Apple now recommends using IDs at or above 501, 
// to reduce the likelihood of conflicts with future UserIDs and groupIDs.
// If we have previously created UserIDs and / or groupIDs below 501, this code 
// now removes them so we can create new ones above 500.
void CheckUserAndGroupConflicts()
{
#ifdef SANDBOX
    passwd          *pw = NULL;
    group           *grp = NULL;
    gid_t           boinc_master_gid = 0, boinc_project_gid = 0;
    uid_t           boinc_master_uid = 0, boinc_project_uid = 0;
    
    FILE            *f;
    char            cmd[256], buf[256];
    int             entryCount;
    OSErr           err = noErr;

    if (compareOSVersionTo(10, 5) < 0) {
        // This fails under OS 10.4, but should not be needed under OS 10.4
        return;     
    }

    printf("Checking user and group conflicts\n");
    fflush(stdout);
    
    entryCount = 0;
    grp = getgrnam(boinc_master_group_name);
    if (grp) {
        boinc_master_gid = grp->gr_gid;
        printf("boinc_master group ID = %d\n", (int)boinc_master_gid);
        fflush(stdout);
        if (boinc_master_gid > 500) {
            sprintf(cmd, "dscl . -search /Groups PrimaryGroupID %d", boinc_master_gid);
            f = popen(cmd, "r");
            REPORT_ERROR(!f);
            if (f) {
                while (PersistentFGets(buf, sizeof(buf), f)) {
                    if (strstr(buf, "PrimaryGroupID")) {
                        ++entryCount;
                    }
                }
                pclose(f);
            }    
        }
    }
    if ((boinc_master_gid < 501) || (entryCount > 1)) {
        err = callPosixSpawn ("dscl . -delete /groups/boinc_master");
        // User boinc_master must have group boinc_master as its primary group.
        // Since this group no longer exists, delete the user as well.
        if (err) {
            fprintf(stdout, "dscl . -delete /groups/boinc_master returned %d\n", err);
            fflush(stdout);
        }
        err = callPosixSpawn ("dscl . -delete /users/boinc_master");
        if (err) {
            fprintf(stdout, "dscl . -delete /users/boinc_master returned %d\n", err);
            fflush(stdout);
        }
        ResynchDSSystem();
    }

    entryCount = 0;
    grp = getgrnam(boinc_project_group_name);
    if (grp) {
        boinc_project_gid = grp->gr_gid;
        printf("boinc_project group ID = %d\n", (int)boinc_project_gid);
        fflush(stdout);
        if (boinc_project_gid > 500) {
            sprintf(cmd, "dscl . -search /Groups PrimaryGroupID %d", boinc_project_gid);
            f = popen(cmd, "r");
            REPORT_ERROR(!f);
            if (f) {
                while (PersistentFGets(buf, sizeof(buf), f)) {
                    if (strstr(buf, "PrimaryGroupID")) {
                        ++entryCount;
                    }
                }
                pclose(f);
            }    
        }
    }
    
    if ((boinc_project_gid < 501) || (entryCount > 1)) {
        err = callPosixSpawn ("dscl . -delete /groups/boinc_project");
        if (err) {
            fprintf(stdout, "dscl . -delete /groups/boinc_project returned %d\n", err);
            fflush(stdout);
        }
        // User boinc_project must have group boinc_project as its primary group.
        // Since this group no longer exists, delete the user as well.
        err = callPosixSpawn ("dscl . -delete /users/boinc_project");
        if (err) {
            fprintf(stdout, "dscl . -delete /users/boinc_project returned %d\n", err);
            fflush(stdout);
        }
        ResynchDSSystem();
    }

    if ((boinc_master_gid < 500) && (boinc_project_gid < 500)) {
        return;
    }

    entryCount = 0;
    pw = getpwnam(boinc_master_user_name);
    REPORT_ERROR(!pw);
    if (pw) {
        boinc_master_uid = pw->pw_uid;
        printf("boinc_master user ID = %d\n", (int)boinc_master_uid);
        fflush(stdout);
        sprintf(cmd, "dscl . -search /Users UniqueID %d", boinc_master_uid);
        f = popen(cmd, "r");
        REPORT_ERROR(!f);
        if (f) {
            while (PersistentFGets(buf, sizeof(buf), f)) {
                if (strstr(buf, "UniqueID")) {
                    ++entryCount;
                }
            }
            pclose(f);
        }    
    }

    if (entryCount > 1) {
        err = callPosixSpawn ("dscl . -delete /users/boinc_master");
        if (err) {
            REPORT_ERROR(true);
            fprintf(stdout, "dscl . -delete /users/boinc_master returned %d\n", err);
            fflush(stdout);
        }
        ResynchDSSystem();
    }
        
    entryCount = 0;
    pw = getpwnam(boinc_project_user_name);
    REPORT_ERROR(!pw);
    if (pw) {
        boinc_project_uid = pw->pw_uid;
        printf("boinc_project user ID = %d\n", (int)boinc_project_uid);
        fflush(stdout);
        sprintf(cmd, "dscl . -search /Users UniqueID %d", boinc_project_uid);
        f = popen(cmd, "r");
        REPORT_ERROR(!f);
        if (f) {
            while (PersistentFGets(buf, sizeof(buf), f)) {
                if (strstr(buf, "UniqueID")) {
                    ++entryCount;
                }
            }
            pclose(f);
        }    
    }

    if (entryCount > 1) {
        err = callPosixSpawn ("dscl . -delete /users/boinc_project");
        if (err) {
            REPORT_ERROR(true);
            fprintf(stdout, "dscl . -delete /users/boinc_project returned %d\n", err);
            fflush(stdout);
        }
        ResynchDSSystem();
    }
#endif  // SANDBOX
}

enum {
	kSystemEventsCreator = 'sevs'
};

CFStringRef kSystemEventsBundleID = CFSTR("com.apple.systemevents");
char *systemEventsAppName = "System Events";


Boolean SetLoginItemOSAScript(long brandID, Boolean deleteLogInItem, char *userName)
{
    int                     i, j;
    char                    cmd[2048];
    char                    systemEventsPath[1024];
    pid_t                   systemEventsPID;
    OSErr                   err, err2;
#if USE_OSASCRIPT_FOR_ALL_LOGGED_IN_USERS
    // NOTE: It may not be necessary to kill and relaunch the
    // System Events application for each logged in user under High Sierra 
    Boolean                 isHighSierraOrLater = (compareOSVersionTo(10, 13) >= 0);
#endif

    fprintf(stdout, "Adjusting login items for user %s\n", userName);
    fflush(stdout);

    // We must launch the System Events application for the target user
    err = noErr;
    systemEventsPath[0] = '\0';

    err = GetPathToAppFromID(kSystemEventsCreator, kSystemEventsBundleID, systemEventsPath, sizeof(systemEventsPath));
    REPORT_ERROR(err);

#if CREATE_LOG
    if (err == noErr) {
        print_to_log("SystemEvents is at %s\n", systemEventsPath);
    } else {
        print_to_log("GetPathToAppFromID(kSystemEventsCreator, kSystemEventsBundleID) returned error %d ", (int) err);
    }
#endif

    if (err == noErr) {
        // Find SystemEvents process.  If found, quit it in case 
        // it is running under a different user.
        fprintf(stdout, "Telling System Events to quit (at start of SetLoginItemOSAScript)\n");
        fflush(stdout);
        systemEventsPID = FindProcessPID(systemEventsAppName, 0);
        if (systemEventsPID != 0) {
            err = kill(systemEventsPID, SIGKILL);
        }
        if (err != noErr) {
            REPORT_ERROR(true);
            fprintf(stdout, "(systemEventsPID, SIGKILL) returned error %d \n", (int) err);
            fflush(stdout);
        }
        // Wait for the process to be gone
        for (i=0; i<50; ++i) {      // 5 seconds max delay
            SleepSeconds(0.1);      // 1/10 second
            systemEventsPID = FindProcessPID(systemEventsAppName, 0);
            if (systemEventsPID == 0) break;
        }
        if (i >= 50) {
            REPORT_ERROR(true);
            fprintf(stdout, "Failed to make System Events quit\n");
            fflush(stdout);
            err = noErr;
            goto cleanupSystemEvents;
        }
        sleep(4);
    }
    
    if (systemEventsPath[0] != '\0') {
        fprintf(stdout, "Launching SystemEvents for user %s\n", userName);
        fflush(stdout);

        for (j=0; j<5; ++j) {
            sprintf(cmd, "sudo -u \"%s\" -b \"%s/Contents/MacOS/System Events\" &", userName, systemEventsPath);
            err = callPosixSpawn(cmd);
            if (err) {
                REPORT_ERROR(true);
                fprintf(stdout, "[2] Command: %s returned error %d (try %d of 5)\n", cmd, (int) err, j);
            }
            // Wait for the process to start
            for (i=0; i<50; ++i) {      // 5 seconds max delay
                SleepSeconds(0.1);      // 1/10 second
                systemEventsPID = FindProcessPID(systemEventsAppName, 0);
                if (systemEventsPID != 0) break;
            }
            if (i < 50) break;  // Exit j loop on success
        }
        if (j >= 5) {
            fprintf(stdout, "Failed to launch System Events for user %s\n", userName);
            REPORT_ERROR(true);
            fflush(stdout);
            err = noErr;
            goto cleanupSystemEvents;
        }
    }
    sleep(2);
    
    for (i=0; i<NUMBRANDS; i++) {
        fprintf(stdout, "Deleting any login items containing %s for user %s\n", appName[i], userName);
        fflush(stdout);
#if USE_OSASCRIPT_FOR_ALL_LOGGED_IN_USERS
        if (isHighSierraOrLater) {
            sprintf(cmd, "su -l \"%s\" -c 'osascript -e \"tell application \\\"System Events\\\" to delete login item \\\"%s\\\"\"'", userName, appName[i]);
        } else
#endif
        {
            sprintf(cmd, "sudo -u \"%s\" osascript -e 'tell application \"System Events\" to delete login item \"%s\"'", userName, appName[i]);
        }
        err = callPosixSpawn(cmd);
        if (err) {
            REPORT_ERROR(true);
            fprintf(stdout, "[2] Command: %s\n", cmd);
            fprintf(stdout, "[2] Delete login item containing %s returned error %d\n", appName[i], err);
            fflush(stdout);
        }
    }

    if (deleteLogInItem) {
        err = noErr;
        goto cleanupSystemEvents;
    }
    
    fprintf(stdout, "Making new login item %s for user %s\n", appName[brandID], userName);
    fflush(stdout);
#if USE_OSASCRIPT_FOR_ALL_LOGGED_IN_USERS
    if (isHighSierraOrLater) {
        sprintf(cmd, "su -l \"%s\" -c 'osascript -e \"tell application \\\"System Events\\\" to make new login item at end with properties {path:\\\"%s\\\", hidden:true, name:\\\"%s\\\"}\"'", userName, appPath[brandID], appName[brandID]);
    } else
#endif
    {
        sprintf(cmd, "sudo -u \"%s\" osascript -e 'tell application \"System Events\" to make new login item at end with properties {path:\"%s\", hidden:true, name:\"%s\"}'", userName, appPath[brandID], appName[brandID]);
    }
    err = callPosixSpawn(cmd);
    if (err) {
        REPORT_ERROR(true);
        fprintf(stdout, "[2] Command: %s\n", cmd);
        printf("[2] Make login item for %s returned error %d\n", appPath[brandID], err);
    }
    fflush(stdout);

cleanupSystemEvents:
    // Clean up in case this was our last user
    fprintf(stdout, "Telling System Events to quit (at end of SetLoginItemOSAScript)\n");
    fflush(stdout);
    systemEventsPID = FindProcessPID(systemEventsAppName, 0);
    err2 = noErr;
    if (systemEventsPID != 0) {
        err2 = kill(systemEventsPID, SIGKILL);
    }
    if (err2 != noErr) {
        REPORT_ERROR(true);
        fprintf(stdout, "kill(systemEventsPID, SIGKILL) returned error %d \n", (int) err2);
        fflush(stdout);
    }
    // Wait for the process to be gone
    for (i=0; i<50; ++i) {      // 5 seconds max delay
        SleepSeconds(0.1);      // 1/10 second
        systemEventsPID = FindProcessPID(systemEventsAppName, 0);
        if (systemEventsPID == 0) break;
    }
    if (i >= 50) {
        REPORT_ERROR(true);
        fprintf(stdout, "Failed to make System Events quit\n");
        fflush(stdout);
    }
    
    sleep(4);
        
    return (err == noErr);
}


// Under OS 10.13 High Sierra, telling System Events to modify Login Items for 
// users who are not currently logged in no longer works, even when System Events 
// is running as that user. 
// So we create a LaunchAgent for that user. The next time that user logs in, the 
// LaunchAgent will make the desired changes to that user's Login Items, launch 
// BOINC Manager if appropriate, and delete itself.
//
// While we could just use a LaunchAgent to launch BOINC Manager on every login 
// instead of using it to create a Login Item, we still need to remove any branded
// Login Items kept from an earlier installation (perhaps before the user upgraded
// the OS to High Sierra.) Also, I prefer Login Items because:
//  * they are more readily visible to a less technically aware user through 
//    System Preferences, and
//  * they are more easily added or removed through System Preferences, and
//  * continuing to use them is consistent with older versions of BOINC Manager.
//
Boolean SetLoginItemLaunchAgent(long brandID, long oldBrandID, Boolean deleteLogInItem, passwd *pw)
{
    struct stat             sbuf;
    char                    s[2048];

    // Create a LaunchAgent for the specified user, replacing any LaunchAgent created
    // previously (such as by Uninstaller or by installing a differently branded BOINC.)

    // Create LaunchAgents directory for this user if it does not yet exist
    snprintf(s, sizeof(s), "/Users/%s/Library/LaunchAgents", pw->pw_name);
    if (stat(s, &sbuf) != 0) {
        mkdir(s, 0755);
        chown(s, pw->pw_uid, pw->pw_gid);
    }

    snprintf(s, sizeof(s), "/Users/%s/Library/LaunchAgents/edu.berkeley.boinc.plist", pw->pw_name);
    FILE* f = fopen(s, "w");
    if (!f) return false;
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    fprintf(f, "<plist version=\"1.0\">\n");
    fprintf(f, "<dict>\n");
    fprintf(f, "\t<key>Label</key>\n");
    fprintf(f, "\t<string>edu.berkeley.fix_login_items</string>\n");
    fprintf(f, "\t<key>ProgramArguments</key>\n");
    fprintf(f, "\t<array>\n");
    fprintf(f, "\t\t<string>/Library/Application Support/BOINC Data/%s_Finish_Install</string>\n", appName[brandID]);
    if (deleteLogInItem || (brandID != oldBrandID)) {
        // If this user was previously authorized to run the Manager, there 
        // may still be a Login Item for this user, and the Login Item may
        // launch the Manager before the LaunchAgent deletes the Login Item.
        // To guard against this, we have the LaunchAgent kill the Manager
        // (for this user only) if it is running.
        //
        fprintf(f, "\t\t<string>-d</string>\n");
        fprintf(f, "\t\t<string>%s</string>\n", appName[oldBrandID]);
    }
    if (!deleteLogInItem) {
        fprintf(f, "\t\t<string>-a</string>\n");
        fprintf(f, "\t\t<string>%s</string>\n", appName[brandID]);
    }
    fprintf(f, "</string>\n");
    fprintf(f, "\t</array>\n");
    fprintf(f, "\t<key>RunAtLoad</key>\n");
    fprintf(f, "\t<true/>\n");
    fprintf(f, "</dict>\n");
    fprintf(f, "</plist>\n");
    fclose(f);

    chmod(s, 0644);
    chown(s, pw->pw_uid, pw->pw_gid);

    return true;
}


// Sets the skin selection in the specified user's preferences to the specified skin
void SetSkinInUserPrefs(char *userName, char *nameOfSkin)
{
    passwd              *pw;
    FILE                *oldPrefs, *newPrefs;
    char                oldFileName[MAXPATHLEN], tempFilename[MAXPATHLEN];
    char                buf[1024];
    int                 wroteSkinName;
    struct stat         sbuf;
    group               *grp;
    OSStatus            statErr;

    if (nameOfSkin[0]) {
        sprintf(oldFileName, "/Users/%s/Library/Preferences/BOINC Manager Preferences", userName);
        sprintf(tempFilename, "/Users/%s/Library/Preferences/BOINC Manager NewPrefs", userName);
        newPrefs = fopen(tempFilename, "w");
        REPORT_ERROR(!newPrefs);
        if (newPrefs) {
            wroteSkinName = 0;
            statErr = stat(oldFileName, &sbuf);

            oldPrefs = fopen(oldFileName, "r");
            if (oldPrefs) {
                while (fgets(buf, sizeof(buf), oldPrefs)) {
                    if (strstr(buf, "Skin=")) {
                        fprintf(newPrefs, "Skin=%s\n", nameOfSkin);
                        wroteSkinName = 1;
                    } else {
                        fputs(buf, newPrefs);
                    }
                }
                fclose(oldPrefs);
            }
            
            if (! wroteSkinName)
                fprintf(newPrefs, "Skin=%s\n", nameOfSkin);
                
            fclose(newPrefs);
            rename(tempFilename, oldFileName);  // Deletes old file
            if (! statErr) {
                chown(oldFileName, sbuf.st_uid, sbuf.st_gid);
                chmod(oldFileName, sbuf.st_mode);
            } else {
                chmod(oldFileName, 0664);
                pw = getpwnam(userName);
                grp = getgrnam(userName);
                if (pw && grp)
                    chown(oldFileName, pw->pw_uid, grp->gr_gid);
            }
        }
    }
}

// Returns true if the user name is in the nologinitems.txt, else false
Boolean CheckDeleteFile(char *name)
{
    FILE        *f;
    char        buf[64];
    size_t      len;
    
    f = fopen("/Library/Application Support/BOINC Data/nologinitems.txt", "r");
    if (!f)
        return false;
    
    while (true) {
        *buf = '\0';
        len = sizeof(buf);
        fgets(buf, len, f);
        if (feof(f)) break;
        strip_cr(buf);
        if (strcmp(buf, name) == 0) {
            fclose(f);
            return true;
        }
    }
    
    fclose(f);
    return false;
}


// If there are other copies of BOINC Manager with different branding
// on the system, Noitifications may display the icon for the wrong
// branding, due to the Launch Services database having one of the
// other copies of BOINC Manager as the first entry. Each user has
// their own copy of the Launch Services database, so this must be
// done for each user.
//
// This probably will happen only on BOINC development systems where
// Xcode has generated copies of BOINC Manager.
static void FixLaunchServicesDataBase(uid_t userID, long brandID) {
    uid_t saved_uid;
    char boincPath[MAXPATHLEN];
    char cmd[MAXPATHLEN+250];
    long i, n;
    CFArrayRef appRefs = NULL;
    OSStatus err;

    if (compareOSVersionTo(10, 8) < 0) {
        return;  // Notifications before OS 10.8 just bounce our Dock icon
    }

    saved_uid = geteuid();
    CFStringRef bundleID = CFSTR("edu.berkeley.boinc");

    if (LSCopyApplicationURLsForBundleIdentifier) { // Weak linked; not available before OS 10.10
        seteuid(userID);    // Temporarily set effective uid to this user
        appRefs = LSCopyApplicationURLsForBundleIdentifier(bundleID, NULL);
        seteuid(saved_uid);     // Set effective uid back to privileged user
            if (appRefs == NULL) {
                printf("Call to LSCopyApplicationURLsForBundleIdentifier returned NULL\n");
                goto registerOurApp;
            }
        n = CFArrayGetCount(appRefs);   // Returns all results at once, in database order
        printf("LSCopyApplicationURLsForBundleIdentifier returned %ld results\n", n);
    } else {
        n = 500;    // Prevent infinite loop
    }

    for (i=0; i<n; ++i) {     // Prevent infinite loop
        if (appRefs) {
            CFURLRef appURL = (CFURLRef)CFArrayGetValueAtIndex(appRefs, i);
            boincPath[0] = '\0';
            if (appURL) {
                CFRetain(appURL);
                CFStringRef CFPath = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);
                CFStringGetCString(CFPath, boincPath, sizeof(boincPath), kCFStringEncodingUTF8);
                if (CFPath) CFRelease(CFPath);
                CFRelease(appURL);
                appURL = NULL;
            }
        } else {
            seteuid(userID);    // Temporarily set effective uid to this user
            // GetPathToAppFromID() returns only first result from database
            err = GetPathToAppFromID('BNC!', bundleID,  boincPath, sizeof(boincPath));
            seteuid(saved_uid);     // Set effective uid back to privileged user
            if (err) {
                printf("Call %ld to GetPathToAppFromID returned error %d\n", i, err);
                break;
            }
        }
        if (strncmp(boincPath, appPath[brandID], sizeof(boincPath)) == 0) {
            printf("**** Keeping %s\n", boincPath);
            if (appRefs) CFRelease(appRefs);
            return;     // Our (possibly branded) BOINC Manager app is now at top of database
        }
        printf("Unregistering %3ld: %s\n", i, boincPath);
        // Remove this entry from the Launch Services database
        sprintf(cmd, "sudo -u #%d /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Versions/Current/Support/lsregister -u \"%s\"", userID, boincPath);
        err = callPosixSpawn(cmd);
        if (err) {
            printf("*** lsregister -u call returned error %d for %s\n", err, boincPath);
            fflush(stdout);
        }
    }

registerOurApp:
    if (appRefs) CFRelease(appRefs);

    // We have exhausted the Launch Services database without finding our
    // (possibly branded) BOINC Manager app, so add it to the dataabase
    printf("%s was not found in Launch Services database; registering it now\n", appPath[brandID]);
    sprintf(cmd, "sudo -u #%d /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Versions/Current/Support/lsregister \"%s\"", userID, appPath[brandID]);
    err = callPosixSpawn(cmd);
    if (err) {
        printf("*** lsregister call returned error %d for %s\n", err, appPath[brandID]);
        fflush(stdout);
    }
}

void SetEUIDBackToUser (void)
{
    uid_t login_uid;
    passwd *pw;

    pw = getpwnam(loginName);
    login_uid = pw->pw_uid;

    setuid(login_uid);
    seteuid(login_uid);
}


static char * PersistentFGets(char *buf, size_t buflen, FILE *f) {
    char *p = buf;
    size_t len = buflen;
    size_t datalen = 0;
    memset(buf, 0, buflen);
    while (datalen < (buflen - 1)) {
        fgets(p, len, f);
        if (feof(f)) break;
        if (ferror(f) && (errno != EINTR)) break;
        if (strchr(buf, '\n')) break;
        datalen = strlen(buf);
        p = buf + datalen;
        len -= datalen;
    }
    return (buf[0] ? buf : NULL);
}


// Because language preferences are set on a per-user basis, we
// must get the preferred languages while set to the current 
// user, before the Apple Installer switches us to root.
// So we get the preferred languages in our BOINC Installer.app 
// which writes them to a temporary file which we retrieve here.
// We must do it this way because, for unknown reasons, the
// CFBundleCopyLocalizationsForPreferences() API does not work
// correctly if we seteuid and setuid to the logged in user by
// calling SetEUIDBackToUser() after running as root.
//
static void LoadPreferredLanguages(){
    char s[MAXPATHLEN];
    FILE *f;
    int i;
    char *p;
    char language[32];

    BOINCTranslationInit();

    // BOINC Installer.app wrote a list of our preferred languages to a temp file
    snprintf(s, sizeof(s), "/tmp/%s/BOINC_preferred_languages", tempDirName);
    f = fopen(s, "r");
    if (!f) return;
    
    for (i=0; i<MAX_LANGUAGES_TO_TRY; ++i) {
        fgets(language, sizeof(language), f);
        if (feof(f)) break;
        language[sizeof(language)-1] = '\0';    // Guarantee a null terminator
        p = strchr(language, '\n');
        if (p) *p = '\0';           // Replace newline with null terminator 
        if (language[0]) {
            if (!BOINCTranslationAddCatalog(Catalogs_Dir, language, Catalog_Name)) {
                REPORT_ERROR(true);
                printf("could not load catalog for langage %s\n", language);
            }
        }
    }
    fclose(f);
}


static Boolean ShowMessage(Boolean askYesNo, const char *format, ...) {
  // CAUTION: vsprintf will produce undesirable results if the string
  // contains a % character that is not a format specification!
  // But CFString is OK!

    va_list                 args;
    char                    s[1024];
    CFOptionFlags           responseFlags;
    CFURLRef                myIconURLRef = NULL;
    CFBundleRef             myBundleRef;
   
    myBundleRef = CFBundleGetMainBundle();
    if (myBundleRef) {
        myIconURLRef = CFBundleCopyResourceURL(myBundleRef, CFSTR("MacInstaller.icns"), NULL, NULL);
    }
    
#if 1
    va_start(args, format);
    vsprintf(s, format, args);
    va_end(args);
#else
    strcpy(s, format);
#endif

    // If defaultButton is nil or an empty string, a default localized
    // button title ("OK" in English) is used.
    
#if 0
    enum {
   kCFUserNotificationDefaultResponse = 0,
   kCFUserNotificationAlternateResponse = 1,
   kCFUserNotificationOtherResponse = 2,
   kCFUserNotificationCancelResponse = 3
};
#endif

    CFStringRef myString = CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8);
    CFStringRef yes = CFStringCreateWithCString(NULL, (char*)_((char*)"Yes"), kCFStringEncodingUTF8);
    CFStringRef no = CFStringCreateWithCString(NULL, (char*)_((char*)"No"), kCFStringEncodingUTF8);

    BringAppToFront();
    SInt32 retval = CFUserNotificationDisplayAlert(0.0, kCFUserNotificationPlainAlertLevel,
                myIconURLRef, NULL, NULL, CFSTR(" "), myString,
                askYesNo ? yes : NULL, askYesNo ? no : NULL, NULL,
                &responseFlags);
    
       
    if (myIconURLRef) CFRelease(myIconURLRef);
    if (myString) CFRelease(myString);
    if (yes) CFRelease(yes);
    if (no) CFRelease(no);

    if (retval) return false;
    return (responseFlags == kCFUserNotificationDefaultResponse);
}


Boolean IsUserMemberOfGroup(const char *userName, const char *groupName) {
    group               *grp;
    short               i = 0;
    char                *p;

    grp = getgrnam(groupName);
    if (!grp) {
        printf("getgrnam(%s) failed\n", groupName);
        fflush(stdout);
        return false;  // Group not found
    }

    while ((p = grp->gr_mem[i]) != NULL) {  // Step through all users in group groupName
        if (strcmp(p, userName) == 0) {
            return true;
        }
        ++i;
    }
    return false;
}

// OS 10.7 dscl merge command has a bug such that the command:
//     dscl . -merge /Groups/GROUPNAME users USERNAME
// adds the user to the group even if it was already a member, resulting in 
// duplicate (multiple) entries.  Earlier BOINC versions used this command 
// but did not check for this, so we remove duplicate entries if present.
// Note: We now avoid this problem by instead using the command:
//     dscl . -merge /Groups/GROUPNAME GroupMembership USERNAME
// which correctly avoids duplication.

int CountGroupMembershipEntries(const char *userName, const char *groupName) {
    int                 count = 0;
    char                cmd[512], buf[2048], escapedUserName[1024];
    FILE                *f;
    char                *p, *q;
    
    // getgrnam(groupName)->gr_mem[] only returns one entry, so we must use dscl
    escape_url(userName, escapedUserName, sizeof(escapedUserName)); // Avoid confusion if name has embedded spaces
    sprintf(cmd, "dscl -url . -read /Groups/%s GroupMembership", groupName);
    f = popen(cmd, "r");
    if (f == NULL) {
        REPORT_ERROR(true);
        return 0;
    }
    
    while (PersistentFGets(buf, sizeof(buf), f))
    {
        p = buf;
        while (p) {
            p = strstr(p, escapedUserName);
            if (p) {
                q = p-1;
                p += strlen(escapedUserName);
                // Count only whole words (preceded and followed by white space) so 
                // that if we have both 'jon' and 'jones' we don't count 'jon' twice
                if (isspace(*q) && isspace(*p)) {
                    ++ count;
               }
            }
        }
    }
    
    pclose(f);
    return count;
}


// Find all visible users.
// If user is a member of group admin, add user to groups boinc_master and boinc_project.
// Optionally add non-admin users to group boinc_master but not to group boinc_project.
// Set login item for all members of group boinc_master to launch BOINC Manager.
// If our install package included a skin, set those user's preferences to use that skin.
// Optionally set BOINC as screensaver for all users running BOINC.
OSErr UpdateAllVisibleUsers(long brandID, long oldBrandID)
{
    passwd              *pw;
    uid_t               saved_uid;
    Boolean             deleteLoginItem;
    char                human_user_name[256];
    char                s[256];
    Boolean             saverAlreadySetForAll = true;
    Boolean             setSaverForAllUsers = false;
    Boolean             allNonAdminUsersAreSet = true;
    Boolean             allowNonAdminUsersToRunBOINC = false;
    int                 err;
    Boolean             isAdminGroupMember, isBMGroupMember, isBPGroupMember;
    struct stat         sbuf;
    char                cmd[256];
#ifdef SANDBOX
    int                 BMGroupMembershipCount, BPGroupMembershipCount; 
    int                 i;
#endif
    int                 userIndex;
    
//    char                nameOfSkin[256];
//    FindSkinName(nameOfSkin, sizeof(nameOfSkin));
        

    // Step through all users
    puts("Beginning first pass through all users\n");
    fflush(stdout);

    saved_uid = geteuid();

    FindAllVisibleUsers();
    
    for (userIndex=0; userIndex< (int)human_user_names.size(); ++userIndex) {
        strlcpy(human_user_name, human_user_names[userIndex].c_str(), sizeof(human_user_name));
        printf("[1] Checking user %s\n", human_user_name);
        fflush(stdout);
            
        // getpwnam works with either the full / login name (pw->pw_gecos) 
        // or the short / Posix name (pw->pw_name)
        pw = getpwnam(human_user_name);
        if (pw == NULL) {
            printf("[1] %s not in getpwnam data base\n", human_user_name);
            fflush(stdout);
            continue;
        }

        printf("[1] User %s: Posix name=%s, Full name=%s\n", human_user_name, pw->pw_name, pw->pw_gecos);
        fflush(stdout);
        
#ifdef SANDBOX
        isAdminGroupMember = false;
        isBMGroupMember = false;
        
        isAdminGroupMember = IsUserMemberOfGroup(pw->pw_name, admin_group_name);
            if (isAdminGroupMember) {
            // User is a member of group admin, so add user to groups boinc_master and boinc_project
            printf("[1] User %s is a member of group admin\n", pw->pw_name);
            fflush(stdout);
        } else {
            isBMGroupMember = IsUserMemberOfGroup(pw->pw_name, boinc_master_group_name);
            if (isBMGroupMember) {
                // User is a member of group boinc_master
                printf("[1] Non-admin user %s is a member of group boinc_master\n", pw->pw_name);
                fflush(stdout);
            } else {
                allNonAdminUsersAreSet = false;
            }
        }
#else   // SANDBOX
        isGroupMember = true;
#endif  // SANDBOX
        if (isAdminGroupMember || isBMGroupMember) {
            if ((strcmp(loginName, human_user_name) == 0) 
                || (strcmp(loginName, pw->pw_name) == 0) 
                    || (strcmp(loginName, pw->pw_gecos) == 0)) {
                currentUserCanRunBOINC = true;
            }
            
            err = GetCurrentScreenSaverSelection(pw, s, sizeof(s) -1);
            if (err == noErr) {
                if (strcmp(s, saverName[brandID])) {
                    saverAlreadySetForAll = false;
                }
            }
            printf("[1] Current Screensaver Selection for user %s is: \"%s\"\n", pw->pw_name, s);
        }       // End if (isGroupMember)
    }           // End for (userIndex=0; userIndex< human_user_names.size(); ++userIndex)
    
    ResynchDSSystem();

    if (allNonAdminUsersAreSet) {
        puts("[2] All non-admin users are already members of group boinc_master\n");
        fflush(stdout);
    } else {
        if (gCommandLineInstall) {
            err = stat("/tmp/nonadminusersok.txt", &sbuf);
            if (err == noErr) {
                puts("nonadminusersok.txt file detected\n");
                fflush(stdout);
                unlink("/tmp/nonadminusersok.txt");
                allowNonAdminUsersToRunBOINC = true;
                currentUserCanRunBOINC = true;
                saverAlreadySetForAll = false;
            }
        } else {
            if (ShowMessage(true,
                (char *)_("Users who are permitted to administer this computer will automatically be allowed to "
                "run and control %s.\n\n"
                "Do you also want non-administrative users to be able to run and control %s on this Mac?"),
                brandName[brandID], brandName[brandID])
            ) {
                allowNonAdminUsersToRunBOINC = true;
                currentUserCanRunBOINC = true;
                saverAlreadySetForAll = false;
                printf("[2] User answered Yes to allowing non-admin users to run %s\n", brandName[brandID]);
                fflush(stdout);
            } else {
                printf("[2] User answered No to allowing non-admin users to run %s\n", brandName[brandID]);
                fflush(stdout);
            }
        }
    }
    
    if (! saverAlreadySetForAll) {
        if (gCommandLineInstall) {
            err = stat("/tmp/setboincsaver.txt", &sbuf);
            if (err == noErr) {
                puts("setboincsaver.txt file detected\n");
                fflush(stdout);
                unlink("/tmp/setboincsaver.txt");
                setSaverForAllUsers = true;
            }
        } else {
            setSaverForAllUsers = ShowMessage(true, 
                    (char *)_("Do you want to set %s as the screensaver for all %s users on this Mac?"),
                    brandName[brandID], brandName[brandID]);
        }
    }

    // Step through all users a second time, setting non-admin users and / or our screensaver
    puts("Beginning second pass through all users\n");
    fflush(stdout);

    for (userIndex=0; userIndex<(int)human_user_names.size(); ++userIndex) {
        strlcpy(human_user_name, human_user_names[userIndex].c_str(), sizeof(human_user_name));

        printf("[2] Checking user %s\n", human_user_name);
        fflush(stdout);
            
        pw = getpwnam(human_user_name);
        if (pw == NULL) {           // "Deleted Users", "Shared", etc.
            printf("[2] %s not in getpwnam data base\n", human_user_name);
            fflush(stdout);
            continue;
        }

        printf("[2] User %s: Posix name=%s, Full name=%s\n", human_user_name, pw->pw_name, pw->pw_gecos);
        fflush(stdout);
        
#ifdef SANDBOX
        isAdminGroupMember = false;
        isBMGroupMember = false;
        isBPGroupMember = false;

        isAdminGroupMember = IsUserMemberOfGroup(pw->pw_name, admin_group_name);
        if (isAdminGroupMember) {
            // User is a member of group admin, so add user to groups boinc_master and boinc_project
            printf("[2] User %s is a member of group admin\n", pw->pw_name);
            fflush(stdout);
        }

        // If allNonAdminUsersAreSet, some older BOINC versions added non-admin users only to group 
        // boinc_master; ensure all permitted BOINC users are also members of group boinc_project
        if (isAdminGroupMember || allowNonAdminUsersToRunBOINC || allNonAdminUsersAreSet) {
            // OS 10.7 dscl merge command has a bug that it adds the user to the group even if 
            // it was already a member, resulting in duplicate (multiple) entries.  Earlier BOINC 
            // versions did not check for this, so we remove duplicate entries if present.            
            BMGroupMembershipCount = CountGroupMembershipEntries(pw->pw_name, boinc_master_group_name);
            printf("[2] User %s found in group %s member list %d times\n", 
                        pw->pw_name, boinc_master_group_name, BMGroupMembershipCount);
            fflush(stdout);
            if (BMGroupMembershipCount == 0) {
                sprintf(cmd, "dscl . -merge /groups/%s GroupMembership \"%s\"", boinc_master_group_name, pw->pw_name);
                err = callPosixSpawn(cmd);
                REPORT_ERROR(err);
                printf("[2] %s returned %d\n", cmd, err);
                fflush(stdout);
                isBMGroupMember = true;
            } else {
                isBMGroupMember = true;
                for (i=1; i<BMGroupMembershipCount; ++i) {
                    sprintf(cmd, "dscl . -delete /groups/%s GroupMembership \"%s\"", boinc_master_group_name, pw->pw_name);
                    err = callPosixSpawn(cmd);
                    REPORT_ERROR(err);
                    printf("[2] %s returned %d\n", cmd, err);
                    fflush(stdout);
                }
            }
            
            BPGroupMembershipCount = CountGroupMembershipEntries(pw->pw_name, boinc_project_group_name);
            printf("[2] User %s found in group %s member list %d times\n", 
                   pw->pw_name, boinc_project_group_name, BPGroupMembershipCount);
            fflush(stdout);
            if (BPGroupMembershipCount == 0) {
                sprintf(cmd, "dscl . -merge /groups/%s GroupMembership \"%s\"", boinc_project_group_name, pw->pw_name);
                err = callPosixSpawn(cmd);
                REPORT_ERROR(err);
                printf("[2] %s returned %d\n", cmd, err);
                fflush(stdout);
                isBPGroupMember = true;
            } else {
                isBPGroupMember = true;
                for (i=1; i<BPGroupMembershipCount; ++i) {
                    sprintf(cmd, "dscl . -delete /groups/%s GroupMembership \"%s\"", boinc_project_group_name, pw->pw_name);
                    err = callPosixSpawn(cmd);
                    REPORT_ERROR(err);
                    printf("[2] %s returned %d\n", cmd, err);
                    fflush(stdout);
                }
            }
        }
#else   // SANDBOX
        isBMGroupMember = true;
#endif  // SANDBOX
        saved_uid = geteuid();
        deleteLoginItem = CheckDeleteFile(human_user_name);
        if (CheckDeleteFile(pw->pw_name)) {
            deleteLoginItem = true;
        }
        if (CheckDeleteFile(pw->pw_gecos)) {
            deleteLoginItem = true;
        }
        if (!isBMGroupMember) {
            deleteLoginItem = true;
        }

        // Set login item for this user
        bool useOSASript = false;
        
        if ((compareOSVersionTo(10, 13) < 0)
            || (strcmp(loginName, human_user_name) == 0) 
                || (strcmp(loginName, pw->pw_name) == 0) 
                    || (strcmp(loginName, pw->pw_gecos) == 0)) {
            useOSASript = true;
        }
#if USE_OSASCRIPT_FOR_ALL_LOGGED_IN_USERS
        if (! useOSASript) {
            useOSASript = IsUserLoggedIn(pw->pw_name);
        }
#endif
       if (useOSASript) {
            snprintf(s, sizeof(s), "/Users/%s/Library/LaunchAgents/edu.berkeley.boinc.plist", pw->pw_name);
            boinc_delete_file(s);
            printf("[2] calling SetLoginItemOSAScript for user %s, euid = %d, deleteLoginItem = %d\n", 
                pw->pw_name, geteuid(), deleteLoginItem);
            fflush(stdout);
           SetLoginItemOSAScript(brandID, deleteLoginItem, pw->pw_name);

            printf("[2] calling FixLaunchServicesDataBase for user %s\n", pw->pw_name);
            FixLaunchServicesDataBase(pw->pw_uid, brandID);
        } else {
            printf("[2] calling SetLoginItemLaunchAgent for user %s, euid = %d, deleteLoginItem = %d\n", 
                pw->pw_name, geteuid(), deleteLoginItem);
            fflush(stdout);
            // SetLoginItemLaunchAgent will run helper app which will call FixLaunchServicesDataBase()
            SetLoginItemLaunchAgent(brandID, oldBrandID, deleteLoginItem, pw);
        }
        if (isBMGroupMember) {
            // For some reason we need to call getpwnam again on OS 10.5
            pw = getpwnam(human_user_name);
            if (pw == NULL) {           // "Deleted Users", "Shared", etc.
                printf("[2] ERROR: %s was in getpwnam data base but now is not!\n", human_user_name);
                fflush(stdout);
                continue;
            }
            SetSkinInUserPrefs(pw->pw_name, skinName[brandID]);

            if (setSaverForAllUsers) {
                seteuid(pw->pw_uid);    // Temporarily set effective uid to this user
                sprintf(s, "/Library/Screen Savers/%s.saver", saverName[brandID]);
                err = SetScreenSaverSelection(saverName[brandID], s, 0);
                seteuid(saved_uid);     // Set effective uid back to privileged user
                // This seems to work also:
                // sprintf(s, "su -l \"%s\" -c 'defaults -currentHost write com.apple.screensaver moduleDict -dict moduleName \"%s\" path \"/Library/Screen Savers/%s.saver\" type 0'", pw->pw_name, saverName[brandID], s);
                // callPosixSpawn(s);
            }
        }

        // Delete the BOINC Manager's wxSingleInstanceChecker lock file, in case
        // it was not deleted (such as due to a crash.)
        // Lock file name always has "BOINC Manager" even if the application is
        // branded, due to SetAppName(wxT("BOINC Manager")) in CBOINCGUIApp::OnInit().
        // This path must match that in CBOINCGUIApp::DetectDuplicateInstance()
        sprintf(cmd, "sudo -u \"%s\" rm -f \"/Users/%s/Library/Application Support/BOINC/BOINC Manager-%s\"",
                            pw->pw_name, pw->pw_name, pw->pw_name);
        err = callPosixSpawn(cmd);
        REPORT_ERROR(err);
        printf("[2] %s returned %d\n", cmd, err);
        fflush(stdout);

    }   // End for (userIndex=0; userIndex< human_user_names.size(); ++userIndex)

    ResynchDSSystem();
    
    BOINCTranslationCleanup();

    return noErr;
}


OSErr GetCurrentScreenSaverSelection(passwd *pw, char *moduleName, size_t maxLen) {
    char                buf[1024];
    FILE                *f;
    char                *p, *q;
    int                 i;

    *moduleName = '\0';
    sprintf(buf, "su -l \"%s\" -c 'defaults -currentHost read com.apple.screensaver moduleDict'", pw->pw_name);
    f = popen(buf, "r");
    if (f == NULL) {
        REPORT_ERROR(true);
        return 0;
    }
    
    while (PersistentFGets(buf, sizeof(buf), f))
    {
        p = strstr(buf, "moduleName = ");
        if (p) {
            p += 13;    // Point past "moduleName = "
            q = moduleName;
            for (i=0; i<maxLen-1; ++i) {
                if (*p == '"') {
                    ++p;
                    continue;
                }
                if (*p == ';') break;
                *q++ = *p++;
            }
            *q = '\0';
            pclose(f);
            return 0;
        }
    }
    
    pclose(f);
    return fnfErr;
}


OSErr SetScreenSaverSelection(char *moduleName, char *modulePath, int type) {
    OSErr err = noErr;
    CFStringRef preferenceName = CFSTR("com.apple.screensaver");
    CFStringRef mainKeyName = CFSTR("moduleDict");
    CFDictionaryRef emptyData;
    CFMutableDictionaryRef newData;
    Boolean success;

    CFStringRef nameKey = CFStringCreateWithCString(NULL, "moduleName", kCFStringEncodingASCII);
    CFStringRef nameValue = CFStringCreateWithCString(NULL, moduleName, kCFStringEncodingASCII);
		
    CFStringRef pathKey = CFStringCreateWithCString(NULL, "path", kCFStringEncodingASCII);
    CFStringRef pathValue = CFStringCreateWithCString(NULL, modulePath, kCFStringEncodingASCII);
    
    CFStringRef typeKey = CFStringCreateWithCString(NULL, "type", kCFStringEncodingASCII);
    CFNumberRef typeValue = CFNumberCreate(NULL, kCFNumberIntType, &type);
    
    emptyData = CFDictionaryCreate(NULL, NULL, NULL, 0, NULL, NULL);
    if (emptyData == NULL) {
        REPORT_ERROR(true);
        CFRelease(nameKey);
        CFRelease(nameValue);
        CFRelease(pathKey);
        CFRelease(pathValue);
        CFRelease(typeKey);
        CFRelease(typeValue);
        return(-1);
    }

    newData = CFDictionaryCreateMutableCopy(NULL,0, emptyData);

    if (newData == NULL)
	{
        REPORT_ERROR(true);
        CFRelease(nameKey);
        CFRelease(nameValue);
        CFRelease(pathKey);
        CFRelease(pathValue);
        CFRelease(typeKey);
        CFRelease(typeValue);
        CFRelease(emptyData);
        return(-1);
    }

    CFDictionaryAddValue(newData, nameKey, nameValue); 	
    CFDictionaryAddValue(newData, pathKey, pathValue); 	
    CFDictionaryAddValue(newData, typeKey, typeValue); 	

    CFPreferencesSetValue(mainKeyName, newData, preferenceName, kCFPreferencesCurrentUser, kCFPreferencesCurrentHost);
    success = CFPreferencesSynchronize(preferenceName, kCFPreferencesCurrentUser, kCFPreferencesCurrentHost);

    if (!success) {
        REPORT_ERROR(true);
        err = -1;
    }
    
    CFRelease(nameKey);
    CFRelease(nameValue);
    CFRelease(pathKey);
    CFRelease(pathValue);
    CFRelease(typeKey);
    CFRelease(typeValue);
    CFRelease(emptyData);
    CFRelease(newData);

    return err;
}


OSErr Initialize()	/* Initialize some managers */
{
//    InitCursor();

    return AEInstallEventHandler( kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP((AEEventHandlerProcPtr)QuitAppleEventHandler), 0, false );
}


static Boolean IsUserLoggedIn(const char *userName){
    char s[1024];
    
    sprintf(s, "w -h \"%s\"", userName);
    FILE *f = popen(s, "r");
    REPORT_ERROR(!f);
    if (f) {
        if (PersistentFGets(s, sizeof(s), f) != NULL) {
            pclose (f);
            printf("User %s is currently logged in\n", userName);
            return true; // this user is logged in (perhaps via fast user switching)
        }
        pclose (f);         
    }
    return false;
}


void FindAllVisibleUsers() {
    FILE                *f;
    char                human_user_name[256];
    char                cmd[256];
    char                buf[256];
    char                *p;
    int                 flag;
    int                 userIndex;

    f = popen("dscl . list /Users UniqueID", "r");
    REPORT_ERROR(!f);
    if (f) {
        while (PersistentFGets(buf, sizeof(buf), f)) {
            p = strrchr(buf, ' ');
            if (p) {
                int id = atoi(p+1);
                if (id < 501) continue;
                human_user_IDs.push_back((uid_t)id);

                while (p > buf) {
                    if (*p != ' ') break;
                    --p;
                }

               *(p+1) = '\0';
                human_user_names.push_back(string(buf));
                *(p+1) = ' ';
            }
        }
        pclose(f);
    }
    
    for (userIndex=human_user_names.size(); userIndex>0; --userIndex) {
        flag = 0;
        strlcpy(human_user_name, human_user_names[userIndex-1].c_str(), sizeof(human_user_name));
        sprintf(cmd, "dscl . -read \"/Users/%s\" NFSHomeDirectory", human_user_name);    
        f = popen(cmd, "r");
        REPORT_ERROR(!f);
        if (f) {
            while (PersistentFGets(buf, sizeof(buf), f)) {
                p = strrchr(buf, ' ');
                if (p) {
                    if (strstr(p, "/var/empty") != NULL) {
                        flag = 1;
                        break;
                    }
                }
            }
            pclose(f);
        }

        if (flag) {
            sprintf(cmd, "dscl . -read \"/Users/%s\" UserShell", human_user_name);    
            f = popen(cmd, "r");
            REPORT_ERROR(!f);
            if (f) {
                while (PersistentFGets(buf, sizeof(buf), f)) {
                    p = strrchr(buf, ' ');
                    if (p) {
                        if (strstr(p, "/usr/bin/false") != NULL) {
                            flag |= 2;
                            break;
                        }
                   }
                }
                pclose(f);
            }
        }
        
        if (flag == 3) { // if (Home Directory == "/var/empty") && (UserShell == "/usr/bin/false")
            human_user_names.erase(human_user_names.begin()+userIndex-1);
            human_user_IDs.erase(human_user_IDs.begin()+userIndex-1);
        }
    }
}


long GetBrandID(char *path)
{
    long iBrandId;

    iBrandId = 0;   // Default value
    
    FILE *f = fopen(path, "r");
    if (f) {
        fscanf(f, "BrandId=%ld\n", &iBrandId);
        fclose(f);
    }
    if ((iBrandId < 0) || (iBrandId > (NUMBRANDS-1))) {
        iBrandId = 0;
    }
    return iBrandId;
}


int TestRPCBind()
{
    sockaddr_in addr;
    int lsock;
    int retval;
    
    lsock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0)
        return -153;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(31416);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int one = 1;
    retval = setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, (char*)&one, 4);

    if (! retval)
        retval = bind(lsock, (const sockaddr*)(&addr), (socklen_t)sizeof(addr));

    if (! retval)
        retval = listen(lsock, 999);
    
    close(lsock);
    
    return retval;
}


pid_t FindProcessPID(char* name, pid_t thePID)
{
    FILE *f;
    char buf[1024];
    size_t n = 0;
    pid_t aPID;
    
    if (name != NULL)     // Search ny name
        n = strlen(name);

    f = popen("ps -a -x -c -o command,pid", "r");
    if (f == NULL) {
        REPORT_ERROR(true);
        return 0;
    }
    
    while (PersistentFGets(buf, sizeof(buf), f))
    {
        if (name != NULL) {     // Search by name
            if (strncmp(buf, name, n) == 0)
            {
                aPID = atol(buf+16);
                pclose(f);
                return aPID;
            }
        } else {      // Search by PID
            aPID = atol(buf+16);
            if (aPID == thePID) {
                pclose(f);
                return aPID;
            }
        }
    }
    pclose(f);
    return 0;
}


// Uses usleep to sleep for full duration even if a signal is received
static void SleepSeconds(double seconds) {
    double end_time = dtime() + seconds - 0.01;
    // sleep() and usleep() can be interrupted by SIGALRM,
    // so we may need multiple calls
    //
    while (1) {
        if (seconds >= 1) {
            sleep((unsigned int) seconds);
        } else {
            usleep((int)fmod(seconds*1000000, 1000000));
        }
        seconds = end_time - dtime();
        if (seconds <= 0) break;
    }
}


static OSErr QuitAppleEventHandler( const AppleEvent *appleEvt, AppleEvent* reply, UInt32 refcon )
{
    gQuitFlag =  true;
    
    return noErr;
}


#define NOT_IN_TOKEN                0
#define IN_SINGLE_QUOTED_TOKEN      1
#define IN_DOUBLE_QUOTED_TOKEN      2
#define IN_UNQUOTED_TOKEN           3

static int parse_posic_spawn_command_line(char* p, char** argv) {
    int state = NOT_IN_TOKEN;
    int argc=0;

    while (*p) {
        switch(state) {
        case NOT_IN_TOKEN:
            if (isspace(*p)) {
            } else if (*p == '\'') {
                p++;
                argv[argc++] = p;
                state = IN_SINGLE_QUOTED_TOKEN;
                break;
            } else if (*p == '\"') {
                p++;
                argv[argc++] = p;
                state = IN_DOUBLE_QUOTED_TOKEN;
                break;
            } else {
                argv[argc++] = p;
                state = IN_UNQUOTED_TOKEN;
            }
            break;
        case IN_SINGLE_QUOTED_TOKEN:
            if (*p == '\'') {
                if (*(p-1) == '\\') break;
                *p = 0;
                state = NOT_IN_TOKEN;
            }
            break;
        case IN_DOUBLE_QUOTED_TOKEN:
            if (*p == '\"') {
                if (*(p-1) == '\\') break;
                *p = 0;
                state = NOT_IN_TOKEN;
            }
            break;
        case IN_UNQUOTED_TOKEN:
            if (isspace(*p)) {
                *p = 0;
                state = NOT_IN_TOKEN;
            }
            break;
        }
        p++;
    }
    argv[argc] = 0;
    return argc;
}

#include <spawn.h>

int callPosixSpawn(const char *cmdline) {
    char command[1024];
    char progName[1024];
    char progPath[MAXPATHLEN];
    char* argv[100];
    int argc = 0;
    char *p;
    pid_t thePid = 0;
    int result = 0;
    int status = 0;
    extern char **environ;
    
    // Make a copy of cmdline because parse_posic_spawn_command_line modifies it
    strlcpy(command, cmdline, sizeof(command));
    argc = parse_posic_spawn_command_line(const_cast<char*>(command), argv);
    strlcpy(progPath, argv[0], sizeof(progPath));
    strlcpy(progName, argv[0], sizeof(progName));
    p = strrchr(progName, '/');
    if (p) {
        argv[0] = p+1;
    } else {
        argv[0] = progName;
    }
    
#if VERBOSE_TEST
    print_to_log("***********");
    for (int i=0; i<argc; ++i) {
        print_to_log("argv[%d]=%s", i, argv[i]);
    }
    print_to_log("***********\n");
#endif

    errno = 0;

    result = posix_spawnp(&thePid, progPath, NULL, NULL, argv, environ);
#if VERBOSE_TEST
    print_to_log("callPosixSpawn command: %s", cmdline);
    print_to_log("callPosixSpawn: posix_spawnp returned %d: %s", result, strerror(result));
#endif
    if (result) {
        return result;
    }
// CAF    int val =
    waitpid(thePid, &status, WUNTRACED);
// CAF        if (val < 0) printf("first waitpid returned %d\n", val);
    if (status != 0) {
#if VERBOSE_TEST
        print_to_log("waitpid() returned status=%d", status);
#endif
        result = status;
    } else {
        if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
            if (result == 1) {
#if VERBOSE_TEST
                print_to_log("WEXITSTATUS(status) returned 1, errno=%d: %s", errno, strerror(errno));
#endif
                result = errno;
            }
#if VERBOSE_TEST
            else if (result) {
                print_to_log("WEXITSTATUS(status) returned %d", result);
            }
#endif
        }   // end if (WIFEXITED(status)) else
    }       // end if waitpid returned 0 sstaus else
    
    return result;
}


void strip_cr(char *buf)
{
    char *theCR;

    theCR = strrchr(buf, '\n');
    if (theCR)
        *theCR = '\0';
    theCR = strrchr(buf, '\r');
    if (theCR)
        *theCR = '\0';
}

// For debugging
void print_to_log(const char *format, ...) {
    va_list args;
    char buf[256];
    time_t t;

    time(&t);
    strcpy(buf, asctime(localtime(&t)));
    strip_cr(buf);

    fputs(buf, stdout);
    fputs("   ", stdout);

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    fputs("\n", stdout);
    fflush(stdout);
}


void CopyPreviousErrorsToLog() {
    FILE *f;
    char buf[MAXPATHLEN];
    snprintf(buf, sizeof(buf), "/tmp/%s/BOINC_Installer_Errors", tempDirName);
    f = fopen(buf, "r");
    if (!f) return;
    while (PersistentFGets(buf, sizeof(buf), f)) {
        printf("%s", buf);
    }
    fflush(stdout);
    fclose(f);
}
