#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <wx/filename.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/filepicker.h>
#include <wx/stattext.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/radiobox.h>

#include "MainFrame.h"
#include "HelpDialog.h"
#include "WorkerThread.h"

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

// Handles changes in the renaming mode selection (Directory Scan vs. Manual)
void MainFrame::OnModeChange(wxCommandEvent &event)
{
	int selection = modeSelectionRadio->GetSelection();
	RenamingMode newMode = (RenamingMode)selection;
	if (newMode != m_currentMode)
	{
		m_currentMode = newMode;
		UpdateStatusBar("Mode changed to " + modeSelectionRadio->GetStringSelection());
		logTextCtrl->AppendText("\n--- Switched to " + modeSelectionRadio->GetStringSelection() + " Mode ---\n");
		ResetInputBackgrounds();
		UpdateUIForMode(); // Updates UI elements, clears lists and data as appropriate for the new mode
	}
}

// Handles the "Add Files..." button click in Manual Selection mode
void MainFrame::OnAddFilesClick(wxCommandEvent &event)
{
	if (m_currentMode != RenamingMode::ManualSelection)
		return;

	wxFileDialog openFileDialog(this, "Select files to add", "", "", "All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

	if (openFileDialog.ShowModal() == wxID_CANCEL)
	{
		UpdateStatusBar("File selection cancelled.");
		return;
	}

	wxArrayString paths;
	openFileDialog.GetPaths(paths);

	if (paths.IsEmpty())
	{
		UpdateStatusBar("No files selected.");
		return;
	}

	AddDroppedFiles(paths);	 // Reuse the drag-and-drop logic for adding files
	SetUndoAvailable(false); // Modifying the file list invalidates any existing undo state
}

// Handles the "Remove Selected" button click in Manual Selection mode
void MainFrame::OnRemoveFilesClick(wxCommandEvent &event)
{
	if (m_currentMode != RenamingMode::ManualSelection)
		return;

	long itemIndex = previewList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (itemIndex == wxNOT_FOUND)
	{
		UpdateStatusBar("No file selected to remove.");
		return;
	}

	// Retrieve and validate the stored path data associated with the list item
	fs::path *storedPath = reinterpret_cast<fs::path *>(previewList->GetItemData(itemIndex));
	if (storedPath)
	{
		// Find and remove the path from the internal m_manualFiles vector
		auto it = std::find(m_manualFiles.begin(), m_manualFiles.end(), *storedPath);
		if (it != m_manualFiles.end())
		{
			m_manualFiles.erase(it);
			logTextCtrl->AppendText("Removed: " + storedPath->filename().string() + "\n");
			UpdateStatusBar("Removed selected file.");
		}
		else
		{
			// This case indicates an inconsistency between the list display and internal data
			logTextCtrl->AppendText("Warning: Path [" + storedPath->string() + "] not found in internal list.\n");
		}
		// Delete the heap-allocated fs::path object to prevent memory leaks
		delete storedPath;
		// Clear the item data pointer; good practice even though the list will be repopulated
		previewList->SetItemData(itemIndex, 0);
	}
	else
	{
		logTextCtrl->AppendText("Warning: No path data associated with selected item index " + std::to_string(itemIndex) + ".\n");
	}

	// Repopulate the list to reflect the removal and update indices
	PopulateManualPreviewList();

	// Reset state after modification
	renameButton->Enable(false);
	m_previewSuccess = false;
	m_lastPreviewResults = {};
	SetUndoAvailable(false);
}

// Handles the "Clear List" button click in Manual Selection mode
void MainFrame::OnClearFilesClick(wxCommandEvent &event)
{
	if (m_currentMode != RenamingMode::ManualSelection)
		return;

	if (wxMessageBox("Are you sure you want to clear the manual file list?", "Confirm Clear", wxYES_NO | wxICON_QUESTION | wxCENTRE, this) == wxYES)
	{
		// Delete heap-allocated fs::path data associated with ALL list items before clearing
		for (long i = 0; i < previewList->GetItemCount(); ++i)
		{
			wxUIntPtr data = previewList->GetItemData(i);
			if (data)
			{
				delete reinterpret_cast<fs::path *>(data);
			}
		}
		previewList->DeleteAllItems(); // Clear visual list

		m_manualFiles.clear(); // Clear internal list

		logTextCtrl->AppendText("Manual file list cleared.\n");
		UpdateStatusBar("Manual list cleared.");

		// Update button states to reflect the empty list
		bool hasItems = !m_manualFiles.empty();
		removeFilesButton->Enable(hasItems);
		clearFilesButton->Enable(hasItems);

		// Reset operational state
		renameButton->Enable(false);
		m_previewSuccess = false;
		m_lastPreviewResults = {};
		SetUndoAvailable(false);
	}
}

// Handles the "Preview Rename" button click
void MainFrame::OnPreviewClick(wxCommandEvent &event)
{
	ResetInputBackgrounds();
	UpdateStatusBar("Initiating preview...");
	SetUndoAvailable(false); // Generating a new preview invalidates any previous undo state
	logTextCtrl->Clear();
	renameButton->Enable(false); // Disable rename until preview succeeds
	m_previewSuccess = false;
	m_lastPreviewResults = {}; // Clear previous results
	m_lastValidParams = {};	   // Clear previous params
	m_lastBackupPath.clear();
	m_lastRenameResult = {};
	m_lastBackupResult = {};
	m_backupAttempted = false;

	// Clear existing preview list items and their associated fs::path data (if in Manual mode)
	for (long i = 0; i < previewList->GetItemCount(); ++i)
	{
		wxUIntPtr data = previewList->GetItemData(i);
		if (data)
		{
			delete reinterpret_cast<fs::path *>(data);
		}
	}
	previewList->DeleteAllItems();

	// If in Manual mode, repopulate the list with original filenames before calculation
	if (m_currentMode == RenamingMode::ManualSelection)
	{
		PopulateManualPreviewList(); // Rebuilds with original names and stores new path data for items
	}
	// In Dir Scan mode, the list remains empty at this stage

	wxTextAttr redStyle(*wxRED);
	wxTextAttr normalStyle;
	logTextCtrl->SetDefaultStyle(normalStyle);
	logTextCtrl->AppendText("Starting preview generation (" + modeSelectionRadio->GetStringSelection() + ")...\n");

	InputParams params; // Parameters to be passed to the renamer logic
	params.mode = m_currentMode;

	// Populate common parameters
	params.namingPattern = patternCtrl->GetValue().ToStdString();
	params.findText = findCtrl->GetValue().ToStdString();
	params.replaceText = replaceCtrl->GetValue().ToStdString();
	params.findCaseSensitive = caseSensitiveCheck->IsChecked();
	int caseSelection = caseChoice->GetSelection();
	if (caseSelection == 1)
		params.caseConversionMode = CaseConversionMode::ToUpper;
	else if (caseSelection == 2)
		params.caseConversionMode = CaseConversionMode::ToLower;
	else
		params.caseConversionMode = CaseConversionMode::NoChange;
	params.increment = incrementSpin->GetValue();

	wxColour errorColour(255, 200, 200); // Light red for highlighting input errors

	// Populate mode-specific parameters and perform validation
	if (m_currentMode == RenamingMode::DirectoryScan)
	{
		wxString targetDirWx = dirPicker->GetPath();
		if (targetDirWx.IsEmpty() || !wxFileName::DirExists(targetDirWx))
		{
			wxTextCtrl *dirText = dirPicker->GetTextCtrl();
			if (dirText)
			{
				dirText->SetBackgroundColour(errorColour);
				dirText->Refresh();
			}
			wxMessageBox("Target Directory is invalid or does not exist.", "Input Error", wxOK | wxICON_ERROR, this);
			logTextCtrl->SetDefaultStyle(redStyle);
			logTextCtrl->AppendText("Error: Invalid target directory.\n");
			logTextCtrl->SetDefaultStyle(normalStyle);
			UpdateStatusBar("Error: Invalid target directory.");
			dirPicker->SetFocus();
			return;
		}
		try
		{
			params.targetDirectory = fs::path(targetDirWx.ToStdWstring());
		}
		catch (const std::exception &e)
		{
			wxMessageBox("Error converting directory path: " + std::string(e.what()), "Path Error", wxOK | wxICON_ERROR, this);
			return;
		}

		params.filenamePattern = fileNamePatternCtrl->GetValue().Trim().ToStdString();
		if (params.filenamePattern.empty())
		{
			fileNamePatternCtrl->SetBackgroundColour(errorColour);
			fileNamePatternCtrl->Refresh();
			wxMessageBox("Filename Pattern cannot be empty.", "Input Error", wxOK | wxICON_ERROR, this);
			logTextCtrl->SetDefaultStyle(redStyle);
			logTextCtrl->AppendText("Error: Filename Pattern is empty.\n");
			logTextCtrl->SetDefaultStyle(normalStyle);
			UpdateStatusBar("Error: Filename Pattern empty.");
			fileNamePatternCtrl->SetFocus();
			return;
		}

		params.filterExtensions = filterExtensionsCtrl->GetValue().Trim().ToStdString();
		params.lowestNumber = lowestNumSpin->GetValue();
		params.highestNumber = highestNumSpin->GetValue();
		// Number filter range must be valid (lowest <= highest, unless both are 0 for no filter)
		if (params.lowestNumber > params.highestNumber && (params.lowestNumber != 0 || params.highestNumber != 0))
		{
			lowestNumSpin->SetBackgroundColour(errorColour);
			lowestNumSpin->Refresh();
			highestNumSpin->SetBackgroundColour(errorColour);
			highestNumSpin->Refresh();
			wxMessageBox("Lowest Number cannot be greater than Highest Number in the filter.", "Input Error", wxOK | wxICON_ERROR, this);
			logTextCtrl->SetDefaultStyle(redStyle);
			logTextCtrl->AppendText("Error: Invalid number filter range.\n");
			logTextCtrl->SetDefaultStyle(normalStyle);
			UpdateStatusBar("Error: Invalid number range.");
			lowestNumSpin->SetFocus();
			return;
		}
		params.recursiveScan = recursiveCheck->IsChecked();

		if (params.recursiveScan)
			logTextCtrl->AppendText("Recursive scan enabled.\n");
		if (params.lowestNumber != 0 || params.highestNumber != 0)
			logTextCtrl->AppendText(wxString::Format("Using number filter: %d to %d.\n", params.lowestNumber, params.highestNumber));
		if (!params.filterExtensions.empty())
			logTextCtrl->AppendText("Using extension filter: " + params.filterExtensions + "\n");
	}
	else
	{ // ManualSelection Mode
		params.manualFiles = m_manualFiles;
		if (params.manualFiles.empty())
		{
			wxMessageBox("No files have been added to the list.", "Input Error", wxOK | wxICON_ERROR, this);
			logTextCtrl->SetDefaultStyle(redStyle);
			logTextCtrl->AppendText("Error: Manual file list is empty.\n");
			logTextCtrl->SetDefaultStyle(normalStyle);
			UpdateStatusBar("Error: No files in manual list.");
			addFilesButton->SetFocus();
			return;
		}
		// Use the parent directory of the first file as the "target" for context (e.g., backup source)
		if (!params.manualFiles.empty())
		{
			params.targetDirectory = params.manualFiles[0].parent_path();
		}
		else
		{
			params.targetDirectory = fs::current_path(); // Fallback if list somehow empty after check
		}
		// Set DirScan specific params to defaults/empty for clarity as they are not used in Manual mode
		params.recursiveScan = false;
		params.filenamePattern = "";
		params.filterExtensions = "";
		params.lowestNumber = 0;
		params.highestNumber = 0;
	}

	// Common validation for the naming pattern
	if (params.namingPattern.empty())
	{
		patternCtrl->SetBackgroundColour(errorColour);
		patternCtrl->Refresh();
		wxMessageBox("New Naming Pattern cannot be empty.", "Input Error", wxOK | wxICON_ERROR, this);
		logTextCtrl->SetDefaultStyle(redStyle);
		logTextCtrl->AppendText("Error: New Naming Pattern is empty.\n");
		logTextCtrl->SetDefaultStyle(normalStyle);
		UpdateStatusBar("Error: New pattern empty.");
		patternCtrl->SetFocus();
		return;
	}

	logTextCtrl->AppendText("Input validation successful.\n");
	m_lastValidParams = params; // Store the validated parameters for potential rename operation

	logTextCtrl->AppendText("Launching preview calculation thread...\n");
	UpdateStatusBar("Calculating preview...");
	SetUIBusy(true); // Disable UI elements during processing

	WorkerThread *thread = new WorkerThread(this, params);

	if (!thread)
	{
		wxLogError("Failed to create worker thread for preview!");
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create worker thread.");
		return;
	}
	if (thread->Create() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to create preview worker thread resource.");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create thread resource.");
		return;
	}
	if (thread->Run() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to run preview worker thread!");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to run worker thread.");
		return;
	}
	// Thread is running; results will be handled by OnPreviewThreadComplete via EVT_PREVIEW_COMPLETE
}

// Handles the "Perform Rename" button click
void MainFrame::OnRenameClick(wxCommandEvent &event)
{
	ResetInputBackgrounds();

	if (!m_previewSuccess || m_lastPreviewResults.renamePlan.empty())
	{
		wxMessageBox("A successful preview must be generated before renaming.\n"
					 "Please click 'Preview Rename' first.",
					 "Cannot Rename", wxOK | wxICON_WARNING, this);
		logTextCtrl->AppendText("Rename aborted: No valid preview generated or preview resulted in no files to rename.\n");
		UpdateStatusBar("Rename aborted: Preview required.");
		return;
	}

	SetUndoAvailable(false); // Disable undo before starting a new rename operation
	int numFiles = m_lastPreviewResults.renamePlan.size();
	wxString confirmMsg = wxString::Format("Are you sure you want to rename %d file(s)?", numFiles);
	if (backupCheck->IsChecked())
	{
		confirmMsg += "\n\nA backup of the target directory will be created before renaming.";
	}
	else
	{
		confirmMsg += "\n\nWARNING: Backup is NOT enabled. This operation cannot be easily undone without a backup.";
	}

	if (wxMessageBox(confirmMsg, "Confirm Rename Operation", wxYES_NO | wxICON_QUESTION | wxCENTRE, this) != wxYES)
	{
		logTextCtrl->AppendText("Rename operation cancelled by user.\n");
		UpdateStatusBar("Rename cancelled.");
		return;
	}

	bool doBackup = backupCheck->IsChecked();
	wxTextAttr redStyle(*wxRED);
	wxTextAttr normalStyle;
	logTextCtrl->SetDefaultStyle(normalStyle);

	// Determine backup source directory and context name using stored valid parameters from the preview stage
	fs::path backupSourceDir = m_lastValidParams.targetDirectory;
	std::string backupContextName;
	if (m_currentMode == RenamingMode::DirectoryScan)
	{
		backupContextName = m_lastValidParams.filenamePattern.empty() ? "DirScan" : m_lastValidParams.filenamePattern;
	}
	else
	{
		backupContextName = "ManualList";
	}
	// Sanitize context name slightly for folder naming. Further sanitization occurs in performBackup
	std::replace(backupContextName.begin(), backupContextName.end(), '*', '_');
	std::replace(backupContextName.begin(), backupContextName.end(), '?', '_');

	if (doBackup)
	{
		// Pre-flight check for backup source directory validity before launching thread
		std::error_code ec;
		if (!fs::exists(backupSourceDir, ec) || ec || !fs::is_directory(backupSourceDir, ec) || ec)
		{
			wxMessageBox("Backup Error: The source directory for backup is invalid or inaccessible:\n" + backupSourceDir.string() +
							 (ec ? "\nError: " + ec.message() : ""),
						 "Backup Error", wxOK | wxICON_ERROR, this);
			logTextCtrl->SetDefaultStyle(redStyle);
			logTextCtrl->AppendText("Error: Cannot perform backup. Invalid source directory: '" + backupSourceDir.string() + "'. Rename aborted.\n");
			logTextCtrl->SetDefaultStyle(normalStyle);
			UpdateStatusBar("Error: Invalid backup source directory.");
			return;
		}
		logTextCtrl->AppendText("\nLaunching backup and rename thread...\n");
		logTextCtrl->AppendText("Backup source directory: " + backupSourceDir.string() + "\n");
		UpdateStatusBar("Performing backup and renaming...");
	}
	else
	{
		logTextCtrl->AppendText("\nLaunching rename thread (backup disabled)...\n");
		UpdateStatusBar("Performing rename...");
	}

	SetUIBusy(true);

	// Create worker thread for the rename operation
	WorkerThread *thread = new WorkerThread(
		this,
		m_lastPreviewResults.renamePlan, // Pass the calculated rename plan from preview
		m_lastValidParams.increment,	 // Pass the increment value used in preview
		backupSourceDir,				 // Pass the determined source directory for backup
		backupContextName,				 // Pass the context for backup naming
		doBackup						 // Pass the backup flag
	);

	if (!thread)
	{
		wxLogError("Failed to create worker thread for rename!");
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create worker thread.");
		return;
	}
	if (thread->Create() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to create rename worker thread resource.");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create thread resource.");
		return;
	}
	if (thread->Run() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to run rename worker thread!");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to run worker thread.");
		return;
	}
	// Thread is running; results handled by OnRenameThreadComplete via EVT_RENAME_COMPLETE
}

// Handles the "File -> Exit" menu item
void MainFrame::OnExit(wxCommandEvent &event)
{
	Close(true); // Trigger OnClose handler for graceful shutdown
}

// Handles the "Help -> About" menu item
void MainFrame::OnAbout(wxCommandEvent &event)
{
	wxAboutDialogInfo i;
	i.SetName("File Renamer Utility");
	i.SetVersion("1.0.0");
	i.SetDescription("Renames files via directory scan or manual selection.\nSupports filters, patterns, find/replace, case change, backups, undo.");
	i.SetCopyright("(C) Deniz Mert Yayla 2025");
	i.SetLicence("GNU General Public License v3.0");
	i.SetWebSite("mailto:denizmerty@gmail.com", "Questions/Suggestions: denizmerty@gmail.com");

	wxAboutBox(i, this);
}

/// Handles the "Help -> Help..." menu item
void MainFrame::OnHelpTopics(wxCommandEvent &event)
{
	// This extensive string contains the help content for the application
	const wxString helpContent =
		"----------------------------------\n"
		" File Renamer Utility - Help\n"
		"----------------------------------\n\n"
		"This utility allows renaming files in two main modes: Directory Scan and Manual File Selection.\n\n"

		"======================\n"
		" Operation Mode\n"
		"======================\n"
		"  - Directory Scan: Select a directory, define filters, and apply a renaming pattern to matching files within that directory (and optionally its subdirectories).\n"
		"  - Manual File Selection: Add specific files from anywhere on your system to a list and apply a renaming pattern to them in the order they appear in the list.\n\n"
		"You can switch between modes using the radio buttons at the top.\n\n"

		"======================\n"
		" Directory Scan Options\n"
		"======================\n"
		"(Only active in Directory Scan mode)\n\n"
		"  - Target Directory: Choose the main folder containing the files you want to rename. You can type the path, use the 'Select...' button, or drag-and-drop a folder onto the application window.\n"
		"  - Filename Pattern (find, uses *, ?): Specify a pattern to find files. Uses standard wildcards:\n"
		"    - * matches any sequence of zero or more characters.\n"
		"    - ? matches any single character.\n"
		"    Examples:\n"
		"      - *.jpg - Finds all files with the .jpg extension.\n"
		"      - image_???.png - Finds files like image_001.png, image_123.png.\n"
		"      - *(*)* - Finds files containing parentheses.\n"
		"      - * or *.* - Finds all files (filtering might still apply).\n"
		"  - Filter by Extensions (opt., comma-sep): Optionally, provide a comma-separated list of extensions (e.g., .jpg, .png, .gif) to further filter the files found by the pattern. Leave empty to disable extension filtering. Case-insensitive.\n"
		"  - Lowest/Highest Number (optional filter): If your filenames contain numbers (e.g., photo_005.jpg), you can use these fields to filter files based on the last number found in the filename. Set both to 0 to disable number filtering. This filter applies after the filename pattern and extension filters.\n"
		"  - Include Subdirectories: Check this box to scan for files within the Target Directory and all its subfolders that match the criteria.\n\n"

		"==============================\n"
		" Manual File Selection Options\n"
		"==============================\n"
		"(Only active in Manual File Selection mode)\n\n"
		"  - Add Files...: Opens a dialog to select one or more files to add to the renaming list.\n"
		"  - Remove Selected: Removes the currently highlighted file from the list.\n"
		"  - Clear List: Removes all files from the list.\n"
		"  - Drag and Drop: You can drag and drop files (not folders) directly onto the application window to add them to the list.\n\n"
		"The preview list in this mode shows an 'Index', the 'Original Name', and the 'New Name' after preview.\n\n"

		"==========================\n"
		" Common Renaming Options\n"
		"==========================\n"
		"(Active in both modes)\n\n"
		"  - New Naming Pattern: This is the core of the renaming process. Define how the new filenames should be constructed using placeholders. Available placeholders depend on the mode:\n\n"
		"    Common Placeholders (Both Modes):\n"
		"    - <orig_name>: The original filename without the extension.\n"
		"    - <ext> or <orig_ext>: The original file extension (including the dot, e.g., .jpg).\n"
		"    - <YYYY>: Current year (4 digits).\n"
		"    - <MM>: Current month (01-12).\n"
		"    - <DD>: Current day (01-31).\n"
		"    - <hh>: Current hour (00-23).\n"
		"    - <mm>: Current minute (00-59).\n"
		"    - <ss>: Current second (00-59).\n\n"
		"    Directory Scan Mode Only:\n"
		"    - <num>: The original number found in the filename (if any, based on the number filter logic), potentially modified by the 'Increment By' value, formatted with leading zeros (default width 2, adjusted by filter range).\n"
		"    - <orig_num>: The original number found in the filename (if any), formatted with leading zeros.\n\n"
		"    Manual Selection Mode Only:\n"
		"    - <index>: The 1-based index of the file in the manual list, formatted with leading zeros to match the total number of files (e.g., 01, 02,... 10 if there are 10 files).\n\n"
		"    Placeholders NOT available in the respective modes will be replaced with empty strings.\n\n"
		"    Examples:\n"
		"    - Document_<YYYY>-<MM>-<DD><ext> -> Document_2024-01-15.txt\n"
		"    - (Dir Scan) Image_<num><ext> -> Image_001.jpg (if original was photo_0.jpg and increment is 1)\n"
		"    - (Manual) <index>_<orig_name><orig_ext> -> 01_MyPicture.png\n\n"
		"  - Find Text (Optional): Text to search for within the filename generated by the Naming Pattern.\n"
		"  - Replace With: Text to replace the 'Find Text' with. If 'Find Text' is empty, this is ignored.\n"
		"  - Case Sensitive Find: If checked, the 'Find Text' search will match case exactly. If unchecked, it will be case-insensitive.\n"
		"  - Change Case: Apply case conversion to the filename stem (the part before the extension) after pattern replacement and find/replace:\n"
		"    - No Change: Leaves case as is.\n"
		"    - UPPERCASE: Converts the stem to all uppercase.\n"
		"    - lowercase: Converts the stem to all lowercase.\n"
		"  - Increment By: (Primarily for Directory Scan with <num>) Specifies the value to add to the parsed number before inserting it with <num>. Can be positive or negative. Ignored if the filename doesn't contain a parseable number or if <num> is not used.\n\n"
		"  - Create backup before renaming: If checked, the entire target directory (in Directory Scan mode) or the directory containing the first file added (in Manual mode) will be copied to a timestamped backup folder within your Documents\\Backups\\RenameUtilityBackups folder before any renaming occurs. If the backup fails, renaming is aborted.\n\n"

		"==========================\n"
		" Actions\n"
		"==========================\n"
		"  - Preview Rename: Scans for files (Dir Scan) or uses the list (Manual), applies the patterns and options, and shows the proposed 'Old Name' -> 'New Name' changes in the list below. Check the 'Log' window for details, warnings, or errors (like potential overwrites or invalid inputs). The 'Perform Rename' button is only enabled after a successful preview that results in files to be renamed.\n"
		"  - Perform Rename: Executes the rename operations shown in the preview list. A confirmation prompt appears first. If backup is enabled, it happens before renaming.\n\n"

		"==========================\n"
		" Preview List & Log\n"
		"==========================\n"
		"  - Preview List: Shows the files identified for renaming. In Dir Scan mode, it shows 'Old Name' and 'New Name'. In Manual mode, it shows 'Index', 'Original Name', and 'New Name'. This list is populated after clicking 'Preview Rename' and only includes files that passed all checks and are scheduled for renaming.\n"
		"  - Log: Displays detailed information about the process: initialization, filters used, files found/skipped, warnings (e.g., target file exists), errors (e.g., invalid pattern, filesystem errors), backup status, rename results, and undo results.\n\n"

		"==========================\n"
		" Menu\n"
		"==========================\n"
		"  - File -> Save Profile...: Saves the current settings (mode, paths, patterns, options) under a chosen name.\n"
		"  - File -> Load Profile...: Loads previously saved settings.\n"
		"  - File -> Delete Profile...: Deletes a saved profile.\n"
		"  - File -> Undo Last Rename (Ctrl+Z): Reverts the immediately preceding successful rename operation. This is only enabled after a successful rename and is disabled after performing another action (preview, add/remove file, mode change, loading profile, closing app) or if the undo fails. It relies on renaming the files back to their original names; it does not use the backup. Use with caution, especially if files were moved or modified after renaming.\n"
		"  - File -> Exit: Closes the application (saves window size/position).\n"
		"  - Help -> Help... (F1): Shows this help information.\n"
		"  - Help -> About...: Shows application information.\n\n"

		"==========================\n"
		" Important Notes\n"
		"==========================\n"
		"  - Overwrites: The preview checks for potential overwrites where the target filename already exists and is not part of the current rename batch. Files causing such conflicts are skipped.\n"
		"  - Backup: Backups copy the entire source directory. This can be large. Backups are stored in Documents\\Backups\\RenameUtilityBackups. Manage these backups manually.\n"
		"  - Undo: Undo is fragile. It only reverses the last successful rename batch. Any intermediate file operations or errors during undo can lead to an inconsistent state. Always check the results.\n"
		"  - Invalid Characters: Generated filenames are automatically sanitized to remove characters invalid for Windows filenames (\\ / : * ? \" < > |).\n"
		"  - Error Handling: Check the Log window for errors or warnings during preview and rename operations.\n";

	HelpDialog helpDlg(this, wxID_ANY, "Help Topics", helpContent, wxDefaultPosition, wxSize(700, 850));
	helpDlg.ShowModal();
}

// Handles the window close event
void MainFrame::OnClose(wxCloseEvent &event)
{
	SaveSettings(); // Save window position, size, and last used inputs to config

	// Clean up dynamically allocated fs::path pointers associated with list items in Manual mode
	// This is crucial to prevent memory leaks upon closing the application
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
	// No item data is stored for Dir Scan mode, so no specific cleanup needed for its list items

	// Clear the undo state as it doesn't persist across sessions
	m_undoableOperations.clear();
	m_undoAvailable = false;

	event.Skip(); // Allow the window to close after performing cleanup
}