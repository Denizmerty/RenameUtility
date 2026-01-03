#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

// This file primarily defines custom wxWidgets events used throughout the
// MainFrame and its associated worker threads for asynchronous operation
// completion notifications
#include "MainFrame.h"

wxDEFINE_EVENT(EVT_PREVIEW_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(EVT_RENAME_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UNDO_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(EVT_PROGRESS_UPDATE, wxCommandEvent);