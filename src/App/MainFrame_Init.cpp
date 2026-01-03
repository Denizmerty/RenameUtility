#include "resource.h"
#include <wx/icon.h>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/aboutdlg.h>
#include <wx/accel.h> // Required for accelerator table
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choicdlg.h>
#include <wx/choice.h>
#include <wx/config.h>
#include <wx/dir.h>
#include <wx/dnd.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/filepicker.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>


#include "HelpDialog.h"
#include "MainFrame.h"
#include "RenamerLogic.h"
#include "WorkerThread.h"


#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>


namespace fs = std::filesystem;

// MainFrame constructor: Initializes UI elements, menus, status bar, and loads
// settings MainFrame constructor: Initializes UI elements, menus, status bar,
// and loads settings
MainFrame::MainFrame(const wxString &title, const wxPoint &pos,
                     const wxSize &size)
    : wxFrame(NULL, wxID_ANY, title, pos, size),
      m_currentMode(
          RenamingMode::DirectoryScan), // Default mode is DirectoryScan
      m_undoAvailable(false), m_previewSuccess(false),
      m_backupAttempted(false) {
  SetIcon(wxIcon(L"#1", wxBITMAP_TYPE_ICO_RESOURCE));
  // Create the menu bar
  wxMenu *menuFile = new wxMenu;
  menuFile->Append(ID_SaveProfile, "Save Profile...\tCtrl+S");
  menuFile->Append(ID_LoadProfile, "Load Profile...\tCtrl+L");
  menuFile->Append(ID_DeleteProfile, "Delete Profile...");
  menuFile->AppendSeparator();
  menuFile->Append(ID_ExportPreview, "Export Preview to CSV...");
  menuFile->AppendSeparator();
  menuFile->Append(ID_UndoRename,
                   "Undo Last Rename\tCtrl+Z"); // Add accelerator hint
  menuFile->AppendSeparator();
  menuFile->Append(wxID_EXIT, "E&xit", "Exit this program");

  wxMenu *menuHelp = new wxMenu;
  menuHelp->Append(ID_HelpTopics, "&Help...\tF1"); // Add accelerator hint
  menuHelp->AppendSeparator();
  menuHelp->Append(wxID_ABOUT);

  wxMenuBar *menuBar = new wxMenuBar;
  menuBar->Append(menuFile, "&File");
  menuBar->Append(menuHelp, "&Help");
  SetMenuBar(menuBar);
  menuBar->Enable(ID_UndoRename, false); // Undo is initially unavailable

  // Create the status bar
  CreateStatusBar(1);
  m_statusBar = GetStatusBar();
  UpdateStatusBar("Ready");

  // Create a single main panel to hold all other controls
  mainPanel = new wxPanel(this, wxID_ANY);

  // Create UI controls
  scrolledWindow =
      new wxScrolledWindow(mainPanel, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxVSCROLL | wxBORDER_SUNKEN);
  wxArrayString modeChoices;
  modeChoices.Add("Directory Scan");
  modeChoices.Add("Manual File Selection");
  modeSelectionRadio = new wxRadioBox(
      scrolledWindow, ID_ModeSelectionRadio, "Operation Mode",
      wxDefaultPosition, wxDefaultSize, modeChoices, 1, wxRA_SPECIFY_COLS);
  modeSelectionRadio->SetSelection(0); // Default to "Directory Scan"
  dirScanBox =
      new wxStaticBox(scrolledWindow, wxID_ANY, "Directory Scan Options");
  manualBox = new wxStaticBox(scrolledWindow, wxID_ANY,
                              "Manual File Selection Options");
  commonBox =
      new wxStaticBox(scrolledWindow, wxID_ANY, "Common Renaming Options");
  dirPicker = new wxDirPickerCtrl(scrolledWindow, ID_DirPicker, wxEmptyString,
                                  "Select...", wxDefaultPosition, wxDefaultSize,
                                  wxDIRP_DEFAULT_STYLE | wxDIRP_DIR_MUST_EXIST);
  fileNamePatternLabel = new wxStaticText(
      scrolledWindow, wxID_ANY, "Filename Pattern (find, uses *, ?):");
  fileNamePatternCtrl =
      new wxTextCtrl(scrolledWindow, ID_FileNamePatternCtrl, "*.*");
  filterExtensionsLabel = new wxStaticText(
      scrolledWindow, wxID_ANY, "Filter by Extensions (opt., comma-sep):");
  filterExtensionsCtrl =
      new wxTextCtrl(scrolledWindow, ID_FilterExtensionsCtrl, "");
  lowestNumLabel = new wxStaticText(scrolledWindow, wxID_ANY,
                                    "Lowest Number (optional filter):");
  lowestNumSpin =
      new wxSpinCtrl(scrolledWindow, wxID_ANY, "", wxDefaultPosition,
                     wxDefaultSize, wxSP_ARROW_KEYS, 0, 9999, 0);
  highestNumLabel = new wxStaticText(scrolledWindow, wxID_ANY,
                                     "Highest Number (optional filter):");
  highestNumSpin =
      new wxSpinCtrl(scrolledWindow, wxID_ANY, "", wxDefaultPosition,
                     wxDefaultSize, wxSP_ARROW_KEYS, 0, 9999, 0);
  recursiveCheck = new wxCheckBox(scrolledWindow, ID_RecursiveCheck,
                                  "Include Subdirectories");
  addFilesButton =
      new wxButton(scrolledWindow, ID_AddFilesButton, "Add Files...");
  removeFilesButton =
      new wxButton(scrolledWindow, ID_RemoveFilesButton, "Remove Selected");
  removeFilesButton->Enable(false); // Initially disabled as list is empty
  clearFilesButton =
      new wxButton(scrolledWindow, ID_ClearFilesButton, "Clear List");
  clearFilesButton->Enable(false); // Initially disabled
  patternLabel =
      new wxStaticText(scrolledWindow, wxID_ANY, "New Naming Pattern:");
  patternCtrl =
      new wxTextCtrl(scrolledWindow, ID_PatternCtrl, "<orig_name><ext>");
  findLabel =
      new wxStaticText(scrolledWindow, wxID_ANY, "Find Text (Optional):");
  findCtrl = new wxTextCtrl(scrolledWindow, ID_FindCtrl, "");
  replaceLabel = new wxStaticText(scrolledWindow, wxID_ANY, "Replace With:");
  replaceCtrl = new wxTextCtrl(scrolledWindow, ID_ReplaceCtrl, "");
  caseSensitiveCheck =
      new wxCheckBox(scrolledWindow, ID_CaseCheck, "Case Sensitive Find");
  regexModeCheck = new wxCheckBox(scrolledWindow, wxID_ANY, "Use Regex");
  caseChoiceLabel = new wxStaticText(scrolledWindow, wxID_ANY, "Change Case:");
  wxArrayString caseOptions;
  caseOptions.Add("No Change");
  caseOptions.Add("UPPERCASE");
  caseOptions.Add("lowercase");
  caseChoice = new wxChoice(scrolledWindow, ID_CaseChoice, wxDefaultPosition,
                            wxDefaultSize, caseOptions);
  caseChoice->SetSelection(0); // Default to "No Change"
  incrementLabel = new wxStaticText(scrolledWindow, wxID_ANY, "Increment By:");
  incrementSpin =
      new wxSpinCtrl(scrolledWindow, wxID_ANY, "", wxDefaultPosition,
                     wxDefaultSize, wxSP_ARROW_KEYS, -9999, 9999, 1);
  backupCheck =
      new wxCheckBox(scrolledWindow, wxID_ANY, "Create backup before renaming");
  bottomPanel = new wxPanel(
      mainPanel, wxID_ANY); // Panel for buttons, preview list, and log
  previewButton = new wxButton(bottomPanel, ID_PreviewButton, "Preview Rename");
  renameButton = new wxButton(bottomPanel, ID_RenameButton, "Perform Rename");
  renameButton->Enable(false); // Initially disabled until a successful preview
  previewList = new wxListCtrl(bottomPanel, wxID_ANY, wxDefaultPosition,
                               wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
  logLabel = new wxStaticText(bottomPanel, wxID_ANY, "Log:");
  logTextCtrl = new wxTextCtrl(bottomPanel, wxID_ANY, "", wxDefaultPosition,
                               wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
  progressBar = new wxGauge(bottomPanel, wxID_ANY, 100, wxDefaultPosition,
                            wxSize(-1, 16), wxGA_HORIZONTAL | wxGA_SMOOTH);
  progressBar->SetValue(0);
  m_defaultTextCtrlBgColour =
      fileNamePatternCtrl
          ->GetBackgroundColour(); // Store default for resetting error states

  // Set up the layout of all UI elements
  SetupLayout();

  // Enable drag and drop for files/directories onto the main panel
  mainPanel->SetDropTarget(new FileDropTarget(this));

  // Load last used settings from config, then update UI accordingly
  LoadSettings();
  UpdateUIForMode();          // Reflects loaded mode and settings
  UpdatePreviewListColumns(); // Sets columns based on current mode
  mainPanel->Layout();

  // Finalize window sizing, respecting minimum requirements and saved
  // dimensions
  this->Fit(); // Calculate minimum size needed by sizers
  wxSize minReqSize = this->GetSize();
  wxConfigBase *config = wxConfigBase::Get();
  int savedW = minReqSize.GetWidth();
  int savedH = 850; // Default height if not saved
  if (config) {
    savedW = config->ReadLong("/Window/Width", minReqSize.GetWidth());
    savedH = config->ReadLong("/Window/Height", 850);
  }
  // Ensure saved dimensions are not smaller than minimum required or arbitrary
  // minimums
  if (savedW < minReqSize.GetWidth())
    savedW = minReqSize.GetWidth();
  if (savedH < minReqSize.GetHeight())
    savedH = minReqSize.GetHeight();
  if (savedW < 700)
    savedW = 700; // Arbitrary minimum sensible width
  if (savedH < 600)
    savedH = 600; // Arbitrary minimum sensible height
  this->SetSize(savedW, savedH);
  this->SetMinSize(minReqSize);

  // Bind all necessary event handlers
  BindEvents();

  // Explicitly ensure Undo is initially disabled
  SetUndoAvailable(false);
}

// Arranges UI elements within the MainFrame using sizers
void MainFrame::SetupLayout() {
  // Sizer for the top input area (mode selection, scan options, manual options,
  // common options)
  wxBoxSizer *inputAreaSizer = new wxBoxSizer(wxVERTICAL);
  inputAreaSizer->Add(modeSelectionRadio, 0, wxEXPAND | wxALL, 5);

  // Sizer for Directory Scan specific options
  dirScanSizer = new wxStaticBoxSizer(dirScanBox, wxVERTICAL);
  wxFlexGridSizer *dirGridSizer =
      new wxFlexGridSizer(5, 2, 5, 5); // 5 rows, 2 columns, 5px gaps
  dirGridSizer->AddGrowableCol(1);     // Second column (controls) should grow
  dirGridSizer->Add(
      new wxStaticText(scrolledWindow, wxID_ANY, "Target Directory:"), 0,
      wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  dirGridSizer->Add(dirPicker, 1, wxEXPAND | wxALL, 2);
  dirGridSizer->Add(fileNamePatternLabel, 0,
                    wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  dirGridSizer->Add(fileNamePatternCtrl, 1, wxEXPAND | wxALL, 2);
  dirGridSizer->Add(filterExtensionsLabel, 0,
                    wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  dirGridSizer->Add(filterExtensionsCtrl, 1, wxEXPAND | wxALL, 2);
  dirGridSizer->Add(lowestNumLabel, 0,
                    wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  dirGridSizer->Add(lowestNumSpin, 1, wxEXPAND | wxALL, 2);
  dirGridSizer->Add(highestNumLabel, 0,
                    wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  dirGridSizer->Add(highestNumSpin, 1, wxEXPAND | wxALL, 2);
  dirScanSizer->Add(dirGridSizer, 0, wxEXPAND | wxALL, 5);
  dirScanSizer->Add(recursiveCheck, 0,
                    wxALIGN_LEFT | wxLEFT | wxRIGHT | wxBOTTOM, 5);
  inputAreaSizer->Add(dirScanSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      5);

  // Sizer for Manual File Selection specific options
  manualSizer = new wxStaticBoxSizer(manualBox, wxVERTICAL);
  wxBoxSizer *manualButtonSizer = new wxBoxSizer(wxHORIZONTAL);
  manualButtonSizer->Add(addFilesButton, 0, wxALL, 2);
  manualButtonSizer->AddStretchSpacer(
      1); // Pushes Remove/Clear buttons to the right
  manualButtonSizer->Add(removeFilesButton, 0, wxALL, 2);
  manualButtonSizer->Add(clearFilesButton, 0, wxALL, 2);
  manualSizer->Add(manualButtonSizer, 0, wxEXPAND | wxALL, 5);
  inputAreaSizer->Add(manualSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      5);

  // Sizer for Common Renaming Options
  commonSizer = new wxStaticBoxSizer(commonBox, wxVERTICAL);
  wxFlexGridSizer *commonGridSizer =
      new wxFlexGridSizer(6, 2, 5, 5); // 6 rows, 2 columns
  commonGridSizer->AddGrowableCol(1);  // Second column (controls) grows
  commonGridSizer->Add(patternLabel, 0,
                       wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  commonGridSizer->Add(patternCtrl, 1, wxEXPAND | wxALL, 2);
  commonGridSizer->Add(findLabel, 0,
                       wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  commonGridSizer->Add(findCtrl, 1, wxEXPAND | wxALL, 2);
  commonGridSizer->Add(replaceLabel, 0,
                       wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  commonGridSizer->Add(replaceCtrl, 1, wxEXPAND | wxALL, 2);
  commonGridSizer->AddSpacer(0); // Empty cell for alignment with checkbox below
  wxBoxSizer *checkboxSizer = new wxBoxSizer(wxHORIZONTAL);
  checkboxSizer->Add(caseSensitiveCheck, 0, wxALL, 2);
  checkboxSizer->Add(regexModeCheck, 0, wxLEFT | wxALL, 10);
  commonGridSizer->Add(checkboxSizer, 1, wxEXPAND | wxALL, 2);
  commonGridSizer->Add(caseChoiceLabel, 0,
                       wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  commonGridSizer->Add(caseChoice, 1, wxEXPAND | wxALL, 2);
  commonGridSizer->Add(incrementLabel, 0,
                       wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, 2);
  commonGridSizer->Add(incrementSpin, 1, wxEXPAND | wxALL, 2);
  commonSizer->Add(commonGridSizer, 0, wxEXPAND | wxALL, 5);
  inputAreaSizer->Add(commonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      5);

  inputAreaSizer->Add(backupCheck, 0,
                      wxALIGN_LEFT | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  scrolledWindow->SetSizer(inputAreaSizer);
  scrolledWindow->SetScrollRate(0, 20); // Vertical scroll step
  scrolledWindow->FitInside();          // Recalculate scrollbars

  // Sizer for the bottom area (action buttons, preview list, log)
  wxBoxSizer *bottomAreaSizer = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer *actionButtonSizer = new wxBoxSizer(wxHORIZONTAL);
  actionButtonSizer->AddStretchSpacer(1); // Pushes buttons to the right
  actionButtonSizer->Add(previewButton, 0, wxALL, 5);
  actionButtonSizer->Add(renameButton, 0, wxALL, 5);
  bottomAreaSizer->Add(actionButtonSizer, 0,
                       wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

  // Sizer to hold preview list and log side-by-side
  wxBoxSizer *previewLogSizer = new wxBoxSizer(wxHORIZONTAL);
  previewLogSizer->Add(previewList, 1, wxEXPAND | wxALL,
                       5); // Preview list takes half the space
  wxBoxSizer *logAreaSizer = new wxBoxSizer(wxVERTICAL);
  logAreaSizer->Add(logLabel, 0, wxLEFT | wxRIGHT | wxTOP, 5);
  logAreaSizer->Add(progressBar, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
  logAreaSizer->Add(logTextCtrl, 1, wxEXPAND | wxALL, 5);
  previewLogSizer->Add(logAreaSizer, 1, wxEXPAND | wxALL,
                       5); // Log area takes the other half
  bottomAreaSizer->Add(previewLogSizer, 1,
                       wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                       5); // Preview/Log area expands
  bottomPanel->SetSizer(bottomAreaSizer);

  // Main sizer for the frame, dividing space between scrolled input area and
  // bottom panel
  wxBoxSizer *mainFrameSizer = new wxBoxSizer(wxVERTICAL);
  mainFrameSizer->Add(scrolledWindow, 1, wxEXPAND | wxALL,
                      5); // Input area expands
  mainFrameSizer->Add(bottomPanel, 1, wxEXPAND | wxALL,
                      5); // Bottom panel also expands
  mainPanel->SetSizer(mainFrameSizer);
}

// Binds UI events to their respective handler functions
void MainFrame::BindEvents() {
  // File Menu events
  Bind(wxEVT_MENU, &MainFrame::OnSaveProfile, this, ID_SaveProfile);
  Bind(wxEVT_MENU, &MainFrame::OnLoadProfile, this, ID_LoadProfile);
  Bind(wxEVT_MENU, &MainFrame::OnDeleteProfile, this, ID_DeleteProfile);
  Bind(wxEVT_MENU, &MainFrame::OnUndoRename, this, ID_UndoRename);
  Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
  // Help Menu events
  Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
  Bind(wxEVT_MENU, &MainFrame::OnHelpTopics, this, ID_HelpTopics);
  // Window and control events
  Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
  Bind(wxEVT_RADIOBOX, &MainFrame::OnModeChange, this, ID_ModeSelectionRadio);
  Bind(wxEVT_BUTTON, &MainFrame::OnAddFilesClick, this, ID_AddFilesButton);
  Bind(wxEVT_BUTTON, &MainFrame::OnRemoveFilesClick, this,
       ID_RemoveFilesButton);
  Bind(wxEVT_BUTTON, &MainFrame::OnClearFilesClick, this, ID_ClearFilesButton);
  previewButton->Bind(wxEVT_BUTTON, &MainFrame::OnPreviewClick, this,
                      ID_PreviewButton);
  renameButton->Bind(wxEVT_BUTTON, &MainFrame::OnRenameClick, this,
                     ID_RenameButton);
  // Custom worker thread completion events
  this->Bind(EVT_PREVIEW_COMPLETE, &MainFrame::OnPreviewThreadComplete, this);
  this->Bind(EVT_RENAME_COMPLETE, &MainFrame::OnRenameThreadComplete, this);
  this->Bind(EVT_UNDO_COMPLETE, &MainFrame::OnUndoThreadComplete, this);
  this->Bind(EVT_PROGRESS_UPDATE, &MainFrame::OnProgressUpdate, this);
  // Column sorting for preview list
  previewList->Bind(wxEVT_LIST_COL_CLICK, &MainFrame::OnPreviewColumnClick,
                    this);
  // Real-time preview on pattern changes
  m_previewTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &MainFrame::OnPreviewTimer, this);
  patternCtrl->Bind(wxEVT_TEXT, &MainFrame::OnPatternTextChanged, this);
  findCtrl->Bind(wxEVT_TEXT, &MainFrame::OnPatternTextChanged, this);
  replaceCtrl->Bind(wxEVT_TEXT, &MainFrame::OnPatternTextChanged, this);
  // Export menu event
  Bind(wxEVT_MENU, &MainFrame::OnExportPreview, this, ID_ExportPreview);
  // Keyboard accelerators
  wxAcceleratorEntry entries[6];
  entries[0].Set(wxACCEL_NORMAL, WXK_F1, ID_HelpTopics);
  entries[1].Set(wxACCEL_CTRL, (int)'Z', ID_UndoRename);
  entries[2].Set(wxACCEL_CTRL, (int)'P', ID_PreviewButton);
  entries[3].Set(wxACCEL_CTRL, (int)'R', ID_RenameButton);
  entries[4].Set(wxACCEL_CTRL, (int)'S', ID_SaveProfile);
  entries[5].Set(wxACCEL_CTRL, (int)'L', ID_LoadProfile);
  wxAcceleratorTable accel(6, entries);
  this->SetAcceleratorTable(accel);
}