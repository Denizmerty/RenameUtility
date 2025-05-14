#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <wx/thread.h>
#include <wx/event.h>
#include "RenamerLogic.h" // Includes InputParams, OutputResults, RenameOperation, UndoResult etc.

class MainFrame;

// Add UNDO_RENAME task
enum class WorkerTask
{
	CALCULATE_PREVIEW,
	PERFORM_RENAME,
	UNDO_RENAME // << New Task
};

// Container for results from PERFORM_RENAME task
struct RenameThreadResults
{
	BackupResult backupResult;
	RenameExecutionResult renameResult;
	bool backupAttempted = false;
};

// Declare the custom event type for undo completion
wxDECLARE_EVENT(EVT_UNDO_COMPLETE, wxCommandEvent);

class WorkerThread : public wxThread
{
public:
	// Constructor for CALCULATE_PREVIEW task
	WorkerThread(MainFrame *handler, const InputParams &params);

	// Constructor for PERFORM_RENAME task
	WorkerThread(MainFrame *handler,
				 const std::vector<RenameOperation> &plan,
				 int increment,
				 const fs::path &targetDir,
				 const std::string &contextName, // Changed param name
				 bool doBackup);

	// >> Constructor for UNDO_RENAME task <<
	WorkerThread(MainFrame *handler, const std::vector<RenameOperation> &opsToUndo);

	virtual ~WorkerThread() {};

protected:
	virtual ExitCode Entry() override;

private:
	MainFrame *m_handler;
	WorkerTask m_task;

	// Parameters for CALCULATE_PREVIEW
	InputParams m_inputParams;

	// Parameters for PERFORM_RENAME
	std::vector<RenameOperation> m_renamePlan;
	int m_increment;
	fs::path m_targetDir;
	std::string m_contextName; // Used for backup naming convention
	bool m_doBackup;

	// >> Parameters for UNDO_RENAME <<
	std::vector<RenameOperation> m_undoOperations;

	// Helper to post results back to the main thread
	void PostResultEvent(wxEventType eventType, void *data);
};

#endif // WORKERTHREAD_H