#ifndef HELPDIALOG_H
#define HELPDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dialog.h>

class wxTextCtrl;
class wxButton;

class HelpDialog : public wxDialog
{
public:
	HelpDialog(wxWindow *parent,
			   wxWindowID id,
			   const wxString &title,
			   const wxString &helpContent,
			   const wxPoint &pos = wxDefaultPosition,
			   const wxSize &size = wxSize(650, 400),
			   long style = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

private:
	wxTextCtrl *helpTextCtrl;
	wxButton *okButton;

	void OnOk(wxCommandEvent &event);
};

#endif