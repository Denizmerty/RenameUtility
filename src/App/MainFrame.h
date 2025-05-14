#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/wx.h>
#include <wx/statusbr.h>
#include <wx/settings.h>
#include <wx/dnd.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/checkbox.h>

#include "RenamerLogic.h"
#include <filesystem>
#include <vector>
#include <optional>

wxDECLARE_EVENT(EVT_PREVIEW_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_RENAME_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UNDO_COMPLETE, wxCommandEvent); // << Declare Undo Event

// Forward declarations
class wxPanel;
class wxDirPickerCtrl;
class wxSpinCtrl;
class wxListCtrl;
class wxButton;
class wxTextCtrl;
class wxStaticText;
class FileDropTarget;
class wxCloseEvent;
class wxStaticBoxSizer;
class wxScrolledWindow;
class wxStaticBox;

namespace fs = std::filesystem;

// Control IDs Enum
enum ControlIDs
{
	// Existing Control IDs ...
	ID_DirPicker = wxID_HIGHEST + 1,
	ID_PatternCtrl,
	ID_FindCtrl,
	ID_ReplaceCtrl,
	ID_CaseCheck,
	ID_CaseChoice,
	ID_PreviewButton,
	ID_RenameButton,
	ID_HelpTopics,
	ID_ModeSelectionRadio,
	ID_AddFilesButton,
	ID_RemoveFilesButton,
	ID_ClearFilesButton,
	ID_RecursiveCheck,
	ID_FileNamePatternCtrl,
	ID_FilterExtensionsCtrl,

	// Profile Menu IDs
	ID_SaveProfile,
	ID_LoadProfile,
	ID_DeleteProfile,

	// Undo Menu ID
	ID_UndoRename // << New
};

class MainFrame : public wxFrame
{
	friend class FileDropTarget;

public:
	MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size);

private:
	// UI Elements
	wxPanel *mainPanel;
	wxScrolledWindow *scrolledWindow;
	wxRadioBox *modeSelectionRadio;
	wxStaticBox *dirScanBox;
	wxStaticBox *manualBox;
	wxStaticBox *commonBox;
	wxStaticBoxSizer *dirScanSizer;
	wxStaticBoxSizer *manualSizer;
	wxStaticBoxSizer *commonSizer;
	wxDirPickerCtrl *dirPicker;
	wxStaticText *fileNamePatternLabel;
	wxTextCtrl *fileNamePatternCtrl;
	wxStaticText *filterExtensionsLabel;
	wxTextCtrl *filterExtensionsCtrl;
	wxStaticText *lowestNumLabel;
	wxSpinCtrl *lowestNumSpin;
	wxStaticText *highestNumLabel;
	wxSpinCtrl *highestNumSpin;
	wxCheckBox *recursiveCheck;
	wxButton *addFilesButton;
	wxButton *removeFilesButton;
	wxButton *clearFilesButton;
	wxStaticText *patternLabel;
	wxTextCtrl *patternCtrl;
	wxStaticText *findLabel;
	wxTextCtrl *findCtrl;
	wxStaticText *replaceLabel;
	wxTextCtrl *replaceCtrl;
	wxCheckBox *caseSensitiveCheck;
	wxStaticText *caseChoiceLabel;
	wxChoice *caseChoice;
	wxStaticText *incrementLabel;
	wxSpinCtrl *incrementSpin;
	wxCheckBox *backupCheck;
	wxPanel *bottomPanel;
	wxButton *previewButton;
	wxButton *renameButton;
	wxListCtrl *previewList;
	wxStaticText *logLabel;
	wxTextCtrl *logTextCtrl;
	wxStatusBar *m_statusBar;

	// State Variables
	RenamingMode m_currentMode;
	std::vector<fs::path> m_manualFiles;
	InputParams m_lastValidParams;
	OutputResults m_lastPreviewResults;
	fs::path m_lastBackupPath;
	bool m_previewSuccess;
	RenameExecutionResult m_lastRenameResult; // Holds results of last rename attempt
	BackupResult m_lastBackupResult;
	bool m_backupAttempted;
	wxColour m_defaultTextCtrlBgColour;

	// Undo State
	std::vector<RenameOperation> m_undoableOperations; // Stores the successful operations from the last rename
	bool m_undoAvailable;							   // Flag indicating if undo is possible

	// Initialization & Layout
	void SetupLayout();
	void BindEvents();

	// Event Handlers
	void OnModeChange(wxCommandEvent &event);
	void OnAddFilesClick(wxCommandEvent &event);
	void OnRemoveFilesClick(wxCommandEvent &event);
	void OnClearFilesClick(wxCommandEvent &event);
	void OnPreviewClick(wxCommandEvent &event);
	void OnRenameClick(wxCommandEvent &event);
	void OnExit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnHelpTopics(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);
	void OnPreviewThreadComplete(wxCommandEvent &event);
	void OnRenameThreadComplete(wxCommandEvent &event);
	void OnUndoThreadComplete(wxCommandEvent &event); // << New Handler for Undo result

	// Profile Event Handlers
	void OnSaveProfile(wxCommandEvent &event);
	void OnLoadProfile(wxCommandEvent &event);
	void OnDeleteProfile(wxCommandEvent &event);

	// Undo Event Handler
	void OnUndoRename(wxCommandEvent &event);

	// Helper Functions
	void SetUIBusy(bool busy);
	void UpdateStatusBar(const wxString &text);
	void ResetInputBackgrounds();
	void UpdateUIForMode();
	void UpdatePreviewListColumns();
	void PopulateManualPreviewList();
	void SetUndoAvailable(bool available); // << Helper to manage undo state

	// Drag & Drop Handlers
	void SetDroppedDirectory(const wxString &path);
	void AddDroppedFiles(const wxArrayString &filenames);

	// Settings Persistence
	void LoadSettings(); // Loads last used settings
	void SaveSettings(); // Saves last used settings

	// Profile Helper
	wxArrayString GetProfileNames();
};

// Unified drop target class declaration
class FileDropTarget : public wxFileDropTarget
{
public:
	FileDropTarget(MainFrame *owner);
	virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames) override;

private:
	MainFrame *m_owner;
};

#endif // MAINFRAME_H