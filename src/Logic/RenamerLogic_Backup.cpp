#include "RenamerLogic.h"

#include <wx/stdpaths.h> // For wxStandardPaths to find user's Documents directory
#include <wx/string.h>	 // For wxString usage with wxWidgets utilities

#include <filesystem>
#include <string>
#include <vector>
#include <chrono>		// For generating timestamps
#include <ctime>		// For timestamp formatting
#include <iomanip>		// For std::put_time
#include <sstream>		// For std::stringstream
#include <system_error> // For std::error_code
#include <stdexcept>	// For std::runtime_error
#include <regex>		// For sanitizing context name for folder naming

namespace fs = std::filesystem;

// Recursively copies the contents of a source directory to a destination directory
// Throws std::runtime_error on failure to provide detailed error context
void RenamerLogic::CopyDirectory(const fs::path &source, const fs::path &destination)
{
	try
	{
		std::error_code ec;

		// Create destination directory if it doesn't exist
		if (!fs::exists(destination, ec) || ec)
		{
			if (!fs::create_directories(destination, ec) || ec)
			{
				throw std::runtime_error("Failed to create destination directory: " + destination.string() + (ec ? " (" + ec.message() + ")" : ""));
			}
		}
		else if (!fs::is_directory(destination, ec) || ec)
		{
			// Destination exists but is not a directory, which is an error for backup
			throw std::runtime_error("Backup destination path exists but is not a directory: " + destination.string() + (ec ? " (" + ec.message() + ")" : ""));
		}

		// Iterate through source directory contents
		for (const auto &entry : fs::directory_iterator(source))
		{
			const fs::path &srcPath = entry.path();
			const fs::path dstPath = destination / srcPath.filename(); // Construct corresponding destination path
			std::error_code copyEc;

			if (fs::is_directory(srcPath, copyEc))
			{
				// Recursively copy subdirectories
				if (!copyEc)
				{
					CopyDirectory(srcPath, dstPath);
				}
				else
				{
					throw std::runtime_error("Error checking if source path is directory '" + srcPath.string() + "': " + copyEc.message());
				}
			}
			else if (fs::is_regular_file(srcPath, copyEc))
			{
				// Copy regular files, overwriting if they exist in the destination
				if (!copyEc)
				{
					fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, copyEc);
					if (copyEc)
					{
						throw std::runtime_error("Failed to copy file '" + srcPath.string() + "' to '" + dstPath.string() + "': " + copyEc.message());
					}
				}
				else
				{
					throw std::runtime_error("Error checking if source path is regular file '" + srcPath.string() + "': " + copyEc.message());
				}
			}
			// Other file types (symlinks, etc) are skipped; no warning logged to avoid noise for common scenarios
			else if (copyEc)
			{
				// Error occurred while checking the type of the source path
				throw std::runtime_error("Error checking type of source path '" + srcPath.string() + "': " + copyEc.message());
			}
		}
	}
	catch (const fs::filesystem_error &e)
	{
		// Catch filesystem errors specifically to provide more context in the error message
		std::string errorMsg = "Directory copy failed (fs::filesystem_error): '";
		errorMsg += (e.path1().empty() ? source.string() : e.path1().string());
		errorMsg += "' -> '";
		errorMsg += (e.path2().empty() ? destination.string() : e.path2().string());
		errorMsg += "'. Reason: " + std::string(e.what());
		throw std::runtime_error(errorMsg);
	}
	catch (const std::exception &ex)
	{
		// Catch other potential standard exceptions
		throw std::runtime_error("Directory copy failed for '" + source.string() + "' to '" + destination.string() + "': " + std::string(ex.what()));
	}
	catch (...)
	{
		// Catch-all for any other unknown errors
		throw std::runtime_error("Unknown error during directory copy from '" + source.string() + "' to '" + destination.string() + "'.");
	}
}

// Determines the parent directory for application backups, typically within the user's Documents folder
fs::path RenamerLogic::GetDefaultBackupParentPathInternal()
{
	wxString docsDirWx = wxStandardPaths::Get().GetDocumentsDir();
	fs::path backupBasePath;

	if (docsDirWx.IsEmpty())
	{
		// Fallback to current working directory if Documents directory cannot be determined
		backupBasePath = fs::current_path();
		// A warning could be logged here if such a fallback is undesirable but for now it's silent
	}
	else
	{
		backupBasePath = fs::path(docsDirWx.ToStdWstring()); // Convert wxString to fs::path
	}

	// Define a specific subfolder for this application's backups to keep them organized
	return backupBasePath / "RenameUtilityBackups";
}

// Performs a backup of the sourcePath to a timestamped folder within the application's backup directory
BackupResult RenamerLogic::performBackup(const fs::path &sourcePath, const std::string &contextName)
{
	BackupResult result;
	result.success = false; // Assume failure initially
	fs::path backupParentDir;

	// Determine the base directory for backups
	try
	{
		backupParentDir = GetDefaultBackupParentPathInternal();
	}
	catch (const std::exception &ex)
	{
		result.errorMessage = "Failed to determine backup base directory: " + std::string(ex.what());
		return result;
	}

	// Create a timestamp string for the backup folder name to ensure uniqueness
	auto now = std::chrono::system_clock::now();
	auto now_c = std::chrono::system_clock::to_time_t(now);
	std::tm now_tm = {};
	std::stringstream timestamp_ss;
#ifdef _WIN32
	if (localtime_s(&now_tm, &now_c) != 0)
	{ // Windows-specific safe version
		result.errorMessage = "Failed to get local time for timestamp.";
		return result;
	}
#else
	if (localtime_r(&now_c, &now_tm) == nullptr)
	{ // POSIX-specific reentrant version
		result.errorMessage = "Failed to get local time for timestamp.";
		return result;
	}
#endif
	timestamp_ss << std::put_time(&now_tm, "%Y%m%d_%H%M%S"); // Format: YYYYMMDD_HHMMSS
	if (timestamp_ss.fail())
	{
		result.errorMessage = "Failed to format timestamp.";
		return result;
	}
	std::string timestamp = timestamp_ss.str();

	// Sanitize the contextName to create a safe folder name component
	std::string safeContext = contextName;
	if (safeContext.empty())
	{
		// If context is empty, attempt to use the source directory's name as a fallback
		if (fs::is_directory(sourcePath) && sourcePath.has_filename())
		{
			safeContext = sourcePath.filename().string();
		}
		else
		{
			safeContext = "BackupContext"; // Generic fallback if source name also unavailable
		}
	}
	// Remove characters invalid for Windows/Unix filenames
	safeContext = std::regex_replace(safeContext, std::regex(R"([<>:"/\\|?*])"), "_");
	safeContext = std::regex_replace(safeContext, std::regex(R"(^\.+$)"), "_");		   // Avoid names like "." or ".."
	safeContext = std::regex_replace(safeContext, std::regex(R"(^[. ]+|[. ]+$)"), ""); // Trim leading/trailing dots/spaces
	if (safeContext.length() > 50)
	{ // Limit length to prevent overly long paths
		safeContext = safeContext.substr(0, 50);
	}
	if (safeContext.empty())
	{ // Final fallback if sanitization results in an empty string
		safeContext = "Backup";
	}

	std::string backupFolderName = "RenameBackup_" + safeContext + "_" + timestamp;
	result.backupPath = backupParentDir / backupFolderName;

	// Perform the backup by copying the directory contents
	try
	{
		std::error_code srcEc;
		// Validate the source path before attempting to copy
		if (!fs::exists(sourcePath, srcEc) || srcEc || !fs::is_directory(sourcePath, srcEc) || srcEc)
		{
			throw std::runtime_error("Backup source path is invalid or not a directory: '" + sourcePath.string() + "'" + (srcEc ? " (" + srcEc.message() + ")" : ""));
		}

		// Ensure the parent backup directory (e.g., Documents/RenameUtilityBackups) exists
		std::error_code parentEc;
		if (!fs::exists(backupParentDir, parentEc) || parentEc)
		{
			if (!fs::create_directories(backupParentDir, parentEc) || parentEc)
			{
				throw std::runtime_error("Failed to create parent backup directory '" + backupParentDir.string() + "'" + (parentEc ? " (" + parentEc.message() + ")" : ""));
			}
		}
		else if (!fs::is_directory(backupParentDir, parentEc) || parentEc)
		{
			throw std::runtime_error("Parent backup path exists but is not a directory '" + backupParentDir.string() + "'" + (parentEc ? " (" + parentEc.message() + ")" : ""));
		}

		// Check if the specific backup destination already exists (highly unlikely due to timestamp, but good practice)
		std::error_code destEc;
		if (fs::exists(result.backupPath, destEc) || destEc)
		{
			throw std::runtime_error("Backup destination path already exists (collision?): '" + result.backupPath.string() + "'" + (destEc ? " (" + destEc.message() + ")" : ""));
		}

		// Perform the recursive copy operation
		CopyDirectory(sourcePath, result.backupPath);

		// If CopyDirectory didn't throw an exception, the backup is considered successful
		result.success = true;
	}
	catch (const std::exception &ex)
	{
		result.errorMessage = "Backup failed: " + std::string(ex.what());
		result.success = false;

		// Attempt to clean up any partially created backup directory to avoid leaving incomplete backups
		if (!result.backupPath.empty())
		{
			std::error_code checkEc, removeEc;
			if (fs::exists(result.backupPath, checkEc) && !checkEc)
			{
				try
				{
					fs::remove_all(result.backupPath, removeEc);
					if (removeEc)
					{
						// Append cleanup error message if cleanup itself fails
						result.errorMessage += " | Additionally, failed to cleanup partially created backup directory: " + removeEc.message();
					}
				}
				catch (const std::exception &cleanupEx)
				{
					result.errorMessage += " | Additionally, exception during backup cleanup: " + std::string(cleanupEx.what());
				}
			}
			else if (checkEc)
			{
				result.errorMessage += " | Additionally, failed to check existence for cleanup: " + checkEc.message();
			}
		}
	}
	catch (...)
	{
		result.errorMessage = "Backup failed due to an unknown error.";
		result.success = false;
		// Attempt cleanup for unknown errors as well
		if (!result.backupPath.empty())
		{
			std::error_code checkEc, removeEc;
			if (fs::exists(result.backupPath, checkEc) && !checkEc)
			{
				try
				{
					fs::remove_all(result.backupPath, removeEc);
				}
				catch (...)
				{
				} // Ignore cleanup errors in this catch-all block
			}
		}
	}

	return result;
}

// Deletes a specified backup directory
DeleteResult RenamerLogic::deleteBackup(const fs::path &backupPath)
{
	DeleteResult result;
	result.success = false;

	// Basic validation of the backup path to prevent accidental deletion of unintended paths
	if (backupPath.empty() || !backupPath.has_filename() || backupPath.filename() == "." || backupPath.filename() == "..")
	{
		result.errorMessage = "Invalid backup path provided for deletion: '" + backupPath.string() + "'";
		return result;
	}

	// Check if the path exists and is a directory before attempting deletion
	std::error_code existEc, typeEc;
	bool bExists = fs::exists(backupPath, existEc);
	if (existEc)
	{
		result.errorMessage = "Error checking backup existence '" + backupPath.string() + "': " + existEc.message();
		return result;
	}
	if (!bExists)
	{
		// If the backup path doesn't exist, consider the deletion successful (idempotent operation)
		result.errorMessage = "Backup path not found (already deleted?): '" + backupPath.string() + "'.";
		result.success = true; // No action needed, so technically a success
		return result;
	}
	if (!fs::is_directory(backupPath, typeEc) || typeEc)
	{
		result.errorMessage = "Path to delete is not a directory: '" + backupPath.string() + "'.";
		if (typeEc)
			result.errorMessage += " (Filesystem Error: " + typeEc.message() + ")";
		return result;
	}

	// Attempt to remove the directory and its contents recursively
	try
	{
		std::error_code removeEc;
		fs::remove_all(backupPath, removeEc);

		if (removeEc)
		{
			result.errorMessage = "Error deleting backup directory '" + backupPath.string() + "': " + removeEc.message();
			result.success = false;
		}
		else
		{
			// Verify deletion by checking if the path still exists
			std::error_code verifyEc;
			if (fs::exists(backupPath, verifyEc) && !verifyEc)
			{
				// Should not exist after a successful remove_all call
				result.errorMessage = "Verification failed: Directory still exists after reported successful deletion: '" + backupPath.string() + "'.";
				result.success = false;
			}
			else if (verifyEc)
			{
				// An error occurred during the verification check itself
				result.errorMessage = "Error verifying backup deletion for '" + backupPath.string() + "': " + verifyEc.message();
				result.success = false; // Assume deletion failed if verification is problematic
			}
			else
			{
				// Path does not exist and verification check had no errors: Success
				result.success = true;
			}
		}
	}
	catch (const fs::filesystem_error &ex)
	{
		// Catch specific filesystem errors for more detailed reporting
		result.errorMessage = "Filesystem exception during backup deletion '" + backupPath.string() + "': " + std::string(ex.what());
		if (!ex.path1().empty())
			result.errorMessage += " (Path1: " + ex.path1().string() + ")";
		if (!ex.path2().empty())
			result.errorMessage += " (Path2: " + ex.path2().string() + ")";
		result.errorMessage += " (Code: " + ex.code().message() + ")";
		result.success = false;
	}
	catch (const std::exception &ex)
	{
		result.errorMessage = "General exception during backup deletion '" + backupPath.string() + "': " + std::string(ex.what());
		result.success = false;
	}
	catch (...)
	{
		result.errorMessage = "Unknown exception during backup deletion for '" + backupPath.string() + "'.";
		result.success = false;
	}

	return result;
}