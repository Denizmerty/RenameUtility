#include "RenamerLogic.h"

#include <wx/log.h> // For wxLogWarning, if needed for less critical warnings

#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>	// For std::sort
#include <system_error> // For std::error_code
#include <stdexcept>	// For std::exception safety

namespace fs = std::filesystem;

// Executes the rename operations defined in the provided plan
RenameExecutionResult RenamerLogic::performRename(const std::vector<RenameOperation> &plan, int increment)
{
	RenameExecutionResult results;
	results.overallSuccess = false; // Default to false; set to true only if all operations succeed

	if (plan.empty())
	{
		// If the plan is empty, there's nothing to do; consider this a success
		results.overallSuccess = true;
		return results;
	}

	std::vector<RenameOperation> executionPlan = plan; // Create a mutable copy of the plan

	// Sort the execution plan to minimize potential conflicts during renaming,
	// especially when dealing with numbered sequences
	// The sort order depends on whether numbers are being incremented or decremented
	std::sort(executionPlan.begin(), executionPlan.end(),
			  [increment](const RenameOperation &a, const RenameOperation &b)
			  {
				  bool aHasNum = a.Number.has_value();
				  bool bHasNum = b.Number.has_value();

				  // If both operations involve numbered files, sort based on the increment direction
				  if (aHasNum && bHasNum)
				  {
					  if (a.Number.value() != b.Number.value())
					  {
						  // If incrementing (>0), rename files with higher original numbers first (descending sort on original number)
						  // This avoids conflicts like renaming "file2.txt" to "file3.txt" before "file3.txt" is moved out of the way
						  // If decrementing (<=0), rename files with lower original numbers first (ascending sort on original number)
						  return (increment > 0) ? (a.Number.value() > b.Number.value()) : (a.Number.value() < b.Number.value());
					  }
					  // If numbers are the same, fall through to other sorting criteria
				  }
				  // If one has a number and the other doesn't, or if numbers are identical,
				  // use the original index (for Manual mode) or full path as tie-breakers for stable sorting

				  // Use Index from Manual mode as a primary tie-breaker if available and different
				  // (Index is 0 in DirScan mode, so this mainly affects Manual mode ordering)
				  if (a.Index != b.Index)
				  {
					  return a.Index < b.Index;
				  }
				  // Final tie-breaker: original full path for consistent ordering
				  return a.OldFullPath < b.OldFullPath;
			  });

	bool anyFailure = false;
	for (const auto &op : executionPlan)
	{
		try
		{
			std::error_code existEc, targetExistEc, typeEc, renameEc;

			// Verify that the source file still exists and is a regular file before attempting to rename
			bool sourceExists = fs::exists(op.OldFullPath, existEc);
			if (existEc)
			{
				results.failedRenames.push_back({op.OldName, "Skipped: Filesystem error checking source existence: " + existEc.message()});
				anyFailure = true;
				continue;
			}
			if (!sourceExists)
			{
				results.failedRenames.push_back({op.OldName, "Skipped: Source file disappeared (" + op.OldFullPath.string() + ")."});
				anyFailure = true;
				continue;
			}
			if (!fs::is_regular_file(op.OldFullPath, typeEc) || typeEc)
			{
				results.failedRenames.push_back({op.OldName, "Skipped: Source is not a regular file (" + op.OldFullPath.string() + ")." + (typeEc ? " Error: " + typeEc.message() : "")});
				anyFailure = true;
				continue;
			}

			// Check if the target path already exists
			// This is a critical check, as fs::rename might overwrite on some platforms or fail on others
			// Identity renames (OldFullPath == NewFullPath) should have been filtered out by the planning stage
			if (op.OldFullPath != op.NewFullPath)
			{
				bool targetExists = fs::exists(op.NewFullPath, targetExistEc);
				if (targetExistEc)
				{
					results.failedRenames.push_back({op.OldName, "Skipped: Filesystem error checking target path (" + op.NewFullPath.string() + "): " + targetExistEc.message()});
					anyFailure = true;
					continue;
				}
				if (targetExists)
				{
					// The sort order attempts to prevent this for *planned* renames within the batch
					// This primarily catches conflicts with external files or unexpected filesystem behavior (e.g. case-insensitivity)
					results.failedRenames.push_back({op.OldName, "Skipped: Target path already exists (" + op.NewFullPath.string() + ")."});
					anyFailure = true;
					continue;
				}
			}
			else
			{
				// This case implies an identity rename made it past planning, which is unexpected
				// Log a warning and skip, as no actual rename is needed or possible
				wxLogWarning("Skipping identity rename operation for '%s' during execution phase", op.OldName.c_str());
				continue;
			}

			// Perform the actual rename operation
			fs::rename(op.OldFullPath, op.NewFullPath, renameEc);

			// Verify the outcome of the rename operation
			if (!renameEc)
			{
				// fs::rename reported success; double-check by verifying file presence/absence
				std::error_code verifyOldEc, verifyNewEc;
				bool oldStillExists = fs::exists(op.OldFullPath, verifyOldEc);
				bool newNowExists = fs::exists(op.NewFullPath, verifyNewEc);

				// Ideal outcome: no verification errors, old file is gone, new file exists
				if (!verifyOldEc && !verifyNewEc && !oldStillExists && newNowExists)
				{
					results.successfulRenameOps.push_back(op); // Record the successful operation
				}
				else
				{
					// Discrepancy found: rename reported success, but verification failed
					std::string verifyMsg = "Verification failed after rename reported success. ";
					if (oldStillExists)
						verifyMsg += "Old file still exists. ";
					else if (verifyOldEc)
						verifyMsg += "Error checking old (" + verifyOldEc.message() + "). ";
					if (!newNowExists)
						verifyMsg += "New file does not exist. ";
					else if (verifyNewEc)
						verifyMsg += "Error checking new (" + verifyNewEc.message() + "). ";
					results.failedRenames.push_back({op.OldName, verifyMsg});
					anyFailure = true;
				}
			}
			else
			{
				// fs::rename itself reported an error
				results.failedRenames.push_back({op.OldName, "Rename failed: " + renameEc.message()});
				anyFailure = true;
			}
		}
		catch (const fs::filesystem_error &ex)
		{
			// Catch specific filesystem exceptions for detailed error reporting
			std::string errMsg = "Filesystem Exception: " + std::string(ex.what());
			if (!ex.path1().empty())
				errMsg += " (Path1: " + ex.path1().string() + ")";
			if (!ex.path2().empty())
				errMsg += " (Path2: " + ex.path2().string() + ")";
			errMsg += " (Code: " + ex.code().message() + ")";
			results.failedRenames.push_back({op.OldName, errMsg});
			anyFailure = true;
		}
		catch (const std::exception &ex)
		{
			// Catch other standard library exceptions
			results.failedRenames.push_back({op.OldName, "General Exception: " + std::string(ex.what())});
			anyFailure = true;
		}
		catch (...)
		{
			// Catch any other unknown exceptions
			results.failedRenames.push_back({op.OldName, "Unknown exception occurred during rename."});
			anyFailure = true;
		}
	}

	// The overall success is true only if the plan was not empty to begin with AND no failures occurred during execution
	results.overallSuccess = !plan.empty() && !anyFailure;
	return results;
}