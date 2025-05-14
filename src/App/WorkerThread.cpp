#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include "WorkerThread.h"
#include "RenamerLogic.h"
#include "MainFrame.h" // Needed for posting events

// Constructor for CALCULATE_PREVIEW task
WorkerThread::WorkerThread(MainFrame *handler, const InputParams &params)
    : wxThread(wxTHREAD_JOINABLE),
      m_handler(handler),
      m_task(WorkerTask::CALCULATE_PREVIEW),
      m_inputParams(params)
{
}

// Constructor for PERFORM_RENAME task
WorkerThread::WorkerThread(MainFrame *handler,
                           const std::vector<RenameOperation> &plan,
                           int increment,
                           const fs::path &targetDir,
                           const std::string &contextName, // Changed param name
                           bool doBackup)
    : wxThread(wxTHREAD_JOINABLE),
      m_handler(handler),
      m_task(WorkerTask::PERFORM_RENAME),
      m_renamePlan(plan),
      m_increment(increment),
      m_targetDir(targetDir),
      m_contextName(contextName),
      m_doBackup(doBackup)
{
}

// Constructor for UNDO_RENAME task
WorkerThread::WorkerThread(MainFrame *handler, const std::vector<RenameOperation> &opsToUndo)
    : wxThread(wxTHREAD_JOINABLE),
      m_handler(handler),
      m_task(WorkerTask::UNDO_RENAME),
      m_undoOperations(opsToUndo) // Store the operations to be undone
{
}

// Helper function to safely post events back to the MainFrame
void WorkerThread::PostResultEvent(wxEventType eventType, void *data)
{
    if (m_handler)
    {
        wxCommandEvent event(eventType);
        event.SetClientData(data);              // Pass the dynamically allocated data
        wxQueueEvent(m_handler, event.Clone()); // Queue a clone of the event
    }
    else if (data)
    {
        // If handler is somehow null, we must prevent memory leaks
        wxLogDebug("WorkerThread::PostResultEvent: Handler is null, deleting event data.");
        try
        {
            // Add cases for different event data types
            if (eventType == EVT_PREVIEW_COMPLETE)
                delete static_cast<OutputResults *>(data);
            else if (eventType == EVT_RENAME_COMPLETE)
                delete static_cast<RenameThreadResults *>(data);
            else if (eventType == EVT_UNDO_COMPLETE) // << Handle UndoResult
                delete static_cast<UndoResult *>(data);
            // Add other event types here if necessary
        }
        catch (...)
        {
            wxLogError("WorkerThread::PostResultEvent: Exception caught while deleting event data for null handler.");
        }
    }
}

wxThread::ExitCode WorkerThread::Entry()
{
    if (TestDestroy())
        return (ExitCode)0;

    try
    {
        if (m_task == WorkerTask::CALCULATE_PREVIEW)
        {
            OutputResults *results = new OutputResults();
            *results = RenamerLogic::calculateRenamePlan(m_inputParams);
            if (TestDestroy())
            {
                delete results;
                return (ExitCode)0;
            }
            PostResultEvent(EVT_PREVIEW_COMPLETE, results);
        }
        else if (m_task == WorkerTask::PERFORM_RENAME)
        {
            RenameThreadResults *results = new RenameThreadResults();
            results->backupAttempted = m_doBackup;
            if (m_doBackup)
            {
                results->backupResult = RenamerLogic::performBackup(m_targetDir, m_contextName);
            }
            else
            {
                results->backupResult.success = true;
            }
            if (TestDestroy())
            {
                delete results;
                return (ExitCode)0;
            }
            if (results->backupResult.success)
            {
                results->renameResult = RenamerLogic::performRename(m_renamePlan, m_increment);
            }
            else
            {
                results->renameResult.overallSuccess = false; // Ensure rename fails if backup fails
            }
            if (TestDestroy())
            {
                delete results;
                return (ExitCode)0;
            }
            PostResultEvent(EVT_RENAME_COMPLETE, results);
        }
        else if (m_task == WorkerTask::UNDO_RENAME) // << Handle Undo Task
        {
            UndoResult *results = new UndoResult();
            *results = RenamerLogic::performUndo(m_undoOperations); // Pass vector by value
            if (TestDestroy())
            {
                delete results;
                return (ExitCode)0;
            }
            PostResultEvent(EVT_UNDO_COMPLETE, results);
        }
    }
    catch (const std::exception &e)
    {
        wxLogError("Unhandled std::exception in worker thread (%s): %s",
                   (m_task == WorkerTask::CALCULATE_PREVIEW ? "Preview" : (m_task == WorkerTask::PERFORM_RENAME ? "Rename" : "Undo")),
                   e.what());
        // Attempt to post an error result back - simplified error posting
        if (m_task == WorkerTask::CALCULATE_PREVIEW)
        {
            OutputResults *errRes = new OutputResults();
            errRes->success = false;
            errRes->errorLog.push_back("FATAL EXCEPTION (Preview): " + std::string(e.what()));
            PostResultEvent(EVT_PREVIEW_COMPLETE, errRes);
        }
        else if (m_task == WorkerTask::PERFORM_RENAME)
        {
            RenameThreadResults *errRes = new RenameThreadResults();
            errRes->backupAttempted = m_doBackup;
            errRes->backupResult.success = false;
            errRes->backupResult.errorMessage = "FATAL EXCEPTION (Rename)";
            errRes->renameResult.overallSuccess = false;
            errRes->renameResult.failedRenames.push_back({"N/A", "FATAL EXCEPTION: " + std::string(e.what())});
            PostResultEvent(EVT_RENAME_COMPLETE, errRes);
        }
        else // UNDO_RENAME
        {
            UndoResult *errRes = new UndoResult();
            errRes->overallSuccess = false;
            errRes->failedUndos.push_back({"N/A", "FATAL EXCEPTION (Undo): " + std::string(e.what())});
            PostResultEvent(EVT_UNDO_COMPLETE, errRes);
        }
    }
    catch (...)
    {
        wxLogError("Unknown exception caught in worker thread (%s)!",
                   (m_task == WorkerTask::CALCULATE_PREVIEW ? "Preview" : (m_task == WorkerTask::PERFORM_RENAME ? "Rename" : "Undo")));
        // Post generic error
        if (m_task == WorkerTask::CALCULATE_PREVIEW)
        {
            OutputResults *errRes = new OutputResults();
            errRes->success = false;
            errRes->errorLog.push_back("FATAL UNKNOWN EXCEPTION (Preview)");
            PostResultEvent(EVT_PREVIEW_COMPLETE, errRes);
        }
        else if (m_task == WorkerTask::PERFORM_RENAME)
        {
            RenameThreadResults *errRes = new RenameThreadResults();
            errRes->backupResult.success = false;
            errRes->backupResult.errorMessage = "FATAL UNKNOWN EXCEPTION (Rename)";
            errRes->renameResult.overallSuccess = false;
            errRes->renameResult.failedRenames.push_back({"N/A", "FATAL UNKNOWN EXCEPTION"});
            PostResultEvent(EVT_RENAME_COMPLETE, errRes);
        }
        else // UNDO_RENAME
        {
            UndoResult *errRes = new UndoResult();
            errRes->overallSuccess = false;
            errRes->failedUndos.push_back({"N/A", "FATAL UNKNOWN EXCEPTION (Undo)"});
            PostResultEvent(EVT_UNDO_COMPLETE, errRes);
        }
    }

    return (ExitCode)0;
}