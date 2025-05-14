#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/config.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/radiobox.h>
#include <wx/textctrl.h>

#include "MainFrame.h"
#include "RenamerLogic.h" // For RenamerLogic::DefaultPath

#include <filesystem>

namespace fs = std::filesystem;

// Loads application settings (window position/size, last used input values) from config
void MainFrame::LoadSettings()
{
	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
		return; // Cannot load settings if config system is unavailable

	// Load window position. Size is handled in MainFrame constructor after initial Fit()
	int x = cfg->ReadLong("/Window/X", 50); // Default X position if not found
	int y = cfg->ReadLong("/Window/Y", 50); // Default Y position if not found
	SetPosition(wxPoint(x, y));

	// Load last used input values, providing sensible defaults if keys are missing
	m_currentMode = (RenamingMode)cfg->ReadLong("/Inputs/Mode", (long)RenamingMode::DirectoryScan);
	modeSelectionRadio->SetSelection((int)m_currentMode);

	// Use DefaultPath from RenamerLogic as a fallback if TargetDir is not in config
	dirPicker->SetPath(cfg->Read("/Inputs/TargetDir", wxString(RenamerLogic::DefaultPath.wstring())));
	fileNamePatternCtrl->SetValue(cfg->Read("/Inputs/FilenamePattern", "*.*"));
	filterExtensionsCtrl->SetValue(cfg->Read("/Inputs/FilterExtensions", wxEmptyString));
	// Spin control defaults match their creation values if config entries are absent
	lowestNumSpin->SetValue(cfg->ReadLong("/Inputs/LowestNum", 0));
	highestNumSpin->SetValue(cfg->ReadLong("/Inputs/HighestNum", 0));
	recursiveCheck->SetValue(cfg->ReadBool("/Inputs/RecursiveScan", false));

	patternCtrl->SetValue(cfg->Read("/Inputs/NamingPattern", "<orig_name><ext>"));
	findCtrl->SetValue(cfg->Read("/Inputs/FindText", wxEmptyString));
	replaceCtrl->SetValue(cfg->Read("/Inputs/ReplaceText", wxEmptyString));
	caseSensitiveCheck->SetValue(cfg->ReadBool("/Inputs/FindCaseSensitive", true)); // Default to case-sensitive find
	caseChoice->SetSelection(cfg->ReadLong("/Inputs/CaseConversion", 0));			// Default to "No Change"
	incrementSpin->SetValue(cfg->ReadLong("/Inputs/Increment", 1));
	backupCheck->SetValue(cfg->ReadBool("/Inputs/Backup", false)); // Default to backup disabled
}

// Saves current application settings (window position/size, input values) to config
void MainFrame::SaveSettings()
{
	wxConfigBase *cfg = wxConfigBase::Get();
	if (!cfg)
		return; // Cannot save settings if config system is unavailable

	// Save window position and size
	int x, y, w, h;
	GetPosition(&x, &y);
	GetSize(&w, &h);
	cfg->Write("/Window/X", (long)x);
	cfg->Write("/Window/Y", (long)y);
	cfg->Write("/Window/Width", (long)w);
	cfg->Write("/Window/Height", (long)h);

	// Save last used input values
	cfg->Write("/Inputs/Mode", (long)m_currentMode);
	cfg->Write("/Inputs/TargetDir", dirPicker->GetPath());
	cfg->Write("/Inputs/FilenamePattern", fileNamePatternCtrl->GetValue());
	cfg->Write("/Inputs/FilterExtensions", filterExtensionsCtrl->GetValue());
	cfg->Write("/Inputs/HighestNum", (long)highestNumSpin->GetValue());
	cfg->Write("/Inputs/LowestNum", (long)lowestNumSpin->GetValue());
	cfg->Write("/Inputs/RecursiveScan", recursiveCheck->IsChecked());
	cfg->Write("/Inputs/NamingPattern", patternCtrl->GetValue());
	cfg->Write("/Inputs/FindText", findCtrl->GetValue());
	cfg->Write("/Inputs/ReplaceText", replaceCtrl->GetValue());
	cfg->Write("/Inputs/FindCaseSensitive", caseSensitiveCheck->IsChecked());
	cfg->Write("/Inputs/CaseConversion", (long)caseChoice->GetSelection());
	cfg->Write("/Inputs/Increment", (long)incrementSpin->GetValue());
	cfg->Write("/Inputs/Backup", backupCheck->IsChecked());

	// Explicitly flush changes to ensure they are written to persistent storage
	cfg->Flush();
}