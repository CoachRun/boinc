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

#ifndef __MACBITMAPCOMBOBOX__
#define __MACBITMAPCOMBOBOX__

#include <wx/choice.h>

WXDLLEXPORT_DATA(extern const wxChar) CBOINCBitmapChoiceNameStr[];
WXDLLEXPORT_DATA(extern const wxChar) CBOINCBitmapComboBoxNameStr[];

class CDrawLargeBitmapEvent;    // Forward declaration

class CBOINCBitmapChoice : public wxChoice 
{
    DECLARE_DYNAMIC_CLASS(CBOINCBitmapChoice )
    DECLARE_EVENT_TABLE()

public:
    CBOINCBitmapChoice() ;

    virtual ~CBOINCBitmapChoice();

    CBOINCBitmapChoice(wxWindow *parent, wxWindowID id,
            const wxString& value = wxT(""), 
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int n = 0, const wxString choices[] = NULL,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = CBOINCBitmapChoiceNameStr);

    void OnMouseDown(wxMouseEvent& event);
    void SetItemBitmap(unsigned int n, const wxBitmap& bitmap);
};

class CBOINCBitmapComboBox : public wxPanel 
{
    DECLARE_DYNAMIC_CLASS( CBOINCBitmapComboBox )
    DECLARE_EVENT_TABLE()

public:
    CBOINCBitmapComboBox() ;

    virtual ~CBOINCBitmapComboBox();

    CBOINCBitmapComboBox(wxWindow *parent, wxWindowID id,
            const wxString& value = wxT(""), 
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int n = 0, const wxString choices[] = NULL,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = CBOINCBitmapComboBoxNameStr);

    void SetItemBitmap(unsigned int n, const wxBitmap& bitmap);
    void SetStringSelection(const wxString& text);
    void SetSelection(int sel);
    int GetCount() { return m_ChoiceControl->GetCount(); }
    void * GetClientData(unsigned int n) const { return m_ChoiceControl->GetClientData(n); }
    void SetClientData(unsigned int n, void *data) { m_ChoiceControl->SetClientData(n, data); }
    int GetSelection() { return m_ChoiceControl->GetCurrentSelection(); }
    wxString GetValue() { return m_ChoiceControl->GetStringSelection(); }
    wxString GetString(unsigned int n) const { return m_ChoiceControl->GetString(n); }
    wxString GetStringSelection() { return m_ChoiceControl->GetStringSelection(); }

    int Append(const wxString& item, const wxBitmap& bitmap);
    int Append(const wxString& item, const wxBitmap& bitmap, void *clientData);
    int Insert(const wxString& item, const wxBitmap& bitmap, unsigned int pos);
    int Insert(const wxString& item, const wxBitmap& bitmap, unsigned int pos, void *clientData);
    void Delete(unsigned int n);
    void Clear();
    void SetToolTip(wxString& s);
    void SetToolTip(wxToolTip* tip);
    
private:
    void OnPaint(wxPaintEvent& event);
    void DrawLargeBitmap(CDrawLargeBitmapEvent& event);
    void OnSelection(wxCommandEvent& event);
    void EmptyBitmapCache();

    CBOINCBitmapChoice      *m_ChoiceControl;
    bool                    m_bHaveLargeBitmaps;
    std::vector<wxBitmap>   m_BitmapCache;
};


class CDrawLargeBitmapEvent : public wxEvent
{
public:
    CDrawLargeBitmapEvent(wxEventType evtType, CBOINCBitmapComboBox* myCtrl)
        : wxEvent(-1, evtType)
        {
            SetEventObject(myCtrl);
        }

    virtual wxEvent *       Clone() const { return new CDrawLargeBitmapEvent(*this); }
};

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_EVENT_TYPE( wxEVT_DRAW_LARGEBITMAP, 12001 )
END_DECLARE_EVENT_TYPES()

#define EVT_DRAW_LARGEBITMAP(fn)            DECLARE_EVENT_TABLE_ENTRY(wxEVT_DRAW_LARGEBITMAP, -1, -1, (wxObjectEventFunction) (wxEventFunction) &fn, NULL),

#endif //__MACBITMAPCOMBOBOX__
