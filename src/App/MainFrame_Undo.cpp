#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/msgdlg.h>
#include <wx/menu.h>
#include <wx/log.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/textctrl.h>

#include "MainFrame.h"
#include "WorkerThread.h"

#include <vector>
#include <string>

// Helper to manage the availability of the "Undo Last Rename" menu item and associated state
void MainFrame::SetUndoAvailable(bool available)
{
	m_undoAvailable = available;
	if (!available)
	{
		m_undoableOperations.clear(); // Clear stored operations when undo becomes unavailable
	}
	wxMenuBar *menuBar = GetMenuBar();
	if (menuBar)
	{
		menuBar->Enable(ID_UndoRename, m_undoAvailable);
	}
}

// Handles the "File -> Undo Last Rename" menu item click
void MainFrame::OnUndoRename(wxCommandEvent &event)
{
	if (!m_undoAvailable || m_undoableOperations.empty())
	{
		UpdateStatusBar("Nothing to undo.");
		// No message box needed, status bar is sufficient for this minor feedback
		return;
	}

	// Confirm the undo operation with the user
	wxString msg = wxString::Format(
		"Are you sure you want to undo the last rename operation?\n"
		"This will attempt to rename %zu file(s) back to their previous names.",
		m_undoableOperations.size());
	if (wxMessageBox(msg, "Confirm Undo Operation", wxYES_NO | wxICON_QUESTION | wxCENTRE, this) != wxYES)
	{
		UpdateStatusBar("Undo operation cancelled.");
		return;
	}

	// Prepare for Undo: copy operations to pass to the thread
	// This is important as m_undoableOperations will be cleared by SetUndoAvailable
	std::vector<RenameOperation> opsToUndo = m_undoableOperations;

	// Immediately disable further undo attempts and clear the stored state to prevent multiple undos of the same batch
	SetUndoAvailable(false);

	logTextCtrl->AppendText("\n--- Starting Undo Operation ---\n");
	UpdateStatusBar("Attempting to undo rename...");
	SetUIBusy(true); // Disable UI elements during the undo process

	// Launch a worker thread to perform the undo operation asynchronously
	WorkerThread *thread = new WorkerThread(this, opsToUndo); // Pass the copied operations

	if (!thread)
	{
		wxLogError("Failed to create worker thread for undo!");
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create undo worker thread.");
		// Undo remains disabled as the state is now uncertain
		return;
	}
	if (thread->Create() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to create undo worker thread resource.");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to create undo thread resource.");
		return;
	}
	if (thread->Run() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Failed to run undo worker thread!");
		delete thread;
		SetUIBusy(false);
		UpdateStatusBar("Error: Failed to run undo worker thread.");
		return;
	}
	// Thread is running; results will be handled by OnUndoThreadComplete via EVT_UNDO_COMPLETE
}