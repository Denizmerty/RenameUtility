#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textctrl.h>


#include "MainFrame.h"
#include "WorkerThread.h"

#include <string>
#include <vector>


// Helper to manage the availability of the "Undo Last Rename" menu item and
// associated state
void MainFrame::SetUndoAvailable(bool available) {
  m_undoAvailable = available;
  if (!available) {
    m_undoStack.clear(); // Clear all undo history when undo becomes unavailable
  }
  wxMenuBar *menuBar = GetMenuBar();
  if (menuBar) {
    menuBar->Enable(ID_UndoRename, m_undoAvailable);
  }
}

// Handles the "File -> Undo Last Rename" menu item click
void MainFrame::OnUndoRename(wxCommandEvent &event) {
  if (!m_undoAvailable || m_undoStack.empty()) {
    UpdateStatusBar("Nothing to undo.");
    // No message box needed, status bar is sufficient for this minor feedback
    return;
  }

  // Get the most recent undo batch from the stack
  std::vector<RenameOperation> opsToUndo = m_undoStack.front();

  // Confirm the undo operation with the user
  wxString msg = wxString::Format(
      "Are you sure you want to undo the last rename operation?\n"
      "This will attempt to rename %zu file(s) back to their previous names.\n"
      "(Undo levels remaining: %zu)",
      opsToUndo.size(), m_undoStack.size());
  if (wxMessageBox(msg, "Confirm Undo Operation",
                   wxYES_NO | wxICON_QUESTION | wxCENTRE, this) != wxYES) {
    UpdateStatusBar("Undo operation cancelled.");
    return;
  }

  // Pop from the stack (opsToUndo already contains the front batch)
  m_undoStack.pop_front();
  // Update availability based on remaining stack
  m_undoAvailable = !m_undoStack.empty();

  logTextCtrl->AppendText("\n--- Starting Undo Operation ---\n");
  UpdateStatusBar("Attempting to undo rename...");
  SetUIBusy(true); // Disable UI elements during the undo process

  // Launch a worker thread to perform the undo operation asynchronously
  WorkerThread *thread =
      new WorkerThread(this, opsToUndo); // Pass the copied operations

  if (!thread) {
    wxLogError("Failed to create worker thread for undo!");
    SetUIBusy(false);
    UpdateStatusBar("Error: Failed to create undo worker thread.");
    // Undo remains disabled as the state is now uncertain
    return;
  }
  if (thread->Create() != wxTHREAD_NO_ERROR) {
    wxLogError("Failed to create undo worker thread resource.");
    delete thread;
    SetUIBusy(false);
    UpdateStatusBar("Error: Failed to create undo thread resource.");
    return;
  }
  if (thread->Run() != wxTHREAD_NO_ERROR) {
    wxLogError("Failed to run undo worker thread!");
    delete thread;
    SetUIBusy(false);
    UpdateStatusBar("Error: Failed to run undo worker thread.");
    return;
  }
  // Thread is running; results will be handled by OnUndoThreadComplete via
  // EVT_UNDO_COMPLETE
}