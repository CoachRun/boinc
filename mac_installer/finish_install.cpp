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

//
//  main.cpp
//  boinc_Finish_Install

// Usage: boinc_Finish_Install [-d] [appName]
//
// * Deletes Login Items of all possible branded and unbranded BOINC Managers for current user.
// * If first argument is -d then also kills the application specified by the second argument.
// * If first argument is the name of a branded or unbranded BOINC Manager, adds it as a Login
//   Item for the current user and launches it.
//
// TODO: Do we ned to code sign this app?
//

#define VERBOSE_TEST 0  /* for debugging callPosixSpawn */
#if VERBOSE_TEST
#define CREATE_LOG 1    /* for debugging */
#else
#define CREATE_LOG 0    /* for debugging */
#endif
#define USE_SPECIAL_LOG_FILE 1


#include <Carbon/Carbon.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>    // waitpid
#include <sys/param.h>  // for MAXPATHLEN
#include <string.h>
#include <ctype.h>
#include <cerrno>
#include <sys/time.h>
#include <stdarg.h>
#include <unistd.h>
#include <pwd.h>    // getpwname, getpwuid, getuid
#include <spawn.h>

#include "mac_branding.h"

int callPosixSpawn(const char *cmd);
long GetBrandID(char *path);
static void FixLaunchServicesDataBase(void);
void print_to_log_file(const char *format, ...);
void strip_cr(char *buf);

int main(int argc, const char * argv[]) {
    int                     i, err;
    char                    cmd[2048];
    passwd                  *pw;

    for (i=0; i<NUMBRANDS; i++) {
        snprintf(cmd, sizeof(cmd), "osascript -e 'tell application \"System Events\" to delete login item \"%s\"'", appName[i]);
        err = callPosixSpawn(cmd);
        if (err) {
            fprintf(stderr, "Command: %s\n", cmd);
            fprintf(stderr, "Delete login item containing %s returned error %d\n", appName[i], err);
            fflush(stderr);
        }
    }

    FixLaunchServicesDataBase();

    for (i=1; i<argc; i+=2) {
        if (strcmp(argv[i], "-d") == 0) {
            // If this user was previously authorized to run the Manager, the Login Item
            // may have launched the Manager before this app deleted that Login Item. To
            // guard against this, we kill the Manager (for this user only) if it is running.
            // 
            snprintf(cmd, sizeof(cmd), "killall -u %d -9 \"%s\"", getuid(), argv[i+1]);
            err = callPosixSpawn(cmd);
            if (err) {
                fprintf(stderr, "Command: %s\n", cmd);
                fprintf(stderr, "killall %s returned error %d\n", argv[i+1], err);
                fflush(stderr);
            }
        } else if (strcmp(argv[i], "-a") == 0) {
            snprintf(cmd, sizeof(cmd), "osascript -e 'tell application \"System Events\" to make new login item at end with properties {path:\"/Applications/%s.app\", hidden:true, name:\"%s\"}'", argv[i+1], argv[i+1]);
            err = callPosixSpawn(cmd);
            if (err) {
                fprintf(stderr, "Command: %s\n", cmd);
                fprintf(stderr, "Make new login item for %s returned error %d\n", argv[i+1], err);
                fflush(stderr);
            }
        
            snprintf(cmd, sizeof(cmd), "open -jg \"/Applications/%s.app\"", argv[i+1]);
            err = callPosixSpawn(cmd);
            if (err) {
                fprintf(stderr, "Command: %s\n", cmd);
                fprintf(stderr, "Make login item for %s returned error %d\n", argv[i+1], err);
                fflush(stderr);
            }
        }   // end if (strcmp(argv[i], "-a") == 0)
    }   // end for (i=i; i<argc; i+=2)
    
    pw = getpwuid(getuid());
    
    snprintf(cmd, sizeof(cmd), "rm -f \"/Users/%s/Library/LaunchAgents/edu.berkeley.boinc.plist\"", pw->pw_name);
    callPosixSpawn(cmd);
    
    return 0;
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


// If there are other copies of BOINC Manager with different branding
// on the system, Noitifications may display the icon for the wrong
// branding, due to the Launch Services database having one of the
// other copies of BOINC Manager as the first entry. Each user has
// their own copy of the Launch Services database, so this must be
// done for each user.
//
// This probably will happen only on BOINC development systems where
// Xcode has generated copies of BOINC Manager.
static void FixLaunchServicesDataBase() {
    long brandID = 0;
    char boincPath[MAXPATHLEN];
    char cmd[MAXPATHLEN+250];
    long i, n;
    CFArrayRef appRefs = NULL;
    OSStatus err;

    brandID = GetBrandID("/Library/Application Support/BOINC Data/Branding");

    CFStringRef bundleID = CFSTR("edu.berkeley.boinc");

    // LSCopyApplicationURLsForBundleIdentifier is not available before OS 10.10,
    // but this app is used only for OS 10.13 and later
        appRefs = LSCopyApplicationURLsForBundleIdentifier(bundleID, NULL);
        if (appRefs == NULL) {
            print_to_log_file("Call to LSCopyApplicationURLsForBundleIdentifier returned NULL");
            goto registerOurApp;
        }
        n = CFArrayGetCount(appRefs);   // Returns all results at once, in database order
        print_to_log_file("LSCopyApplicationURLsForBundleIdentifier returned %ld results", n);

    for (i=0; i<n; ++i) {     // Prevent infinite loop
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
        if (strncmp(boincPath, appPath[brandID], sizeof(boincPath)) == 0) {
            print_to_log_file("**** Keeping %s", boincPath);
            if (appRefs) CFRelease(appRefs);
            return;     // Our (possibly branded) BOINC Manager app is now at top of database
        }
        print_to_log_file("Unregistering %3ld: %s", i, boincPath);
        // Remove this entry from the Launch Services database
        sprintf(cmd, "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Versions/Current/Support/lsregister -u \"%s\"", boincPath);
        err = callPosixSpawn(cmd);
        if (err) {
            print_to_log_file("*** lsregister -u call returned error %d for %s", err, boincPath);
        }
    }

registerOurApp:
    if (appRefs) CFRelease(appRefs);

    // We have exhausted the Launch Services database without finding our
    // (possibly branded) BOINC Manager app, so add it to the dataabase
    print_to_log_file("%s was not found in Launch Services database; registering it now", appPath[brandID]);
    sprintf(cmd, "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Versions/Current/Support/lsregister \"%s\"", appPath[brandID]);
    err = callPosixSpawn(cmd);
    if (err) {
        print_to_log_file("*** lsregister call returned error %d for %s", err, appPath[brandID]);
        fflush(stdout);
    }
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
    print_to_log_file("***********");
    for (int i=0; i<argc; ++i) {
        print_to_log_file("argv[%d]=%s", i, argv[i]);
    }
    print_to_log_file("***********\n");
#endif

    errno = 0;

    result = posix_spawnp(&thePid, progPath, NULL, NULL, argv, environ);
#if VERBOSE_TEST
    print_to_log_file("callPosixSpawn command: %s", cmdline);
    print_to_log_file("callPosixSpawn: posix_spawnp returned %d: %s", result, strerror(result));
#endif
    if (result) {
        return result;
    }
// CAF    int val =
    waitpid(thePid, &status, WUNTRACED);
// CAF        if (val < 0) printf("first waitpid returned %d\n", val);
    if (status != 0) {
#if VERBOSE_TEST
        print_to_log_file("waitpid() returned status=%d", status);
#endif
        result = status;
    } else {
        if (WIFEXITED(status)) {
            result = WEXITSTATUS(status);
            if (result == 1) {
#if VERBOSE_TEST
                print_to_log_file("WEXITSTATUS(status) returned 1, errno=%d: %s", errno, strerror(errno));
#endif
                result = errno;
            }
#if VERBOSE_TEST
            else if (result) {
                print_to_log_file("WEXITSTATUS(status) returned %d", result);
            }
#endif
        }   // end if (WIFEXITED(status)) else
    }       // end if waitpid returned 0 sstaus else
    
    return result;
}


void print_to_log_file(const char *format, ...) {
#if CREATE_LOG
    va_list args;
    char buf[256];
    time_t t;
#if USE_SPECIAL_LOG_FILE
    strlcpy(buf, getenv("HOME"), sizeof(buf));
    strlcat(buf, "/Documents/test_log.txt", sizeof(buf));
    FILE *f;
    f = fopen(buf, "a");
    if (!f) return;

//  freopen(buf, "a", stdout);
//  freopen(buf, "a", stderr);
#else
    #define f stderr
#endif
    time(&t);
    strlcpy(buf, asctime(localtime(&t)), sizeof(buf));

    strip_cr(buf);

    fputs(buf, f);
    fputs("   ", f);

    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    
    fputs("\n", f);
#if USE_SPECIAL_LOG_FILE
    fflush(f);
    fclose(f);
#endif
#endif
}

#if CREATE_LOG
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
#endif    // CREATE_LOG
