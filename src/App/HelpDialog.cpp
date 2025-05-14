#include "HelpDialog.h"
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/button.h>

// Constructor: Initializes and lays out the components of the help dialog
HelpDialog::HelpDialog(wxWindow *parent,
					   wxWindowID id,
					   const wxString &title,
					   const wxString &helpContent,
					   const wxPoint &pos,
					   const wxSize &size,
					   long style)
	: wxDialog(parent, id, title, pos, size, style)
{
	wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

	helpTextCtrl = new wxTextCtrl(
		this,
		wxID_ANY,
		helpContent,
		wxDefaultPosition,
		wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 // Use a rich text control for better formatting, read-only
	);
	mainSizer->Add(helpTextCtrl, 1, wxEXPAND | wxALL, 10); // Text control should expand

	wxStdDialogButtonSizer *buttonSizer = new wxStdDialogButtonSizer();
	okButton = new wxButton(this, wxID_OK);
	buttonSizer->AddButton(okButton);
	buttonSizer->Realize(); // Arranges buttons according to platform standards
	mainSizer->Add(buttonSizer, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(mainSizer);
	mainSizer->SetSizeHints(this); // Apply sizer constraints to the dialog

	SetInitialSize(size); // Respect the requested initial size

	Centre(wxBOTH); // Center the dialog on screen

	// Bind the OK button event to the OnOk handler
	Bind(wxEVT_BUTTON, &HelpDialog::OnOk, this, wxID_OK);
}

// Handles the OK button click event
void HelpDialog::OnOk(wxCommandEvent &WXUNUSED(event))
{
	// Closes the modal dialog and returns wxID_OK
	EndModal(wxID_OK);
}