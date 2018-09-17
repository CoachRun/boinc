// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2016 University of California
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

///
/// @defgroup BOINCMgr BOINC Manager
/// The BOINC Manager
/// @{

#ifndef BOINC_BOINCGUIAPP_H
#define BOINC_BOINCGUIAPP_H

#if defined(__GNUG__) && !defined(__APPLE__)
#pragma interface "BOINCGUIApp.cpp"
#endif

///
/// Which view is on display
///
#define BOINC_ADVANCEDGUI                   1
#define BOINC_SIMPLEGUI                     2


class wxLogBOINC;
class CBOINCBaseFrame;
class CMainDocument;
class CTaskBarIcon;
class CSkinManager;
class CDlgEventLog;
class CRPCFinishedEvent;

#ifdef __WXMAC__
    OSErr               QuitAppleEventHandler(const AppleEvent *appleEvt, AppleEvent* reply, UInt32 refcon);
#endif

class CBOINCGUIApp : public wxApp {

    DECLARE_DYNAMIC_CLASS(CBOINCGUIApp)

protected:
    int                 OnExit();
#ifndef __WXMAC__
    void                OnEndSession(wxCloseEvent& event);
#endif
    
    void                OnInitCmdLine(wxCmdLineParser &parser);
    bool                OnCmdLineParsed(wxCmdLineParser &parser);

    bool                DetectDuplicateInstance();
    void                DetectExecutableName();
    void                DetectRootDirectory();
    void                DetectDataDirectory();

    void                InitSupportedLanguages();

    int                 IdleTrackerAttach();
    int                 IdleTrackerDetach();

    wxConfig*           m_pConfig;
    wxLocale*           m_pLocale;
    wxLogBOINC*         m_pLog;
    wxSingleInstanceChecker* m_pInstanceChecker;

    CSkinManager*       m_pSkinManager;
    CBOINCBaseFrame*    m_pFrame;
    CMainDocument*      m_pDocument;
    CTaskBarIcon*       m_pTaskBarIcon;
    CDlgEventLog*       m_pEventLog;
    bool                m_bEventLogWasActive;
    bool                m_bProcessingActivateAppEvent;
#ifdef __WXMAC__
    CTaskBarIcon*       m_pMacDockIcon;
#endif
    wxString            m_strBOINCMGRExecutableName;
    wxString            m_strBOINCMGRRootDirectory;
    wxString            m_strBOINCMGRDataDirectory;
    wxString            m_strHostNameArg;
    wxString            m_strPasswordArg;
    wxString            m_strBOINCArguments;
    int                 m_iRPCPortArg;

    bool                m_bBOINCMGRAutoStarted;
    int                 m_iBOINCMGRDisableAutoStart;
    int                 m_iShutdownCoreClient;
    int                 m_iDisplayExitDialog;
    int                 m_iDisplayShutdownConnectedClientDialog;

    bool                m_bGUIVisible;
    
    int                 m_iGUISelected;
    bool                m_bDebugSkins;
    bool                m_bMultipleInstancesOK;
    bool                m_bFilterEvents;
    bool                m_bAboutDialogIsOpen;
    bool                m_bRunDaemon;  
    bool                m_bNeedRunDaemon;  

    // The last value defined in the wxLanguage enum is wxLANGUAGE_USER_DEFINED.
    // defined in: wx/intl.h
    wxArrayString       m_astrLanguages;
    wxString            m_strISOLanguageCode;
    
    int                 m_bSafeMessageBoxDisplayed;

public:

    bool                OnInit();
    void                SaveState();

    wxLocale*           GetLocale()                 { return m_pLocale; }
    CSkinManager*       GetSkinManager()            { return m_pSkinManager; }
    CBOINCBaseFrame*    GetFrame()                  { return m_pFrame; }
    CMainDocument*      GetDocument()               { return m_pDocument; }
    wxString            GetExecutableName()         { return m_strBOINCMGRExecutableName; }
    wxString            GetRootDirectory()          { return m_strBOINCMGRRootDirectory; }
    wxString            GetDataDirectory()          { return m_strBOINCMGRDataDirectory; }
    wxString            GetClientHostNameArg()      { return m_strHostNameArg; }    
    wxString            GetClientPasswordArg()      { return m_strPasswordArg; }    
    wxString            GetArguments()              { return m_strBOINCArguments; }
    int                 GetClientRPCPortArg()       { return m_iRPCPortArg; }
    CDlgEventLog*       GetEventLog()               { return m_pEventLog; }
    CTaskBarIcon*       GetTaskBarIcon()            { return m_pTaskBarIcon; }

    bool                IsAnotherInstanceRunning()  { return m_pInstanceChecker->IsAnotherRunning(); }
    bool                IsMgrMultipleInstance()     { return m_bMultipleInstancesOK; }

#ifdef __WXMAC__
    void                OnFinishInit();
    CTaskBarIcon*       GetMacDockIcon()            { return m_pMacDockIcon; }
    int                 ShouldShutdownCoreClient()  { return true; }
#else
    int                 ShouldShutdownCoreClient()  { return m_iShutdownCoreClient; }
#endif

    int                 GetBOINCMGRDisableAutoStart()
                                                    { return m_iBOINCMGRDisableAutoStart; }
    void                SetBOINCMGRDisableAutoStart(int iDisableAutoStart)
                                                    { m_iBOINCMGRDisableAutoStart = iDisableAutoStart; }

    int                 GetBOINCMGRDisplayExitMessage()
                                                    { return m_iDisplayExitDialog; }
    void                SetBOINCMGRDisplayExitMessage(int iDisplayExitMessage)
                                                    { m_iDisplayExitDialog = iDisplayExitMessage; }

    int                 GetBOINCMGRDisplayShutdownConnectedClientMessage()
                                                    { return m_iDisplayShutdownConnectedClientDialog; }
    void                SetBOINCMGRDisplayShutdownConnectedClientMessage(int iDisplayShutdownConnectedClientDialog)
                                                    { m_iDisplayShutdownConnectedClientDialog = iDisplayShutdownConnectedClientDialog; }

    bool                GetRunDaemon()
                                                    { return m_bRunDaemon; }  
    void                SetRunDaemon(bool bRunDaemon)  
                                                    { m_bRunDaemon = bRunDaemon; }  
  
    bool                GetNeedRunDaemon()  
                                                    { return m_bNeedRunDaemon; }  

    wxArrayString&      GetSupportedLanguages()     { return m_astrLanguages; }
    wxString            GetISOLanguageCode()        { return m_strISOLanguageCode; }
    void                SetISOLanguageCode(wxString strISOLanguageCode)
                                                    { m_strISOLanguageCode = strISOLanguageCode; }
    
    void                SetEventLogWasActive(bool wasActive) { m_bEventLogWasActive = wasActive; }
    void                DisplayEventLog(bool bShowWindow = true);
    void                OnEventLogClose();

    void                FireReloadSkin();
    void                FrameClosed()               { m_pFrame = NULL; }

    int                 StartBOINCScreensaverTest();
    int                 StartBOINCDefaultScreensaverTest();

    bool                SetActiveGUI(int iGUISelection, bool bShowWindow = true);
    
    void                OnActivateApp( wxActivateEvent& event );
    void                OnRPCFinished( CRPCFinishedEvent& event );
    
    int                 ConfirmExit();

    int                 SafeMessageBox(
                            const wxString& message,
                            const wxString& caption = wxMessageBoxCaptionStr,
                            long style = wxOK | wxCENTRE,
                            wxWindow *parent = NULL,
                            int x = wxDefaultCoord,
                            int y = wxDefaultCoord
                        );

    bool                IsApplicationVisible();
    void                ShowApplication(bool bShow);
    bool                ShowInterface();
    bool                ShowNotifications();

    bool                IsModalDialogDisplayed();
    bool                IsSafeMesageBoxDisplayed() { return (m_bSafeMessageBoxDisplayed != 0); };

    int                 FilterEvent(wxEvent &event);


    int                 UpdateSystemIdleDetection();
    
    void                SetEventFiltering(bool set) { m_bFilterEvents = set; }
    
    void                SetAboutDialogIsOpen(bool set) { m_bAboutDialogIsOpen = set; }
    bool                GetAboutDialogIsOpen() { return m_bAboutDialogIsOpen; }

#ifdef __WXMAC__
    // The following Cocoa routines are in CBOINCGUIApp.mm
    //
    bool                WasFileModifiedBeforeSystemBoot(char * filePath);
    void                HideThisApp(void);
    void                getDisplayNameForThisApp(char* pathBuf, size_t bufSize);
    
#if !wxCHECK_VERSION(3,0,1)
// This should be fixed after wxCocoa 3.0.0:
// http://trac.wxwidgets.org/ticket/16156

    // Override standard wxCocoa wxApp::CallOnInit() to allow Manager
    // to run properly when launched hidden on login via Login Item. 
    bool                CallOnInit();
#endif

    void                CheckPartialActivation();
#endif

DECLARE_EVENT_TABLE()
};

DECLARE_APP(CBOINCGUIApp)


#endif

/// @}
