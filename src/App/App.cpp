#include <wx/wxprec.h>
#ifdef __WXMSW__
#include <windows.h>
#endif
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include "App.h"
#include "MainFrame.h"
#include <wx/config.h>

wxIMPLEMENT_APP(App);

bool App::OnInit()
{
#ifdef __WXMSW__
	// Enable per-monitor DPI awareness (V2) on Windows for sharp UI rendering on high-DPI displays
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

	SetAppName("FileRenamerUtility");

	// Initialize the configuration system for storing/retrieving application settings
	wxConfigBase::Set(new wxConfig(GetAppName()));

	if (!wxApp::OnInit())
	{
		wxConfigBase::Set(nullptr);
		return false;
	}

	MainFrame *frame = new MainFrame(
		"File Renamer Utility",
		wxPoint(50, 50),
		wxSize(1000, 900));
	frame->Show(true);
	return true;
}