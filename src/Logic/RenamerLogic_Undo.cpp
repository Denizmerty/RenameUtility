#include "RenamerLogic.h"

#include <wx/log.h> // For wxLogWarning, if needed for less critical warnings

#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>	// For std::reverse
#include <system_error> // For std::error_code
#include <stdexcept>	// For std::exception safety

namespace fs = std::filesystem;

// Attempts to undo a previous rename operation by reverting files to their original names
UndoResult RenamerLogic::performUndo(std::vector<RenameOperation> opsToUndo) // Pass by value to allow modification (reversing)
{
	UndoResult results;
	results.overallSuccess = false; // Default to false; set to true only if all undo operations succeed

	if (opsToUndo.empty())
	{
		// If there are no operations to undo, consider it a success (nothing to do)
		results.overallSuccess = true;
		return results;
	}

	// Undo operations should be performed in the reverse order of their original execution
	// This helps to avoid conflicts if the original renames involved sequential numbering or dependencies
	std::reverse(opsToUndo.begin(), opsToUndo.end());

	bool anyFailure = false;
	for (const auto &op : opsToUndo)
	{
		// For an undo operation:
		// - The "current path" is the file's path *after* the rename (op.NewFullPath)
		// - The "target path" for undo is the file's path *before* the rename (op.OldFullPath)
		const fs::path &currentPath = op.NewFullPath;
		const fs::path &originalPath = op.OldFullPath;

		try
		{
			std::error_code existEc, targetExistEc, typeEc, renameEc;

			// Verify that the current file (the one to be reverted) still exists and is a regular file
			bool sourceExists = fs::exists(currentPath, existEc);
			if (existEc)
			{
				results.failedUndos.push_back({op.NewName, "Skipped Undo: Filesystem error checking current file existence: " + existEc.message()});
				anyFailure = true;
				continue;
			}
			if (!sourceExists)
			{
				results.failedUndos.push_back({op.NewName, "Skipped Undo: Current file not found (" + currentPath.string() + "). Cannot revert."});
				anyFailure = true;
				continue;
			}
			if (!fs::is_regular_file(currentPath, typeEc) || typeEc)
			{
				results.failedUndos.push_back({op.NewName, "Skipped Undo: Current path is not a regular file (" + currentPath.string() + ")." + (typeEc ? " Error: " + typeEc.message() : "")});
				anyFailure = true;
				continue;
			}

			// Check if the original path (the target for undo) is already occupied by another file
			// This prevents overwriting an unrelated file that might have been created or moved to the original location
			// Identity renames (originalPath == currentPath) should not occur if the original rename plan was valid
			if (originalPath != currentPath)
			{
				bool targetExists = fs::exists(originalPath, targetExistEc);
				if (targetExistEc)
				{
					results.failedUndos.push_back({op.NewName, "Skipped Undo: Filesystem error checking original path (" + originalPath.string() + "): " + targetExistEc.message()});
					anyFailure = true;
					continue;
				}
				if (targetExists)
				{
					results.failedUndos.push_back({op.NewName, "Skipped Undo: Original path is already occupied (" + originalPath.string() + ")."});
					anyFailure = true;
					continue;
				}
			}
			else
			{
				// This implies an identity rename in the original plan, which is unexpected for undo
				wxLogWarning("Skipping identity undo operation for '%s' during undo phase", op.NewName.c_str());
				continue;
			}

			// Perform the rename operation to revert the file (from NewFullPath back to OldFullPath)
			fs::rename(currentPath, originalPath, renameEc);

			// Verify the outcome of the undo rename operation
			if (!renameEc)
			{
				// fs::rename reported success; double-check by verifying file presence/absence
				std::error_code verifyCurrentEc, verifyOriginalEc;
				bool currentStillExists = fs::exists(currentPath, verifyCurrentEc);	 // Should be gone
				bool originalNowExists = fs::exists(originalPath, verifyOriginalEc); // Should exist

				// Ideal outcome: no verification errors, current file is gone, original file exists
				if (!verifyCurrentEc && !verifyOriginalEc && !currentStillExists && originalNowExists)
				{
					results.successfulUndos.push_back({op.NewName, op.OldName}); // Record successful undo (NewName -> OldName)
				}
				else
				{
					// Discrepancy found: rename reported success, but verification failed
					std::string verifyMsg = "Verification failed after undo rename reported success. ";
					if (currentStillExists)
						verifyMsg += "Current file still exists. ";
					else if (verifyCurrentEc)
						verifyMsg += "Error checking current (" + verifyCurrentEc.message() + "). ";
					if (!originalNowExists)
						verifyMsg += "Original file does not exist. ";
					else if (verifyOriginalEc)
						verifyMsg += "Error checking original (" + verifyOriginalEc.message() + "). ";
					results.failedUndos.push_back({op.NewName, verifyMsg});
					anyFailure = true;
				}
			}
			else
			{
				// fs::rename itself reported an error
				results.failedUndos.push_back({op.NewName, "Undo rename failed: " + renameEc.message()});
				anyFailure = true;
			}
		}
		catch (const fs::filesystem_error &ex)
		{
			// Catch specific filesystem exceptions for detailed error reporting
			std::string errMsg = "Filesystem Exception during undo: " + std::string(ex.what());
			if (!ex.path1().empty())
				errMsg += " (Path1: " + ex.path1().string() + ")";
			if (!ex.path2().empty())
				errMsg += " (Path2: " + ex.path2().string() + ")";
			errMsg += " (Code: " + ex.code().message() + ")";
			results.failedUndos.push_back({op.NewName, errMsg});
			anyFailure = true;
		}
		catch (const std::exception &ex)
		{
			// Catch other standard library exceptions
			results.failedUndos.push_back({op.NewName, "General Exception during undo: " + std::string(ex.what())});
			anyFailure = true;
		}
		catch (...)
		{
			// Catch any other unknown exceptions
			results.failedUndos.push_back({op.NewName, "Unknown exception occurred during undo."});
			anyFailure = true;
		}
	}

	// The overall success of the undo operation is true only if the list of operations was not empty
	// AND no failures occurred during any of the individual undo attempts
	results.overallSuccess = !opsToUndo.empty() && !anyFailure;
	return results;
}