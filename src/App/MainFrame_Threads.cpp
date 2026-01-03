#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/textctrl.h>

#include "MainFrame.h"
#include "RenamerLogic.h"
#include "WorkerThread.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Handles the completion of the preview calculation worker thread
void MainFrame::OnPreviewThreadComplete(wxCommandEvent &event) {
  SetUIBusy(false);                    // Re-enable UI elements
  progressBar->SetValue(100);          // Set progress to complete
  wxTextAttr redStyle(*wxRED);         // For error messages
  wxColour warningColour(255, 190, 0); // Amber/Orange for warnings
  wxTextAttr warningStyle(warningColour);
  wxTextAttr normalStyle; // Default text style
  logTextCtrl->SetDefaultStyle(normalStyle);
  logTextCtrl->AppendText("Preview calculation thread finished.\n");

  OutputResults *results = static_cast<OutputResults *>(event.GetClientData());
  if (!results) {
    logTextCtrl->SetDefaultStyle(redStyle);
    logTextCtrl->AppendText(
        "Error: Failed to receive preview results from worker thread.\n");
    logTextCtrl->SetDefaultStyle(normalStyle);
    wxLogError("Received null data pointer for preview results.");
    UpdateStatusBar("Error: Preview data lost.");
    renameButton->Enable(false);
    m_previewSuccess = false;
    return; // Cannot proceed without results
  }

  // Store results and update application state
  m_lastPreviewResults = *results; // Copy the results data
  m_previewSuccess = results->success;
  delete results; // Delete the heap-allocated data received from the thread

  // Log messages from the results structure
  for (const auto &msg : m_lastPreviewResults.generalInfoLog) {
    logTextCtrl->AppendText("Info: " + wxString(msg) + "\n");
  }
  logTextCtrl->SetDefaultStyle(warningStyle);
  for (const auto &msg : m_lastPreviewResults.warningLog) {
    logTextCtrl->AppendText("Warning: " + wxString(msg) + "\n");
  }
  logTextCtrl->SetDefaultStyle(normalStyle);
  for (const auto &msg : m_lastPreviewResults.missingSourceFilesLog) {
    logTextCtrl->AppendText("Skipped/Missing: " + wxString(msg) + "\n");
  }
  logTextCtrl->SetDefaultStyle(warningStyle);
  for (const auto &po : m_lastPreviewResults.potentialOverwritesLog) {
    logTextCtrl->AppendText(
        "Potential Overwrite: Skipped renaming '" + wxString(po.SourceFile) +
        "' to '" + wxString(po.TargetFile) +
        "' because target path exists and is not part of this rename batch.\n");
  }
  logTextCtrl->SetDefaultStyle(redStyle);
  for (const auto &msg : m_lastPreviewResults.errorLog) {
    logTextCtrl->AppendText("Error: " + wxString(msg) + "\n");
  }
  logTextCtrl->SetDefaultStyle(normalStyle);

  // Clear the preview list (and any associated item data from Manual mode's
  // pre-population) before repopulating with results
  for (long i = 0; i < previewList->GetItemCount(); ++i) {
    wxUIntPtr data = previewList->GetItemData(i);
    if (data) {
      delete reinterpret_cast<fs::path *>(data);
    }
  }
  previewList->DeleteAllItems();

  // Populate the preview list with operations from the successful renamePlan
  if (!m_lastPreviewResults.renamePlan.empty()) {
    logTextCtrl->AppendText(
        "Populating preview list with planned renames...\n");
    long itemIndex = 0;
    for (const auto &op : m_lastPreviewResults.renamePlan) {
      if (m_currentMode == RenamingMode::DirectoryScan) {
        previewList->InsertItem(itemIndex, wxString(op.OldName));
        previewList->SetItem(itemIndex, 1, wxString(op.NewName));
        // No item data is stored for list items in Directory Scan mode
      } else { // Manual Mode
        previewList->InsertItem(
            itemIndex,
            std::to_string(op.Index)); // Use 1-based index from the operation
        previewList->SetItem(itemIndex, 1, wxString(op.OldName));
        previewList->SetItem(itemIndex, 2, wxString(op.NewName));
        // Store a new fs::path pointer for the OldFullPath of items in the
        // actual rename plan This replaces any path data stored during the
        // initial PopulateManualPreviewList
        previewList->SetItemData(itemIndex, reinterpret_cast<wxUIntPtr>(
                                                new fs::path(op.OldFullPath)));
      }
      // Highlight rows with conflicts
      if (op.hasConflict) {
        previewList->SetItemBackgroundColour(
            itemIndex, wxColour(255, 200, 200)); // Light red
      }
      itemIndex++;
    }

    // Count conflicts to inform user
    int conflictCount = 0;
    for (const auto &op : m_lastPreviewResults.renamePlan) {
      if (op.hasConflict)
        conflictCount++;
    }

    // Enable/Disable rename button based on overall success and if there are
    // items in the plan
    if (m_previewSuccess) {
      if (conflictCount > 0) {
        logTextCtrl->SetDefaultStyle(warningStyle);
        logTextCtrl->AppendText(
            wxString::Format("Warning: %d file(s) have conflicts and will be "
                             "skipped during rename.\n",
                             conflictCount));
        logTextCtrl->SetDefaultStyle(normalStyle);
      }
      logTextCtrl->AppendText("Preview generated successfully.\n");
      UpdateStatusBar(wxString::Format(
          "Preview ready: %d file(s) to be renamed%s.",
          (int)m_lastPreviewResults.renamePlan.size(),
          conflictCount > 0 ? wxString::Format(" (%d conflicts)", conflictCount)
                            : wxString("")));
      renameButton->Enable(true);
    } else {
      logTextCtrl->AppendText(
          "Preview generation completed with errors. See log.\n");
      UpdateStatusBar("Preview failed. Check log.");
      renameButton->Enable(false);
      wxMessageBox("Preview generation failed or encountered errors. Please "
                   "check the log for details.",
                   "Preview Error", wxOK | wxICON_ERROR, this);
    }
  } else {                       // No items in the renamePlan
    renameButton->Enable(false); // Ensure rename is disabled
    if (m_previewSuccess) {
      // Preview technically succeeded, but no files matched criteria or were
      // eligible
      logTextCtrl->AppendText("Preview complete: No files found matching "
                              "criteria or eligible for renaming.\n");
      UpdateStatusBar("Preview: No files eligible for rename.");
      // Show an info box only if there were no errors/warnings at all,
      // otherwise a warning
      if (m_lastPreviewResults.errorLog.empty() &&
          m_lastPreviewResults.warningLog.empty() &&
          m_lastPreviewResults.missingSourceFilesLog.empty() &&
          m_lastPreviewResults.potentialOverwritesLog.empty()) {
        wxMessageBox("No files were found matching the specified criteria or "
                     "no renames are necessary.",
                     "Preview Information", wxOK | wxICON_INFORMATION, this);
      } else {
        wxMessageBox("Preview complete, but no files are eligible for "
                     "renaming. Check the log for skipped files or warnings.",
                     "Preview Information", wxOK | wxICON_WARNING, this);
      }
    } else {
      // Preview failed, and resulted in no files
      logTextCtrl->AppendText("Preview generation failed, resulting in no "
                              "files to rename. See log.\n");
      UpdateStatusBar("Preview failed. Check log.");
      wxMessageBox(
          "Preview generation failed. Please check the log for errors.",
          "Preview Error", wxOK | wxICON_ERROR, this);
    }
  }
}

// Handles the completion of the rename operation worker thread
void MainFrame::OnRenameThreadComplete(wxCommandEvent &event) {
  SetUIBusy(false);           // Re-enable UI
  progressBar->SetValue(100); // Set progress to complete

  wxTextAttr redStyle(*wxRED);
  wxTextAttr normalStyle;
  logTextCtrl->SetDefaultStyle(normalStyle);
  logTextCtrl->AppendText("Rename operation thread finished.\n");

  RenameThreadResults *results =
      static_cast<RenameThreadResults *>(event.GetClientData());
  if (!results) {
    logTextCtrl->SetDefaultStyle(redStyle);
    logTextCtrl->AppendText(
        "Error: Failed to receive rename results from worker thread.\n");
    logTextCtrl->SetDefaultStyle(normalStyle);
    wxLogError("Received null data pointer for rename results.");
    UpdateStatusBar("Error: Rename data lost.");
    m_previewSuccess = false; // Invalidate preview state
    renameButton->Enable(false);
    SetUndoAvailable(false);
    return;
  }

  // Store results
  m_lastBackupResult = results->backupResult;
  m_lastRenameResult = results->renameResult;
  m_backupAttempted = results->backupAttempted;
  // Store backup path only if backup was successful
  m_lastBackupPath =
      m_lastBackupResult.success ? m_lastBackupResult.backupPath : fs::path();

  delete results; // Delete heap-allocated data

  // Process backup results first. If backup failed, rename was aborted by the
  // worker
  if (m_backupAttempted) {
    if (!m_lastBackupResult.success) {
      logTextCtrl->SetDefaultStyle(redStyle);
      logTextCtrl->AppendText("CRITICAL ERROR: Backup failed: " +
                              wxString(m_lastBackupResult.errorMessage) +
                              "\nRename operation was aborted.\n");
      logTextCtrl->SetDefaultStyle(normalStyle);
      UpdateStatusBar("Backup failed. Rename aborted.");
      wxMessageBox(
          "Backup FAILED!\n" + m_lastBackupResult.errorMessage +
              "\nThe rename operation was aborted to prevent data loss.",
          "Backup Error", wxOK | wxICON_ERROR, this);
      m_previewSuccess = false;
      renameButton->Enable(false);
      SetUndoAvailable(false);
      // Clear list items and associated data (especially in Manual mode) as
      // state is now invalid
      for (long i = 0; i < previewList->GetItemCount(); ++i) {
        wxUIntPtr data = previewList->GetItemData(i);
        if (data) {
          delete reinterpret_cast<fs::path *>(data);
        }
      }
      previewList->DeleteAllItems();
      if (m_currentMode == RenamingMode::ManualSelection) {
        m_manualFiles.clear();       // Clear internal list too
        PopulateManualPreviewList(); // Update UI to reflect empty list
      }
      return; // Stop further processing
    } else {
      logTextCtrl->AppendText("Backup completed successfully: " +
                              wxString(m_lastBackupPath.wstring()) + "\n");
    }
  }

  // Process rename results
  bool renameAttempted = !m_lastRenameResult.successfulRenameOps.empty() ||
                         !m_lastRenameResult.failedRenames.empty();
  int successCount = m_lastRenameResult.successfulRenameOps.size();
  int failCount = m_lastRenameResult.failedRenames.size();

  if (renameAttempted) {
    logTextCtrl->AppendText("--- Rename Execution Results ---\n");
    // Log successful renames
    for (const auto &op : m_lastRenameResult.successfulRenameOps) {
      logTextCtrl->AppendText("Success: '" + wxString(op.OldName) +
                              "' renamed to '" + wxString(op.NewName) + "'\n");
    }
    // Log failed renames
    if (failCount > 0) {
      logTextCtrl->SetDefaultStyle(redStyle);
      logTextCtrl->AppendText("--- Failures ---\n");
      for (const auto &pair : m_lastRenameResult.failedRenames) {
        logTextCtrl->AppendText("FAILED: '" + wxString(pair.first) +
                                "': " + wxString(pair.second) + "\n");
      }
      logTextCtrl->SetDefaultStyle(normalStyle);
    }

    // Report overall status and manage Undo availability
    if (m_lastRenameResult.overallSuccess) {
      logTextCtrl->AppendText("Rename operation completed successfully.\n");
      UpdateStatusBar(wxString::Format("Rename successful: %d file(s) renamed.",
                                       successCount));
      wxMessageBox(
          wxString::Format("%d file(s) renamed successfully.", successCount),
          "Rename Successful", wxOK | wxICON_INFORMATION, this);
      // Enable Undo only if the rename was fully successful and resulted in
      // changes - push to multi-level undo stack
      if (!m_lastRenameResult.successfulRenameOps.empty()) {
        m_undoStack.push_front(m_lastRenameResult.successfulRenameOps);
        // Limit stack size to MAX_UNDO_LEVELS
        while (m_undoStack.size() > MAX_UNDO_LEVELS) {
          m_undoStack.pop_back();
        }
      }
      SetUndoAvailable(!m_undoStack.empty());
      // Write to history log
      RenamerLogic::writeHistoryLog(m_lastRenameResult.successfulRenameOps,
                                    "RENAME");
    } else {
      logTextCtrl->AppendText("Rename operation completed with errors.\n");
      UpdateStatusBar(
          wxString::Format("Rename finished: %d successful, %d failed.",
                           successCount, failCount));
      wxMessageBox(wxString::Format("Rename operation completed, but %d "
                                    "error(s) occurred. Please check the log.",
                                    failCount),
                   "Rename Errors", wxOK | wxICON_WARNING, this);
      SetUndoAvailable(false); // Disable undo if there were any failures
    }
  } else {
    // No rename operations were attempted (e.g., backup failed, or plan was
    // initially empty)
    if (m_backupAttempted && m_lastBackupResult.success) {
      logTextCtrl->AppendText(
          "Backup was successful, but no rename operations were performed.\n");
      UpdateStatusBar("Backup OK. No files renamed.");
    } else if (!m_backupAttempted) {
      logTextCtrl->AppendText("No rename operations were performed.\n");
      UpdateStatusBar("Rename: Nothing to do.");
    }
    SetUndoAvailable(false);
  }

  logTextCtrl->AppendText("--- Rename Process End ---\n");

  // Post-rename cleanup
  m_previewSuccess = false;    // Invalidate the preview
  renameButton->Enable(false); // Disable rename button

  // Clear the preview list and its associated data
  for (long i = 0; i < previewList->GetItemCount(); ++i) {
    wxUIntPtr data = previewList->GetItemData(i);
    if (data) {
      delete reinterpret_cast<fs::path *>(data);
    }
  }
  previewList->DeleteAllItems();

  // If in manual mode, clear the internal list as files have been renamed (or
  // failed)
  if (m_currentMode == RenamingMode::ManualSelection) {
    m_manualFiles.clear();
    PopulateManualPreviewList(); // Update UI to reflect empty list
  }

  // Handle backup retention messaging based on rename success and undo
  // availability
  bool renameSucceededFully =
      renameAttempted && m_lastRenameResult.overallSuccess;
  if (m_backupAttempted && m_lastBackupResult.success &&
      !m_lastBackupPath.empty()) {
    if (renameSucceededFully && m_undoAvailable) {
      // Successful rename, undo is possible, so backup is key for full recovery
      logTextCtrl->AppendText(
          "Backup retained at: " + wxString(m_lastBackupPath.wstring()) + "\n");
      UpdateStatusBar(
          wxString::Format("Finished: %d OK. Backup kept.", successCount));
    } else if (!renameSucceededFully) {
      // Rename had errors, backup is essential for potential manual recovery
      logTextCtrl->AppendText("Backup retained due to rename errors: " +
                              wxString(m_lastBackupPath.wstring()) + "\n");
      wxMessageBox(
          "Rename operation had errors. The backup has been retained:\n" +
              m_lastBackupPath.wstring(),
          "Backup Retained", wxOK | wxICON_WARNING, this);
      UpdateStatusBar(
          wxString::Format("Finished: %d OK, %d FAIL. Backup retained.",
                           successCount, failCount));
    }
  }
}

// Handles the completion of the undo operation worker thread
void MainFrame::OnUndoThreadComplete(wxCommandEvent &event) {
  SetUIBusy(false);           // Re-enable UI
  progressBar->SetValue(100); // Set progress to complete

  wxTextAttr redStyle(*wxRED);
  wxTextAttr normalStyle;
  logTextCtrl->SetDefaultStyle(normalStyle);
  logTextCtrl->AppendText("Undo operation thread finished.\n");

  UndoResult *results = static_cast<UndoResult *>(event.GetClientData());
  if (!results) {
    logTextCtrl->SetDefaultStyle(redStyle);
    logTextCtrl->AppendText(
        "Error: Failed to receive undo results from worker thread.\n");
    logTextCtrl->SetDefaultStyle(normalStyle);
    wxLogError("Received null data pointer for undo results.");
    UpdateStatusBar("Error: Undo data lost.");
    SetUndoAvailable(false); // Ensure undo remains unavailable
    return;
  }

  // Log undo results
  logTextCtrl->AppendText("--- Undo Execution Results ---\n");
  for (const auto &pair : results->successfulUndos) {
    logTextCtrl->AppendText("Success: Reverted '" + wxString(pair.first) +
                            "' back to '" + wxString(pair.second) + "'\n");
  }
  if (!results->failedUndos.empty()) {
    logTextCtrl->SetDefaultStyle(redStyle);
    logTextCtrl->AppendText("--- Failures ---\n");
    for (const auto &pair : results->failedUndos) {
      logTextCtrl->AppendText("FAILED Undo: '" + wxString(pair.first) +
                              "': " + wxString(pair.second) + "\n");
    }
    logTextCtrl->SetDefaultStyle(normalStyle);
  }

  // Report overall undo status
  if (results->overallSuccess) {
    logTextCtrl->AppendText("Undo operation completed successfully.\n");
    UpdateStatusBar(wxString::Format("Undo successful: %zu file(s) reverted.",
                                     results->successfulUndos.size()));
    wxMessageBox("The last rename operation was successfully undone.",
                 "Undo Complete", wxOK | wxICON_INFORMATION, this);
  } else {
    logTextCtrl->AppendText("Undo operation completed with errors.\n");
    UpdateStatusBar(wxString::Format(
        "Undo finished: %zu successful, %zu failed.",
        results->successfulUndos.size(), results->failedUndos.size()));
    wxMessageBox(
        wxString::Format(
            "Undo operation completed, but %zu error(s) occurred. Please check "
            "the log and verify the file status manually.",
            results->failedUndos.size()),
        "Undo Errors", wxOK | wxICON_WARNING, this);
  }
  logTextCtrl->AppendText("--- Undo Process End ---\n");

  delete results; // Delete heap-allocated data

  // Post-undo cleanup
  SetUndoAvailable(false);  // Undo is a one-shot operation for the last rename
  m_previewSuccess = false; // Invalidate preview state
  renameButton->Enable(false);

  // Clear preview list and associated item data (if in Manual mode)
  if (m_currentMode == RenamingMode::ManualSelection) {
    for (long i = 0; i < previewList->GetItemCount(); ++i) {
      wxUIntPtr data = previewList->GetItemData(i);
      if (data) {
        delete reinterpret_cast<fs::path *>(data);
      }
    }
  }
  previewList->DeleteAllItems();

  // Clear internal state that is now potentially invalid after undo
  m_lastPreviewResults = {};
  m_lastValidParams = {};
  m_lastRenameResult = {}; // Clear last rename result details
  // The last backup (m_lastBackupResult, m_lastBackupPath) might still be
  // relevant for user reference

  // If in manual mode, clear the list as file state is potentially mixed after
  // undo
  if (m_currentMode == RenamingMode::ManualSelection) {
    m_manualFiles.clear();
    PopulateManualPreviewList(); // Update UI to reflect empty list
  }

  UpdateStatusBar("Ready"); // Reset status bar
}