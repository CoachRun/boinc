// Berkeley Open Infrastructure for Network Computing
// http://boinc.berkeley.edu
// Copyright (C) 2017 University of California
//
// This is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation;
// either version 2.1 of the License, or (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// To view the GNU Lesser General Public License visit
// http://www.gnu.org/copyleft/lesser.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

//
//  macglutfix.m
//

#define CREATE_LOG 0    // Set to 1 for debugging

#define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED

#include <Cocoa/Cocoa.h>
#include <mach/mach_time.h>
#include <pthread.h>
#import <OpenGL/CGLIOSurface.h>
#import <GLKit/GLKit.h>
#include <servers/bootstrap.h>
#import "MultiGPUMig.h"
#import "MultiGPUMigServer.h"
#include "x_opengl.h"
#include "boinc_gl.h"
#include "boinc_glut.h"

extern bool fullscreen; // set in graphics2_unix.cpp

// For unknown reason, "boinc_api.h" gets a compile 
// error here so just declare boinc_is_standalone()
//#include "boinc_api.h"
extern int boinc_is_standalone(void);

// int set_realtime(int period, int computation, int constraint);
void MacGLUTFix(bool isScreenSaver);
void BringAppToFront(void);

// The standard ScreenSaverView class actually sets the window 
// level to 2002, not the 1000 defined by NSScreenSaverWindowLevel 
// and kCGScreenSaverWindowLevel
#define RealSaverLevel 2002
// Glut sets the window level to 100 when it sets full screen mode
#define GlutFullScreenWindowLevel 100

// Delay when switching to screensaver mode to reduce annoying flashes
#define SAVERDELAY 30

void MacGLUTFix(bool isScreenSaver) {
    static NSMenu * emptyMenu;
    NSOpenGLContext * myContext = nil;
    NSView *myView = nil;
    NSWindow* myWindow = nil;

    if (! boinc_is_standalone()) {
        if (emptyMenu == nil) {
            emptyMenu = [ NSMenu alloc ];
            [ NSApp setMainMenu:emptyMenu ];
        }
    }

    myContext = [ NSOpenGLContext currentContext ];
    if (myContext)
        myView = [ myContext view ];
    if (myView)
        myWindow = [ myView window ];
    if (myWindow == nil)
        return;
    
    if (!isScreenSaver) {
        NSButton *closeButton = [myWindow standardWindowButton:NSWindowCloseButton ];
        [closeButton setEnabled:YES];
        [myWindow setDocumentEdited: NO];
        return;
    }

    // As of OS 10.13, app windows can no longer appear on top of screensaver
    // window, but we still use this method on older versions of OS X for
    // compatibility with older project graphics apps.
    if (!UseSharedOffscreenBuffer()) {
        // In screensaver mode, set our window's level just above
        // our BOINC screensaver's window level so it can appear
        // over it.  This doesn't interfere with the screensaver
        // password dialog because the dialog appears only after
        // our screensaver is closed.
        if ([ myWindow level ] == GlutFullScreenWindowLevel) {
            [ myWindow setLevel:RealSaverLevel+20 ];
        }
    }
}

#if 0
// NOT USED: See comments in animateOneFrame in Mac_Saver_ModuleView.m
// <https://developer.apple.com/library/content/technotes/tn2169>
int set_realtime(int period, int computation, int constraint) {
    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);
 
    const uint64_t NANOS_PER_MSEC = 1000000ULL;
    double clock2abs = ((double)timebase_info.denom / (double)timebase_info.numer) * NANOS_PER_MSEC;
 
    thread_time_constraint_policy_data_t policy;
    policy.period      = period;
    policy.computation = (uint32_t)(computation * clock2abs); // computation ms of work
    policy.constraint  = (uint32_t)(constraint * clock2abs);
//    policy.preemptible = FALSE;
    policy.preemptible = TRUE;

    int kr = thread_policy_set(pthread_mach_thread_np(pthread_self()),
                   THREAD_TIME_CONSTRAINT_POLICY,
                   (thread_policy_t)&policy,
                   THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "set_realtime() failed.\n");
        return 0;
    }
    return 1;
}
#endif

void BringAppToFront() {
    [ NSApp activateIgnoringOtherApps:YES ];
}

void HideThisApp() {
    [ NSApp hide:NSApp ];
}

// On OS 10.13 or later, use MachO comunication and IOSurfaceBuffer to
// display the graphics output of our child graphics apps in our window.

// Code adapted from Apple Developer Tech Support Sample Code MutiGPUIOSurface:
// <https://developer.apple.com/library/content/samplecode/MultiGPUIOSurface>

#define NUM_IOSURFACE_BUFFERS 2

@interface ServerController : NSObject <NSMachPortDelegate>
{
    NSMachPort *serverPort;
	NSMachPort *localPort;
    
	uint32_t serverPortName;
	uint32_t localPortName;
    
	NSMachPort *clientPort[16];
	uint32_t clientPortNames[16];
	uint32_t clientPortCount;
}
- (ServerController *)init;
- (kern_return_t)checkInClient:(mach_port_t)client_port index:(int32_t *)client_index;
- (void)portDied:(NSNotification *)notification;
- (void)sendIOSurfaceMachPortToClients: (uint32_t)index withMachPort:(mach_port_t) iosurface_port;

@end

static ServerController *myserverController;

static uint32_t currentFrameIndex;

static IOSurfaceRef ioSurfaceBuffers[NUM_IOSURFACE_BUFFERS];
static mach_port_t ioSurfaceMachPorts[NUM_IOSURFACE_BUFFERS];
static GLuint textureNames[NUM_IOSURFACE_BUFFERS];
static GLuint fboNames[NUM_IOSURFACE_BUFFERS];
static GLuint depthBufferName;

@implementation ServerController

- (ServerController *)init
{
	[[NSNotificationCenter defaultCenter] addObserver:self
	    selector:@selector(portDied:) name:NSPortDidBecomeInvalidNotification object:nil];
	
    mach_port_t servicePortNum = MACH_PORT_NULL;
    kern_return_t machErr;
    char *portName = "edu.berkeley.boincsaver";
    
// NSMachBootstrapServer is deprecated in OS 10.13, so use bootstrap_look_up
//	serverPort = [(NSMachPort *)([[NSMachBootstrapServer sharedInstance] servicePortWithName:@"edu.berkeley.boincsaver"]) retain];
    machErr = bootstrap_check_in(bootstrap_port, portName, &servicePortNum);
    if (machErr != KERN_SUCCESS) {
        		[NSApp terminate:self];
    }
    serverPort = (NSMachPort*)[NSMachPort portWithMachPort:servicePortNum];
	
	// Create a local dummy reply port to use with the mig reply stuff
	localPort = [[NSMachPort alloc] init];
	
	// Retrieve raw mach port names.
	serverPortName = [serverPort machPort];
	localPortName  = [localPort machPort];

	[serverPort setDelegate:self];
	[serverPort scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];

    // NOT USED: See comments in animateOneFrame in Mac_Saver_ModuleView.m
#if 0
    // This is an alternate method to get enough CPU cycles when we
    // are running in the background behind ScreensaverEngine.app
    set_realtime(0, 5, 33);
    //set_realtime(0, 5, 10);
    //set_realtime(33, 5, 33);
    //set_realtime(30, 3, 6);
    //set_realtime(30, 10, 20);
#endif

    return self;
}

- (void)portDied:(NSNotification *)notification
{
	NSPort *port = [notification object];
	if(port == serverPort)
	{
		[NSApp terminate:self];
	}
	else
	{		
		int i;
		for(i = 0; i < clientPortCount+1; i++)
		{
			if([clientPort[i] isEqual:port])
			{
				[clientPort[i] release];
				clientPort[i] = nil;
				clientPortNames[i] = 0;
			}
		}
	}
}

- (void)handleMachMessage:(void *)msg
{
	union __ReplyUnion___MGCMGSServer_subsystem reply;
	
	mach_msg_header_t *reply_header = (void *)&reply;
	kern_return_t kr;
	
	if(MGSServer_server(msg, reply_header) && reply_header->msgh_remote_port != MACH_PORT_NULL)
	{
		kr = mach_msg(reply_header, MACH_SEND_MSG, reply_header->msgh_size, 0, MACH_PORT_NULL,
			     0, MACH_PORT_NULL);
        if(kr != 0)
			[NSApp terminate:nil];
	}
}

- (kern_return_t)checkInClient:(mach_port_t)client_port index:(int32_t *)client_index
{	
	clientPortCount++;			// clients always start at index 1
	clientPortNames[clientPortCount] = client_port;
	clientPort[clientPortCount] = [[NSMachPort alloc] initWithMachPort:client_port];
	
	*client_index = clientPortCount;
	return 0;
}

kern_return_t _MGSCheckinClient(mach_port_t server_port, mach_port_t client_port,
			       int32_t *client_index)
{
	return [myserverController checkInClient:client_port index:client_index];
}

// For the MachO server, this is a no-op
kern_return_t _MGSDisplayFrame(mach_port_t server_port, int32_t frame_index, uint32_t iosurface_port)
{
	return 0;
}

- (void)sendIOSurfaceMachPortToClients:(uint32_t)index withMachPort:(mach_port_t)iosurface_port
{
	int i;
	for(i = 0; i < clientPortCount+1; i++)
	{
		if(clientPortNames[i])
		{
            // print_to_log_file("BOINCSCR: about to call _MGCDisplayFrame  with iosurface_port %d, IOSurfaceGetID %d and frameIndex %d", (int)iosurface_port, IOSurfaceGetID(ioSurfaceBuffers[index]), (int)index);
			_MGCDisplayFrame(clientPortNames[i], index, iosurface_port);
		}
	}
}
@end


void MacPassOffscreenBufferToScreenSaver() {
    NSOpenGLContext * myContext = [ NSOpenGLContext currentContext ];
    NSView *myView = [ myContext view ];
    GLsizei w = myView.bounds.size.width;
    GLsizei h = myView.bounds.size.height;

    GLuint name, namef;

    if (!myserverController) {
        myserverController = [[[ServerController alloc] init] retain];
    }

    if (!ioSurfaceBuffers[0]) {
        NSOpenGLContext * myContext = [ NSOpenGLContext currentContext ];
        NSView *myView = [ myContext view ];
        GLsizei w = myView.bounds.size.width;
        GLsizei h = myView.bounds.size.height;

        // Set up all of our iosurface buffers
        for(int i = 0; i < NUM_IOSURFACE_BUFFERS; i++) {
            ioSurfaceBuffers[i] = IOSurfaceCreate((CFDictionaryRef)@{
                (id)kIOSurfaceWidth: [NSNumber numberWithInt: w],
                (id)kIOSurfaceHeight: [NSNumber numberWithInt: h],
                (id)kIOSurfaceBytesPerElement: @4
                });
            ioSurfaceMachPorts[i] = IOSurfaceCreateMachPort(ioSurfaceBuffers[i]);
        }
    }
    
    if(!textureNames[currentFrameIndex])
    {
        CGLContextObj cgl_ctx = (CGLContextObj)[myContext CGLContextObj];
        
        glGenTextures(1, &name);
        
        glBindTexture(GL_TEXTURE_RECTANGLE, name);
        // At the moment, CGLTexImageIOSurface2D requires the GL_TEXTURE_RECTANGLE target
        CGLTexImageIOSurface2D(cgl_ctx, GL_TEXTURE_RECTANGLE, GL_RGBA, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                        ioSurfaceBuffers[currentFrameIndex], 0);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        // Generate an FBO and bind the texture to it as a render target.
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        
        glGenFramebuffers(1, &namef);
        glBindFramebuffer(GL_FRAMEBUFFER, namef);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, name, 0);

        if(!depthBufferName)
        {
            glGenRenderbuffers(1, &depthBufferName);
            glRenderbufferStorage(GL_TEXTURE_RECTANGLE, GL_DEPTH, w, h);
        }
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_RECTANGLE, depthBufferName);

        fboNames[currentFrameIndex] = namef;
        textureNames[currentFrameIndex] = name;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);   // First, draw to default FBO (screen FBO)

    // To see the original rendering in the graphics app's full-screen window
    // for debugging, temporarily enable this "glutSwapBuffers" statement
//    glutSwapBuffers();  // FOR DEBUGGING ONLY

    // Copy the default FBO to the IOSurface texture's FBO
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboNames[currentFrameIndex]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,w,h, 0,0,w,h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // To see the contents of the IOSurface in the graphics app's full-screen window
    // for debugging, temporarily change "#if 0" to #if 1" bin the next line:
 #if 0  // FOR DEBUGGING ONLY
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboNames[currentFrameIndex]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,w,h, 0,0,w,h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif

   glutSwapBuffers();
    [myserverController sendIOSurfaceMachPortToClients: currentFrameIndex
                        withMachPort:ioSurfaceMachPorts[currentFrameIndex]];
    glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

    currentFrameIndex = (currentFrameIndex + 1) % NUM_IOSURFACE_BUFFERS;
}

// Code for debugging:

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
#endif

void print_to_log_file(const char *format, ...) {
#if CREATE_LOG
    va_list args;
    char buf[256];
    time_t t;
    FILE *f;
    if (fullscreen) {
        // We can't write to our home directory if running as user / group boinc_project
        f = fopen("/Users/Shared/test_log.txt", "a");
    } else {
        strlcpy(buf, getenv("HOME"), sizeof(buf));
        strlcat(buf, "/Documents/test_log.txt", sizeof(buf));
        f = fopen(buf, "a");
        // freopen(buf, "a", stdout);
        //freopen(buf, "a", stderr);
    }
    if (!f) return;
    time(&t);
    strcpy(buf, asctime(localtime(&t)));
    strip_cr(buf);

    fputs(buf, f);
    fputs("   ", f);

    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    
    fputs("\n", f);
    fflush(f);
    fclose(f);
#endif
}

