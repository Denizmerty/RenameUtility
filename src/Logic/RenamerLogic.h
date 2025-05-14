#ifndef RENAMERLOGIC_H
#define RENAMERLOGIC_H

#include <vector>
#include <string>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <functional>
#include <chrono>
#include <sstream>
#include <cmath>
#include <locale>
#include <optional>

#include <wx/string.h>
#include <wx/stdpaths.h>

namespace fs = std::filesystem;

enum class CaseConversionMode
{
	NoChange,
	ToUpper,
	ToLower
};

enum class RenamingMode
{
	DirectoryScan,
	ManualSelection
};

struct RenameOperation
{
	std::string OldName;
	std::string NewName;
	fs::path OldFullPath;
	fs::path NewFullPath;
	std::optional<int> Number;
	int Index;
};

struct PotentialOverwrite
{
	std::string SourceFile;
	std::string TargetFile;
	fs::path TargetPath;
};

struct InputParams
{
	RenamingMode mode;
	fs::path targetDirectory;
	std::string namingPattern;
	std::string findText;
	std::string replaceText;
	bool findCaseSensitive;
	CaseConversionMode caseConversionMode;
	int increment;
	std::string filenamePattern;
	std::string filterExtensions;
	int highestNumber;
	int lowestNumber;
	bool recursiveScan;
	std::vector<fs::path> manualFiles;
};

struct OutputResults
{
	std::vector<RenameOperation> renamePlan;
	std::vector<std::string> missingSourceFilesLog;
	std::vector<PotentialOverwrite> potentialOverwritesLog;
	std::vector<std::string> generalInfoLog;
	std::vector<std::string> warningLog;
	std::vector<std::string> errorLog;
	bool success = false;
};

struct RenameExecutionResult
{
	std::vector<RenameOperation> successfulRenameOps;
	std::vector<std::pair<std::string, std::string>> failedRenames;
	bool overallSuccess = false;
};

struct UndoResult
{
	std::vector<std::pair<std::string, std::string>> successfulUndos;
	std::vector<std::pair<std::string, std::string>> failedUndos;
	bool overallSuccess = false;
};

struct BackupResult
{
	fs::path backupPath;
	bool success = false;
	std::string errorMessage;
};

struct DeleteResult
{
	bool success = false;
	std::string errorMessage;
};

std::string ToLower(std::string s);

class RenamerLogic
{
private:
	static void CopyDirectory(const fs::path &source, const fs::path &destination);
	static fs::path GetDefaultBackupParentPathInternal();

public:
	static std::string EscapeRegexChars(const std::string &input);
	static std::string ConvertWildcardToRegex(const std::string &pattern);
	static std::string FormatNumber(int number, int width);
	static bool iequals(const std::string &a, const std::string &b);
	static std::string ReplacePlaceholders(
		const std::string &pattern, const RenamingMode mode, int index, int totalManualFiles,
		const std::string &originalFullName, const std::string &originalNameStem, const std::string &originalExtension,
		const std::optional<int> &dirScanOriginalNum, const std::optional<int> &dirScanNewNum, int dirScanNumberWidth);
	static std::string PerformFindReplace(std::string subject, const std::string &find, const std::string &replace, bool caseSensitive);
	static std::string ApplyCaseConversion(std::string filename, CaseConversionMode mode);
	static std::optional<int> ParseLastNumber(const std::string &filename);

	static OutputResults calculateRenamePlan(const InputParams &params);
	static RenameExecutionResult performRename(const std::vector<RenameOperation> &plan, int increment);
	static UndoResult performUndo(std::vector<RenameOperation> opsToUndo);
	static BackupResult performBackup(const fs::path &sourcePath, const std::string &contextName);
	static DeleteResult deleteBackup(const fs::path &backupPath);

	static const fs::path DefaultPath;
};

#endif // RENAMERLOGIC_H