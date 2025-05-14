#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/config.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/choicdlg.h>
#include <wx/log.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/radiobox.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>

#include "MainFrame.h"

#include <vector>
#include <string>
#include <algorithm> // for std::sort on wxArrayString
#include <filesystem>

namespace fs = std::filesystem;

// Retrieves a sorted list of saved profile names from the application's configuration
wxArrayString MainFrame::GetProfileNames()
{
	wxArrayString names;
	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
	{
		wxLogError("GetProfileNames: wxConfigBase::Get() returned nullptr.");
		return names; // Return empty list if config system is unavailable
	}

	cfg->SetPath("/Profiles"); // Navigate to the Profiles group in the config

	long index;
	wxString groupName;
	// Iterate through all subgroups under "/Profiles"
	bool continueSearch = cfg->GetFirstGroup(groupName, index);
	while (continueSearch)
	{
		names.Add(groupName);
		continueSearch = cfg->GetNextGroup(groupName, index);
	}

	cfg->SetPath("/"); // Reset config path to root
	names.Sort();	   // Sort profile names alphabetically for consistent display
	return names;
}

// Handles the "File -> Save Profile..." menu item
void MainFrame::OnSaveProfile(wxCommandEvent &event)
{
	wxString profileName = wxGetTextFromUser(
		"Enter a name for this profile:",
		"Save Profile",
		"", // Default value for input
		this);

	profileName = profileName.Trim(); // Remove leading/trailing whitespace

	if (profileName.IsEmpty())
	{
		UpdateStatusBar("Profile save cancelled.");
		return;
	}

	// Basic validation for profile name to prevent invalid characters or purely numeric names
	if (profileName.find('/') != wxString::npos || profileName.find('\\') != wxString::npos || profileName.IsNumber())
	{
		wxMessageBox("Invalid profile name. Please avoid using slashes ('/', '\\') and ensure it's not purely numeric.",
					 "Invalid Name", wxOK | wxICON_ERROR, this);
		return;
	}

	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
	{
		wxLogError("OnSaveProfile: wxConfigBase::Get() returned nullptr.");
		UpdateStatusBar("Error: Configuration system not available.");
		wxMessageBox("Cannot save profile. Configuration system error.", "Save Error", wxOK | wxICON_ERROR, this);
		return;
	}

	wxString profilePath = "/Profiles/" + profileName;

	// Check if profile already exists and confirm overwrite with the user
	if (cfg->HasGroup(profilePath))
	{
		if (wxMessageBox("A profile named '" + profileName + "' already exists.\nDo you want to overwrite it?",
						 "Confirm Overwrite", wxYES_NO | wxICON_QUESTION | wxCENTRE, this) != wxYES)
		{
			UpdateStatusBar("Profile save cancelled.");
			return;
		}
		// Delete the existing group before writing the new one to ensure a clean save
		if (!cfg->DeleteGroup(profilePath))
		{
			wxLogWarning("OnSaveProfile: Failed to delete existing profile group '%s'. Overwrite might fail.", profilePath);
			// Proceeding, as Write might still succeed in overwriting
		}
	}

	// Save current UI settings under the specified profile path in the config
	cfg->SetPath(profilePath);
	cfg->Write("Mode", (long)m_currentMode);
	cfg->Write("TargetDir", dirPicker->GetPath());
	cfg->Write("FilenamePattern", fileNamePatternCtrl->GetValue());
	cfg->Write("FilterExtensions", filterExtensionsCtrl->GetValue());
	cfg->Write("HighestNum", (long)highestNumSpin->GetValue());
	cfg->Write("LowestNum", (long)lowestNumSpin->GetValue());
	cfg->Write("RecursiveScan", recursiveCheck->IsChecked());
	cfg->Write("NamingPattern", patternCtrl->GetValue());
	cfg->Write("FindText", findCtrl->GetValue());
	cfg->Write("ReplaceText", replaceCtrl->GetValue());
	cfg->Write("FindCaseSensitive", caseSensitiveCheck->IsChecked());
	cfg->Write("CaseConversion", (long)caseChoice->GetSelection());
	cfg->Write("Increment", (long)incrementSpin->GetValue());
	cfg->Write("Backup", backupCheck->IsChecked());

	cfg->SetPath("/"); // Reset config path
	cfg->Flush();	   // Ensure changes are written to persistent storage

	UpdateStatusBar("Profile '" + profileName + "' saved successfully.");
	logTextCtrl->AppendText("Profile '" + profileName + "' saved.\n");
}

// Handles the "File -> Load Profile..." menu item
void MainFrame::OnLoadProfile(wxCommandEvent &event)
{
	wxArrayString profileNames = GetProfileNames();
	if (profileNames.IsEmpty())
	{
		wxMessageBox("No saved profiles found.", "Load Profile", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxSingleChoiceDialog dialog(
		this,
		"Select a profile to load:",
		"Load Profile",
		profileNames);

	if (dialog.ShowModal() != wxID_OK)
	{
		UpdateStatusBar("Profile load cancelled.");
		return;
	}

	wxString selectedProfile = dialog.GetStringSelection();
	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
	{
		wxLogError("OnLoadProfile: wxConfigBase::Get() returned nullptr.");
		UpdateStatusBar("Error: Configuration system not available.");
		wxMessageBox("Cannot load profile. Configuration system error.", "Load Error", wxOK | wxICON_ERROR, this);
		return;
	}

	wxString profilePath = "/Profiles/" + selectedProfile;

	if (!cfg->HasGroup(profilePath))
	{
		wxMessageBox("The selected profile '" + selectedProfile + "' could not be found.\nIt might have been deleted.",
					 "Load Error", wxOK | wxICON_ERROR, this);
		UpdateStatusBar("Error loading profile: Not found.");
		return;
	}

	// Load settings from the selected profile path
	cfg->SetPath(profilePath);

	RenamingMode loadedMode = (RenamingMode)cfg->ReadLong("Mode", (long)RenamingMode::DirectoryScan);
	bool modeChanged = (loadedMode != m_currentMode);

	if (modeChanged)
	{
		m_currentMode = loadedMode;
		modeSelectionRadio->SetSelection((int)m_currentMode);
	}

	// Apply loaded values to UI controls, providing defaults if a key is missing
	dirPicker->SetPath(cfg->Read("TargetDir", wxEmptyString));
	fileNamePatternCtrl->SetValue(cfg->Read("FilenamePattern", "*.*"));
	filterExtensionsCtrl->SetValue(cfg->Read("FilterExtensions", wxEmptyString));
	highestNumSpin->SetValue(cfg->ReadLong("HighestNum", 0));
	lowestNumSpin->SetValue(cfg->ReadLong("LowestNum", 0));
	recursiveCheck->SetValue(cfg->ReadBool("RecursiveScan", false));
	patternCtrl->SetValue(cfg->Read("NamingPattern", "<orig_name><ext>"));
	findCtrl->SetValue(cfg->Read("FindText", wxEmptyString));
	replaceCtrl->SetValue(cfg->Read("ReplaceText", wxEmptyString));
	caseSensitiveCheck->SetValue(cfg->ReadBool("FindCaseSensitive", true));
	caseChoice->SetSelection(cfg->ReadLong("CaseConversion", 0));
	incrementSpin->SetValue(cfg->ReadLong("Increment", 1));
	backupCheck->SetValue(cfg->ReadBool("Backup", false));

	cfg->SetPath("/"); // Reset config path

	// Update UI if the mode changed as part of loading the profile
	if (modeChanged)
	{
		UpdateUIForMode(); // This handles list clearing and other mode-specific UI adjustments
	}

	ResetInputBackgrounds(); // Clear any error highlighting on input fields
	UpdateStatusBar("Profile '" + selectedProfile + "' loaded.");
	logTextCtrl->AppendText("Profile '" + selectedProfile + "' loaded.\n");

	// Reset operational state after loading a profile
	SetUndoAvailable(false);
	renameButton->Enable(false);
	m_previewSuccess = false;
	m_lastPreviewResults = {};
	m_lastValidParams = {};
	// If mode didn't change, explicitly clear list contents, as UpdateUIForMode wouldn't have
	if (!modeChanged)
	{
		// In Manual mode, list items store fs::path data that needs deletion
		if (m_currentMode == RenamingMode::ManualSelection)
		{
			for (long i = 0; i < previewList->GetItemCount(); ++i)
			{
				wxUIntPtr data = previewList->GetItemData(i);
				if (data)
				{
					delete reinterpret_cast<fs::path *>(data);
				}
			}
		}
		previewList->DeleteAllItems();
		if (m_currentMode == RenamingMode::ManualSelection)
		{
			m_manualFiles.clear();
			PopulateManualPreviewList(); // Ensure buttons like "Remove" are correctly disabled if list is empty
		}
	}
}

// Handles the "File -> Delete Profile..." menu item
void MainFrame::OnDeleteProfile(wxCommandEvent &event)
{
	wxArrayString profileNames = GetProfileNames();
	if (profileNames.IsEmpty())
	{
		wxMessageBox("No saved profiles exist to delete.", "Delete Profile", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxSingleChoiceDialog dialog(
		this,
		"Select the profile you want to delete:",
		"Delete Profile",
		profileNames);

	if (dialog.ShowModal() != wxID_OK)
	{
		UpdateStatusBar("Profile deletion cancelled.");
		return;
	}

	wxString selectedProfile = dialog.GetStringSelection();

	if (wxMessageBox("Are you sure you want to permanently delete the profile '" + selectedProfile + "'?",
					 "Confirm Delete", wxYES_NO | wxICON_WARNING | wxCENTRE, this) != wxYES)
	{
		UpdateStatusBar("Profile deletion cancelled.");
		return;
	}

	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
	{
		wxLogError("OnDeleteProfile: wxConfigBase::Get() returned nullptr.");
		UpdateStatusBar("Error: Configuration system not available.");
		wxMessageBox("Cannot delete profile. Configuration system error.", "Delete Error", wxOK | wxICON_ERROR, this);
		return;
	}

	wxString profilePath = "/Profiles/" + selectedProfile;

	if (cfg->HasGroup(profilePath))
	{
		if (cfg->DeleteGroup(profilePath))
		{
			cfg->Flush(); // Persist the deletion
			UpdateStatusBar("Profile '" + selectedProfile + "' deleted.");
			logTextCtrl->AppendText("Profile '" + selectedProfile + "' deleted.\n");
			wxMessageBox("Profile '" + selectedProfile + "' has been deleted.", "Deletion Successful", wxOK | wxICON_INFORMATION, this);
		}
		else
		{
			wxLogError("OnDeleteProfile: Failed to delete profile group '%s'.", profilePath);
			UpdateStatusBar("Error deleting profile '" + selectedProfile + "'.");
			wxMessageBox("An error occurred while trying to delete the profile '" + selectedProfile + "'.",
						 "Delete Error", wxOK | wxICON_ERROR, this);
		}
	}
	else
	{
		// This case implies an inconsistency if GetProfileNames listed it
		wxMessageBox("The profile '" + selectedProfile + "' was not found. It might have already been deleted.",
					 "Profile Not Found", wxOK | wxICON_WARNING, this);
		UpdateStatusBar("Profile '" + selectedProfile + "' not found for deletion.");
	}

	cfg->SetPath("/"); // Reset config path
}