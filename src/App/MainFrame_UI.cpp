#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>
#include <wx/listctrl.h>
#include <wx/statusbr.h>
#include <wx/settings.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/statbox.h>
#include <wx/menu.h> // For disabling menu items in SetUIBusy

#include "MainFrame.h"

#include <filesystem>
#include <string>
#include <vector>
#include <set>

namespace fs = std::filesystem;

// Updates the text displayed in the status bar
void MainFrame::UpdateStatusBar(const wxString &text)
{
	if (m_statusBar)
	{
		m_statusBar->SetStatusText(text);
	}
}

// Resets the background color of input controls to their default, clearing any error highlighting
void MainFrame::ResetInputBackgrounds()
{
	patternCtrl->SetBackgroundColour(m_defaultTextCtrlBgColour);
	patternCtrl->Refresh();
	findCtrl->SetBackgroundColour(m_defaultTextCtrlBgColour);
	findCtrl->Refresh();
	replaceCtrl->SetBackgroundColour(m_defaultTextCtrlBgColour);
	replaceCtrl->Refresh();
	wxTextCtrl *dirText = dirPicker->GetTextCtrl();
	if (dirText)
	{
		dirText->SetBackgroundColour(m_defaultTextCtrlBgColour);
		dirText->Refresh();
	}
	fileNamePatternCtrl->SetBackgroundColour(m_defaultTextCtrlBgColour);
	fileNamePatternCtrl->Refresh();
	filterExtensionsCtrl->SetBackgroundColour(m_defaultTextCtrlBgColour);
	filterExtensionsCtrl->Refresh();
	wxColour defaultSpinBg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW); // Standard background for spin controls
	lowestNumSpin->SetBackgroundColour(defaultSpinBg);
	lowestNumSpin->Refresh();
	highestNumSpin->SetBackgroundColour(defaultSpinBg);
	highestNumSpin->Refresh();
}

// Updates the UI elements based on the current renaming mode (Directory Scan or Manual Selection)
void MainFrame::UpdateUIForMode()
{
	bool isDirScan = (m_currentMode == RenamingMode::DirectoryScan);

	// Show/Hide UI sections relevant to the current mode
	dirScanSizer->Show(isDirScan);
	manualSizer->Show(!isDirScan);

	// Explicitly show/hide individual controls within those sections for clarity and robustness
	dirPicker->Show(isDirScan);
	fileNamePatternCtrl->Show(isDirScan);
	fileNamePatternLabel->Show(isDirScan);
	filterExtensionsCtrl->Show(isDirScan);
	filterExtensionsLabel->Show(isDirScan);
	lowestNumSpin->Show(isDirScan);
	lowestNumLabel->Show(isDirScan);
	highestNumSpin->Show(isDirScan);
	highestNumLabel->Show(isDirScan);
	recursiveCheck->Show(isDirScan);

	addFilesButton->Show(!isDirScan);
	removeFilesButton->Show(!isDirScan);
	clearFilesButton->Show(!isDirScan);

	// Adjust default naming pattern based on the new mode if current pattern seems mode-specific
	if (isDirScan && (patternCtrl->GetValue().find("<index>") != wxString::npos || patternCtrl->GetValue().empty()))
	{
		patternCtrl->SetValue("<orig_name><ext>"); // Sensible default for Directory Scan
	}
	else if (!isDirScan && (patternCtrl->GetValue().find("<num>") != wxString::npos || patternCtrl->GetValue().empty()))
	{
		patternCtrl->SetValue("<orig_name>_<index><orig_ext>"); // Sensible default for Manual Selection
	}

	// Clear list items and their associated fs::path data (if any from Manual mode)
	for (long i = 0; i < previewList->GetItemCount(); ++i)
	{
		wxUIntPtr data = previewList->GetItemData(i);
		if (data)
		{
			delete reinterpret_cast<fs::path *>(data);
		}
	}
	previewList->DeleteAllItems();
	logTextCtrl->Clear();
	renameButton->Enable(false);
	m_previewSuccess = false;
	m_lastPreviewResults = {};
	m_lastValidParams = {};
	SetUndoAvailable(false); // Mode change invalidates undo state

	if (!isDirScan)
	{ // Manual Mode specific cleanup and setup
		// Reset Directory Scan inputs to defaults or empty values
		dirPicker->SetPath("");
		fileNamePatternCtrl->SetValue("*.*");
		filterExtensionsCtrl->SetValue("");
		lowestNumSpin->SetValue(0);
		highestNumSpin->SetValue(0);
		recursiveCheck->SetValue(false);
		PopulateManualPreviewList(); // Rebuild list from m_manualFiles (which may be empty)
	}
	else
	{						   // Directory Scan Mode specific cleanup
		m_manualFiles.clear(); // Ensure internal manual file list is cleared
		// Update button states for Manual mode controls (they should now be disabled)
		removeFilesButton->Enable(false);
		clearFilesButton->Enable(false);
	}

	UpdatePreviewListColumns(); // Adjust preview list columns for the new mode

	// Force layout recalculation to reflect shown/hidden elements
	scrolledWindow->GetSizer()->Layout();
	scrolledWindow->FitInside(); // Adjust scrollbars if necessary
	mainPanel->Layout();		 // Layout the main panel containing the scrolled window and bottom panel
}

// Updates the columns of the preview list control based on the current renaming mode
void MainFrame::UpdatePreviewListColumns()
{
	previewList->DeleteAllColumns();
	if (m_currentMode == RenamingMode::DirectoryScan)
	{
		previewList->InsertColumn(0, "Old Name", wxLIST_FORMAT_LEFT, 250);
		previewList->InsertColumn(1, "New Name", wxLIST_FORMAT_LEFT, 250);
	}
	else
	{ // ManualSelection Mode
		previewList->InsertColumn(0, "Index", wxLIST_FORMAT_RIGHT, 60);
		previewList->InsertColumn(1, "Original Name", wxLIST_FORMAT_LEFT, 220);
		previewList->InsertColumn(2, "New Name", wxLIST_FORMAT_LEFT, 220);
	}
}

// Populates the preview list with files from m_manualFiles (Manual Selection mode only)
// This version shows original names and prepares for preview calculation
void MainFrame::PopulateManualPreviewList()
{
	if (m_currentMode != RenamingMode::ManualSelection)
		return;

	// Clear existing visual items AND their associated fs::path data first to prevent leaks
	for (long i = 0; i < previewList->GetItemCount(); ++i)
	{
		wxUIntPtr data = previewList->GetItemData(i);
		if (data)
		{
			delete reinterpret_cast<fs::path *>(data);
		}
	}
	previewList->DeleteAllItems();

	// Rebuild the list from the internal m_manualFiles vector
	long itemIndex = 0;
	for (const auto &path : m_manualFiles)
	{
		previewList->InsertItem(itemIndex, std::to_string(itemIndex + 1)); // Display 1-based index
		previewList->SetItem(itemIndex, 1, path.filename().string());
		previewList->SetItem(itemIndex, 2, ""); // New name column is initially empty
		// Store a new fs::path pointer for this item, to be used/replaced by preview results
		previewList->SetItemData(itemIndex, reinterpret_cast<wxUIntPtr>(new fs::path(path)));
		itemIndex++;
	}

	// Update enable/disable state of "Remove Selected" and "Clear List" buttons
	bool hasItems = !m_manualFiles.empty();
	removeFilesButton->Enable(hasItems);
	clearFilesButton->Enable(hasItems);
}

// Enables or disables UI elements to indicate a busy state (e.g., during thread operations)
void MainFrame::SetUIBusy(bool busy)
{
	bool enable = !busy; // Controls should be enabled if not busy
	bool isDirScan = (m_currentMode == RenamingMode::DirectoryScan);

	// Enable/disable controls based on 'busy' state and current mode
	modeSelectionRadio->Enable(enable);

	// Directory Scan Controls
	dirPicker->Enable(enable && isDirScan);
	fileNamePatternCtrl->Enable(enable && isDirScan);
	filterExtensionsCtrl->Enable(enable && isDirScan);
	highestNumSpin->Enable(enable && isDirScan);
	lowestNumSpin->Enable(enable && isDirScan);
	recursiveCheck->Enable(enable && isDirScan);

	// Manual Selection Controls
	addFilesButton->Enable(enable && !isDirScan);
	bool hasManualItems = !m_manualFiles.empty();
	removeFilesButton->Enable(enable && !isDirScan && hasManualItems);
	clearFilesButton->Enable(enable && !isDirScan && hasManualItems);

	// Common Controls
	patternCtrl->Enable(enable);
	findCtrl->Enable(enable);
	replaceCtrl->Enable(enable);
	caseSensitiveCheck->Enable(enable);
	caseChoice->Enable(enable);
	incrementSpin->Enable(enable);
	backupCheck->Enable(enable);

	// Action Buttons
	previewButton->Enable(enable);
	// Rename button depends on preview success, not being busy, AND having items in the rename plan
	renameButton->Enable(enable && m_previewSuccess && !m_lastPreviewResults.renamePlan.empty());

	// Menu Items
	wxMenuBar *menuBar = GetMenuBar();
	if (menuBar)
	{
		menuBar->Enable(wxID_EXIT, enable);
		menuBar->Enable(ID_HelpTopics, enable);
		menuBar->Enable(wxID_ABOUT, enable);
		menuBar->Enable(ID_SaveProfile, enable);
		menuBar->Enable(ID_LoadProfile, enable);
		menuBar->Enable(ID_DeleteProfile, enable);
		// Undo menu item state depends on undo availability AND not being busy
		menuBar->Enable(ID_UndoRename, enable && m_undoAvailable);
	}

	// Update status bar and cursor to reflect busy state
	if (busy)
	{
		UpdateStatusBar("Processing...");
		wxBeginBusyCursor();
	}
	else
	{
		// Status bar will be updated by the calling function with a more specific message
		wxEndBusyCursor();
	}
}