#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dnd.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "RenamerLogic.h"
#include <deque>
#include <filesystem>
#include <optional>
#include <vector>

wxDECLARE_EVENT(EVT_PREVIEW_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_RENAME_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_UNDO_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(EVT_PROGRESS_UPDATE, wxCommandEvent);

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
enum ControlIDs {
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

  // Export Menu ID
  ID_ExportPreview,

  // Undo Menu ID
  ID_UndoRename
};

class MainFrame : public wxFrame {
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
  wxCheckBox *regexModeCheck;
  wxStaticText *caseChoiceLabel;
  wxChoice *caseChoice;
  wxStaticText *incrementLabel;
  wxSpinCtrl *incrementSpin;
  wxCheckBox *backupCheck;
  wxPanel *bottomPanel;
  wxButton *previewButton;
  wxButton *renameButton;
  wxListCtrl *previewList;
  wxGauge *progressBar;
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
  RenameExecutionResult
      m_lastRenameResult; // Holds results of last rename attempt
  BackupResult m_lastBackupResult;
  bool m_backupAttempted;
  wxColour m_defaultTextCtrlBgColour;

  // Undo State - Multi-level undo stack (up to 10 levels)
  static const size_t MAX_UNDO_LEVELS = 10;
  std::deque<std::vector<RenameOperation>> m_undoStack;
  bool m_undoAvailable;

  // Sorting state for preview list
  int m_sortColumn = -1;
  bool m_sortAscending = true;

  // Real-time preview timer
  wxTimer m_previewTimer;

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
  void
  OnUndoThreadComplete(wxCommandEvent &event); // << New Handler for Undo result

  // Profile Event Handlers
  void OnSaveProfile(wxCommandEvent &event);
  void OnLoadProfile(wxCommandEvent &event);
  void OnDeleteProfile(wxCommandEvent &event);

  // Undo Event Handler
  void OnUndoRename(wxCommandEvent &event);

  // Export Preview Handler
  void OnExportPreview(wxCommandEvent &event);

  // Progress Handler
  void OnProgressUpdate(wxCommandEvent &event);

  // Column sorting handler
  void OnPreviewColumnClick(wxListEvent &event);

  // Real-time preview
  void OnPreviewTimer(wxTimerEvent &event);
  void OnPatternTextChanged(wxCommandEvent &event);

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
class FileDropTarget : public wxFileDropTarget {
public:
  FileDropTarget(MainFrame *owner);
  virtual bool OnDropFiles(wxCoord x, wxCoord y,
                           const wxArrayString &filenames) override;

private:
  MainFrame *m_owner;
};

#endif // MAINFRAME_H