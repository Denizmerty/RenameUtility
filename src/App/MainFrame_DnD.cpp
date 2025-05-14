#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/dnd.h>
#include <wx/dir.h>		   // For wxDirExists
#include <wx/filefn.h>	   // For wxFileExists
#include <wx/filename.h>   // For wxFileName
#include <wx/filepicker.h> // For dirPicker
#include <wx/listctrl.h>   // For list manipulation
#include <wx/textctrl.h>   // For log output
#include <wx/button.h>	   // For button state changes

#include "MainFrame.h" // Needs access to MainFrame members and methods

#include <filesystem>
#include <set>
#include <vector>
#include <string>

namespace fs = std::filesystem;

// Constructor for the file drop target, associating it with the MainFrame
FileDropTarget::FileDropTarget(MainFrame *owner) : m_owner(owner) {}

// Called when files/directories are dropped onto the target
bool FileDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames)
{
	if (!m_owner)
		return false; // Safety check: ensure the owner frame exists

	// Behavior depends on the current mode of the MainFrame
	if (m_owner->m_currentMode == RenamingMode::DirectoryScan)
	{
		// In Directory Scan mode, expect a single directory
		if (filenames.GetCount() == 1)
		{
			wxString droppedPath = filenames[0];
			if (wxDirExists(droppedPath))
			{
				m_owner->SetDroppedDirectory(droppedPath); // Delegate to MainFrame to handle the dropped directory
				return true;							   // Indicate success
			}
			else
			{
				m_owner->UpdateStatusBar("Drop Error: The dropped item is not a valid directory.");
				return false; // Indicate failure
			}
		}
		else
		{
			m_owner->UpdateStatusBar("Drop Error: Please drop only a single directory in Directory Scan mode.");
			return false; // Indicate failure
		}
	}
	else
	{ // ManualSelection Mode
		// In Manual Selection mode, expect one or more files; directories are ignored
		if (filenames.IsEmpty())
			return false; // Nothing dropped

		wxArrayString validFilesToAdd;
		for (const auto &name : filenames)
		{
			if (wxFileExists(name))
			{
				validFilesToAdd.Add(name);
			}
			else if (wxDirExists(name))
			{
				// Directories are not accepted in manual mode
				m_owner->UpdateStatusBar("Drop Ignored: Cannot add directories ('" + name + "') in Manual mode.");
			}
			else
			{
				m_owner->UpdateStatusBar("Drop Ignored: Invalid item ('" + name + "').");
			}
		}

		if (!validFilesToAdd.IsEmpty())
		{
			m_owner->AddDroppedFiles(validFilesToAdd); // Delegate to MainFrame to add valid files
			return true;							   // Indicate success
		}
		else
		{
			// Only update status if something was dropped but nothing was valid
			if (filenames.GetCount() > 0)
			{
				m_owner->UpdateStatusBar("Drop Failed: No valid files were dropped.");
			}
			return false; // Indicate failure (no valid files added)
		}
	}
	// Should not be reached under normal circumstances
	return false;
}

// Handles a directory dropped in DirectoryScan mode
void MainFrame::SetDroppedDirectory(const wxString &path)
{
	// This method is called by FileDropTarget
	if (m_currentMode != RenamingMode::DirectoryScan)
		return; // Should not happen based on FileDropTarget logic

	if (dirPicker && wxDirExists(path))
	{ // Double-check existence for robustness
		dirPicker->SetPath(path);
		UpdateStatusBar("Target directory set via drag and drop: " + path);
		logTextCtrl->AppendText("Target directory set: " + path + "\n");
		// Reset UI and state as the target directory has changed
		ResetInputBackgrounds();
		previewList->DeleteAllItems(); // Clear preview
		renameButton->Enable(false);
		m_previewSuccess = false;
		m_lastPreviewResults = {};
		m_lastValidParams = {};
		SetUndoAvailable(false);
	}
	else
	{
		UpdateStatusBar("Drop Error: Invalid directory path received.");
	}
}

// Adds dropped files (or files from "Add Files" dialog) in ManualSelection mode
void MainFrame::AddDroppedFiles(const wxArrayString &filenames)
{
	if (m_currentMode != RenamingMode::ManualSelection)
		return; // Safety check

	int addedCount = 0;
	int skippedCount = 0;
	int invalidCount = 0;

	// Use a set of existing fs::path for efficient duplicate checking of full paths
	std::set<fs::path> currentPaths(m_manualFiles.begin(), m_manualFiles.end());

	for (const auto &wxName : filenames)
	{
		try
		{
			fs::path filePath(wxName.ToStdWstring());
			std::error_code ec;

			// Check if it's a valid, existing regular file
			if (fs::exists(filePath, ec) && !ec && fs::is_regular_file(filePath, ec) && !ec)
			{
				// Check if it's already in the list (case-sensitive path comparison via std::set)
				if (currentPaths.find(filePath) == currentPaths.end())
				{
					m_manualFiles.push_back(filePath); // Add to internal vector
					currentPaths.insert(filePath);	   // Add to set for efficient duplicate checks within this loop
					addedCount++;
				}
				else
				{
					logTextCtrl->AppendText("Skipped duplicate file: " + filePath.filename().string() + "\n");
					skippedCount++;
				}
			}
			else
			{
				// Log why it was invalid (e.g. doesn't exist, is a directory, or filesystem error)
				if (ec)
				{
					logTextCtrl->AppendText("Skipped invalid item (error: " + ec.message() + "): " + wxName + "\n");
				}
				else if (!fs::exists(filePath))
				{
					logTextCtrl->AppendText("Skipped non-existent file: " + wxName + "\n");
				}
				else if (fs::is_directory(filePath))
				{ // Double check, though FileDropTarget should filter directories
					logTextCtrl->AppendText("Skipped directory (Manual Mode): " + wxName + "\n");
				}
				else
				{
					logTextCtrl->AppendText("Skipped invalid item: " + wxName + "\n");
				}
				invalidCount++;
			}
		}
		catch (const std::exception &e)
		{
			logTextCtrl->AppendText("Error processing dropped/added file '" + wxName + "': " + std::string(e.what()) + "\n");
			invalidCount++;
		}
	}

	// Update UI and state if any files were successfully added
	if (addedCount > 0)
	{
		wxString statusMsg = wxString::Format("Added %d file(s).", addedCount);
		if (skippedCount > 0)
			statusMsg += wxString::Format(" Skipped %d duplicate(s).", skippedCount);
		if (invalidCount > 0)
			statusMsg += wxString::Format(" Ignored %d invalid.", invalidCount);
		UpdateStatusBar(statusMsg);
		logTextCtrl->AppendText(wxString::Format("Added %d file(s).\n", addedCount));

		PopulateManualPreviewList(); // Clears old items/data and rebuilds list display

		// Reset preview/rename state as the list content has changed
		renameButton->Enable(false);
		m_previewSuccess = false;
		m_lastPreviewResults = {};
		SetUndoAvailable(false);
	}
	else
	{
		// Only show status if nothing was added but items were dropped/selected
		if (filenames.GetCount() > 0)
		{
			wxString statusMsg = "No new valid files added.";
			if (skippedCount > 0)
				statusMsg += wxString::Format(" Skipped %d duplicate(s).", skippedCount);
			if (invalidCount > 0)
				statusMsg += wxString::Format(" Ignored %d invalid.", invalidCount);
			UpdateStatusBar(statusMsg);
		}
	}
}