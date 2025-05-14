#include "RenamerLogic.h"

#include <wx/log.h>     // For wxLogWarning, if needed
#include <wx/tokenzr.h> // For splitting comma-separated extension string

#include <filesystem>
#include <string>
#include <vector>
#include <regex>
#include <map>
#include <set>
#include <optional>
#include <algorithm>    // For std::sort, std::max, std::abs
#include <system_error> // For std::error_code
#include <stdexcept>    // For std::exception
#include <cmath>        // For std::floor, std::log10
#include <limits>       // For std::numeric_limits
#include <type_traits>  // for std::is_same_v (used in a commented-out section)

namespace fs = std::filesystem;

// Calculates the rename plan based on input parameters, performing file scanning and validation
OutputResults RenamerLogic::calculateRenamePlan(const InputParams &params)
{
    OutputResults results;
    results.success = true;           // Assume success initially, set to false on fatal errors
    bool dirScanFilesChecked = false; // Tracks if any files were encountered during directory scan

    // Basic validation: a naming pattern is always required
    if (params.namingPattern.empty())
    {
        results.errorLog.push_back("FATAL: New name pattern cannot be empty.");
        results.success = false;
        return results;
    }

    if (params.mode == RenamingMode::DirectoryScan)
    {
        // Directory Scan specific validations
        std::error_code ec;
        if (!fs::exists(params.targetDirectory, ec) || ec || !fs::is_directory(params.targetDirectory, ec) || ec)
        {
            results.errorLog.push_back("FATAL: Target directory is invalid or inaccessible: " + params.targetDirectory.string() + (ec ? " (" + ec.message() + ")" : ""));
            results.success = false;
            return results;
        }
        if (params.filenamePattern.empty())
        {
            results.errorLog.push_back("FATAL: Filename Pattern cannot be empty in Directory Scan mode.");
            results.success = false;
            return results;
        }
        // Number filter range must be valid (lowest <= highest, unless both are 0 for no filter)
        if (params.lowestNumber > params.highestNumber && (params.lowestNumber != 0 || params.highestNumber != 0))
        {
            results.errorLog.push_back("FATAL: Lowest Number filter cannot be greater than Highest Number filter.");
            results.success = false;
            return results;
        }

        // Prepare filename pattern regex for matching files
        std::regex findRegex;
        try
        {
            std::string regexString = ConvertWildcardToRegex(params.filenamePattern); // Convert wildcard to regex
            findRegex.assign(regexString, std::regex::icase | std::regex::optimize);  // Case-insensitive matching
        }
        catch (const std::regex_error &e)
        {
            results.errorLog.push_back("FATAL: Invalid Filename Pattern (regex error): " + std::string(e.what()));
            results.success = false;
            return results;
        }

        // Prepare extension filter set if provided
        std::set<std::string> extensionFilterSet;
        bool useExtFilter = !params.filterExtensions.empty();
        if (useExtFilter)
        {
            wxStringTokenizer tokenizer(params.filterExtensions, ","); // Split comma-separated extensions
            while (tokenizer.HasMoreTokens())
            {
                std::string ext = ToLower(tokenizer.GetNextToken().Trim().ToStdString()); // Normalize to lowercase
                if (!ext.empty())
                {
                    if (ext[0] != '.')
                    {
                        ext = "." + ext; // Ensure leading dot for consistent matching
                    }
                    extensionFilterSet.insert(ext);
                }
            }
            if (extensionFilterSet.empty())
            {
                useExtFilter = false; // No valid extensions were parsed from the input string
            }
            else
            {
                results.generalInfoLog.push_back("Filtering by extensions: " + params.filterExtensions);
            }
        }

        // Determine number width for formatting <num> and <orig_num> placeholders
        // This aims to provide consistent zero-padding based on the range of numbers involved
        int numberWidth = 1; // Default minimum width
        bool useNumFilter = (params.lowestNumber != 0 || params.highestNumber != 0);
        if (useNumFilter)
        {
            // Consider the absolute magnitude of filter bounds and potential values after increment/decrement
            long long maxAbsVal = std::max(std::abs((long long)params.highestNumber), std::abs((long long)params.lowestNumber));
            long long potentialMaxAfterInc = (long long)params.highestNumber + std::abs((long long)params.increment);
            long long potentialMinAfterInc = (long long)params.lowestNumber - std::abs((long long)params.increment); // consider negative increment
            maxAbsVal = std::max({maxAbsVal, std::abs(potentialMaxAfterInc), std::abs(potentialMinAfterInc)});

            if (maxAbsVal > 0)
            {
                numberWidth = (int)std::floor(std::log10(maxAbsVal)) + 1;
            }
            numberWidth = std::max(2, numberWidth); // Ensure a minimum width of 2 for typical numbering (e.g., 01, 02)
            numberWidth = std::min(9, numberWidth); // Cap at a sensible maximum width
        }
        else
        {
            numberWidth = 2; // Default width if no number filter active but <num>/<orig_num> might be used
        }

        // Scan files in the target directory (recursively or not)
        std::map<fs::path, std::optional<int>> foundFilesMap; // Stores {file path -> original number (if any)}
        std::set<fs::path> foundFilesSet;                     // Stores unique full paths of found files for later overwrite checks
        try
        {
            auto scanOptions = fs::directory_options::skip_permission_denied; // Skip inaccessible directories/files
            auto processEntry = [&](const fs::directory_entry &entry)         // Lambda to process each directory entry
            {
                dirScanFilesChecked = true; // Mark that at least one file/directory was encountered
                const fs::path &currentPath = entry.path();
                std::error_code fileEc;

                if (fs::is_regular_file(currentPath, fileEc) && !fileEc) // Process only regular files
                {
                    std::string filename = currentPath.filename().string();
                    std::string extension = ToLower(currentPath.extension().string()); // For case-insensitive extension filter

                    // 1. Match filename against the wildcard pattern (converted to regex)
                    if (std::regex_match(filename, findRegex))
                    {
                        // 2. Match extension filter (if active)
                        if (!useExtFilter || extensionFilterSet.count(extension))
                        {
                            // 3. Parse number from filename if needed for filtering or placeholders
                            std::optional<int> originalNum = std::nullopt;
                            bool needsNumParsing = useNumFilter ||
                                                   (params.namingPattern.find("<num>") != std::string::npos) ||
                                                   (params.namingPattern.find("<orig_num>") != std::string::npos);
                            if (needsNumParsing)
                            {
                                originalNum = RenamerLogic::ParseLastNumber(filename);
                            }

                            // 4. Match number filter (if active)
                            bool numberRangeOk = true;
                            if (useNumFilter)
                            {
                                if (!originalNum.has_value() || originalNum.value() < params.lowestNumber || originalNum.value() > params.highestNumber)
                                {
                                    numberRangeOk = false; // Number is outside the specified filter range
                                }
                            }

                            // If all filters pass, add the file to the map for processing
                            if (numberRangeOk)
                            {
                                foundFilesMap[currentPath] = originalNum;
                                foundFilesSet.insert(currentPath);
                            }
                        }
                    }
                }
                else if (fileEc)
                {
                    // Log error checking file type but continue scanning other files
                    results.warningLog.push_back("Warning: Filesystem error checking type of '" + currentPath.string() + "': " + fileEc.message());
                }
            };

            if (params.recursiveScan)
            {
                results.generalInfoLog.push_back("Starting recursive directory scan...");
                for (const auto &entry : fs::recursive_directory_iterator(params.targetDirectory, scanOptions))
                {
                    try
                    {
                        processEntry(entry);
                    }
                    catch (const fs::filesystem_error &fs_err)
                    {
                        // Log error during iteration but attempt to continue
                        results.warningLog.push_back("Warning: Filesystem error during recursive scan near '" + fs_err.path1().string() + "': " + fs_err.what());
                        // The following was a commented-out attempt to disable recursion on error, not applicable here as entry is const
                        // if constexpr (std::is_same_v<decltype(entry), const fs::directory_entry&>) { }
                    }
                    catch (const std::exception &e)
                    {
                        results.warningLog.push_back("Warning: Exception during recursive scan: " + std::string(e.what()));
                    }
                }
            }
            else
            {
                results.generalInfoLog.push_back("Starting non-recursive directory scan...");
                for (const auto &entry : fs::directory_iterator(params.targetDirectory, scanOptions))
                {
                    try
                    {
                        processEntry(entry);
                    }
                    catch (const fs::filesystem_error &fs_err)
                    {
                        results.warningLog.push_back("Warning: Filesystem error during scan near '" + fs_err.path1().string() + "': " + fs_err.what());
                    }
                    catch (const std::exception &e)
                    {
                        results.warningLog.push_back("Warning: Exception during scan: " + std::string(e.what()));
                    }
                }
            }
        }
        catch (const fs::filesystem_error &e)
        {
            results.errorLog.push_back("FATAL: Filesystem error starting directory scan at '" + e.path1().string() + "': " + e.what());
            results.success = false;
            return results;
        }
        catch (const std::exception &e)
        {
            results.errorLog.push_back("FATAL: Unexpected error during directory scan: " + std::string(e.what()));
            results.success = false;
            return results;
        }

        // Generate the rename plan from the files found and filtered
        std::vector<RenameOperation> tempPlan;
        std::set<std::string> targetPathsLowercase; // For case-insensitive detection of target conflicts within this batch

        for (const auto &pair : foundFilesMap)
        { // Iterate over {path, original_number}
            const fs::path &currentPath = pair.first;
            const std::optional<int> &originalNumOpt = pair.second;
            std::string originalFilename = currentPath.filename().string();
            std::string originalStem = currentPath.stem().string();
            std::string originalExtension = currentPath.extension().string(); // Preserve original case for placeholders

            // Calculate new number if applicable (original number + increment)
            std::optional<int> newNumOpt = std::nullopt;
            if (originalNumOpt.has_value())
            {
                long long newNumLL = (long long)originalNumOpt.value() + params.increment; // Use long long to detect overflow
                if (newNumLL >= std::numeric_limits<int>::min() && newNumLL <= std::numeric_limits<int>::max())
                {
                    newNumOpt = static_cast<int>(newNumLL);
                }
                else
                {
                    results.missingSourceFilesLog.push_back(originalFilename + " (in " + currentPath.parent_path().string() + ") (Skipped: Incremented number out of int range)");
                    results.success = false; // Mark as error if number overflows, as it's an invalid operation
                    continue;                // Skip this file
                }
            }

            // Generate new filename using placeholders, find/replace, and case conversion
            std::string nameAfterPlaceholders = RenamerLogic::ReplacePlaceholders(params.namingPattern, params.mode, 0, 0,
                                                                                  originalFilename, originalStem, originalExtension,
                                                                                  originalNumOpt, newNumOpt, numberWidth);
            std::string nameAfterFindReplace = RenamerLogic::PerformFindReplace(nameAfterPlaceholders, params.findText, params.replaceText, params.findCaseSensitive);
            std::string finalNewFilename = RenamerLogic::ApplyCaseConversion(nameAfterFindReplace, params.caseConversionMode);

            if (finalNewFilename.empty())
            {
                results.errorLog.push_back("Error: Generated new filename is empty for '" + originalFilename + "'. Skipped.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Generated name was empty)");
                results.success = false; // An empty filename is an error
                continue;
            }

            fs::path newFullPath = currentPath.parent_path() / finalNewFilename;

            // Check if the rename is redundant (new name is same as old, case-insensitively)
            if (RenamerLogic::iequals(currentPath.string(), newFullPath.string()))
            {
                results.generalInfoLog.push_back("Skipping '" + originalFilename + "' (New name is identical to old name, case-insensitively)");
                continue;
            }

            // Check for target path conflicts within this batch (case-insensitive)
            // This prevents renaming two different source files to the same target name in this operation
            std::string newPathLower = ToLower(newFullPath.string());
            if (!targetPathsLowercase.insert(newPathLower).second)
            { // .second is false if element already existed
                results.errorLog.push_back("Error: Generated new path '" + newFullPath.string() + "' conflicts with another generated path in this batch. Skipping '" + originalFilename + "'.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Target path conflict within batch)");
                results.success = false; // Conflict within batch is an error preventing a clean rename
                continue;
            }

            // Check if target path already exists on disk AND is not one of the source files being renamed in this batch
            std::error_code targetEc;
            bool targetExists = fs::exists(newFullPath, targetEc);
            if (targetEc)
            {
                results.warningLog.push_back("Warning: Filesystem error checking target path '" + newFullPath.string() + "': " + targetEc.message() + ". Skipping '" + originalFilename + "'.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Error checking target path)");
                continue;
            }
            if (targetExists && foundFilesSet.count(newFullPath) == 0)
            { // Target exists and is NOT an original file in our scan
                results.potentialOverwritesLog.push_back({originalFilename, finalNewFilename, newFullPath});
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Target path '" + newFullPath.string() + "' already exists and is not part of this rename batch)");
                continue; // Skip this operation to prevent overwriting an unrelated file
            }

            // All checks passed, add to the plan
            tempPlan.push_back({originalFilename, finalNewFilename, currentPath, newFullPath, originalNumOpt, 0}); // Index is 0 for DirScan mode
        }
        results.renamePlan = std::move(tempPlan);
    }
    else
    { // ManualSelection Mode
        if (params.manualFiles.empty())
        {
            results.errorLog.push_back("FATAL: No files were added to the list in Manual Selection mode.");
            results.success = false;
            return results;
        }

        int currentIndex = 1; // 1-based index for manual list display and <index> placeholder
        int totalFiles = params.manualFiles.size();
        std::set<std::string> targetPathsLowercase; // For case-insensitive conflict detection within this batch
        std::set<fs::path> uniqueInputPaths;        // To detect duplicate input files and for overwrite checks
        std::vector<RenameOperation> tempPlan;

        for (const auto &filePath : params.manualFiles)
        {
            fs::path currentPath = filePath; // Work with a copy

            // Check for duplicate input files in the manual list itself
            if (!uniqueInputPaths.insert(currentPath).second)
            {
                results.warningLog.push_back("Warning: Skipping duplicate input file: " + currentPath.string());
                currentIndex++; // Still increment index as it represents position in the original user list
                continue;
            }

            // Verify the file exists and is a regular file right before processing
            // This is important as file might have been moved/deleted since being added to list
            std::error_code ec;
            if (!fs::exists(currentPath, ec) || ec || !fs::is_regular_file(currentPath, ec) || ec)
            {
                results.missingSourceFilesLog.push_back(currentPath.string() + " (Skipped: Not a valid file or inaccessible" + (ec ? ". Error: " + ec.message() : "") + ")");
                currentIndex++;
                continue; // Skip this file
            }

            std::string originalFilename = currentPath.filename().string();
            std::string originalStem = currentPath.stem().string();
            std::string originalExtension = currentPath.extension().string();

            // Generate new name using placeholders, find/replace, and case conversion
            std::string nameAfterPlaceholders = RenamerLogic::ReplacePlaceholders(params.namingPattern, params.mode, currentIndex, totalFiles,
                                                                                  originalFilename, originalStem, originalExtension,
                                                                                  std::nullopt, std::nullopt, 0); // No numeric placeholders (<num>, <orig_num>) in manual mode
            std::string nameAfterFindReplace = RenamerLogic::PerformFindReplace(nameAfterPlaceholders, params.findText, params.replaceText, params.findCaseSensitive);
            std::string finalNewFilename = RenamerLogic::ApplyCaseConversion(nameAfterFindReplace, params.caseConversionMode);

            if (finalNewFilename.empty())
            {
                results.errorLog.push_back("Error: Generated new filename is empty for '" + originalFilename + "'. Skipped.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Generated name was empty)");
                results.success = false;
                currentIndex++;
                continue;
            }

            fs::path newFullPath = currentPath.parent_path() / finalNewFilename;

            // Check for redundant rename (case-insensitive)
            if (RenamerLogic::iequals(currentPath.string(), newFullPath.string()))
            {
                results.generalInfoLog.push_back("Skipping '" + originalFilename + "' (New name is identical to old name, case-insensitively)");
                currentIndex++;
                continue;
            }

            // Check for target path conflicts within this batch (case-insensitive)
            std::string newPathLower = ToLower(newFullPath.string());
            if (!targetPathsLowercase.insert(newPathLower).second)
            {
                results.errorLog.push_back("Error: Generated new path '" + newFullPath.string() + "' conflicts with another generated path in this batch. Skipping '" + originalFilename + "'.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Target path conflict within batch)");
                results.success = false;
                currentIndex++;
                continue;
            }

            // Check if target path already exists on disk AND is not one of the other *input* files in this manual list
            std::error_code targetEc;
            bool targetExists = fs::exists(newFullPath, targetEc);
            if (targetEc)
            {
                results.warningLog.push_back("Warning: Filesystem error checking target path '" + newFullPath.string() + "': " + targetEc.message() + ". Skipping '" + originalFilename + "'.");
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Error checking target path)");
                currentIndex++;
                continue;
            }
            if (targetExists && uniqueInputPaths.count(newFullPath) == 0)
            { // Target exists and is NOT one of the other files in our manual list
                results.potentialOverwritesLog.push_back({originalFilename, finalNewFilename, newFullPath});
                results.missingSourceFilesLog.push_back(originalFilename + " (Skipped: Target path '" + newFullPath.string() + "' already exists and is not part of this rename batch)");
                currentIndex++;
                continue; // Skip this operation
            }

            // All checks passed
            tempPlan.push_back({originalFilename, finalNewFilename, currentPath, newFullPath, std::nullopt, currentIndex}); // Store 1-based index
            currentIndex++;
        }
        results.renamePlan = std::move(tempPlan);
    }

    // Final success state depends on no new errors being logged during this plan generation
    // It preserves any 'false' state from initial fatal errors
    results.success = results.success && results.errorLog.empty();

    // Add a summary log message about the outcome of the planning phase
    if (results.renamePlan.empty())
    {
        bool issuesLogged = !results.missingSourceFilesLog.empty() || !results.potentialOverwritesLog.empty() || !results.warningLog.empty() || !results.errorLog.empty();
        // If no files found in DirScan and no other issues, it's likely just an empty matching set
        if (params.mode == RenamingMode::DirectoryScan && !issuesLogged && !dirScanFilesChecked) // MODIFIED: Removed && foundFilesMap.empty()
        {
            results.generalInfoLog.push_back("No files found in the target directory matching the specified pattern/filters.");
        }
        // If manual list was empty (should be caught earlier, but for completeness)
        else if (params.mode == RenamingMode::ManualSelection && params.manualFiles.empty())
        {
            results.generalInfoLog.push_back("No files were added to the list to be renamed.");
        }
        // Otherwise, some files were found/added but none made it into the final plan
        else
        {
            results.generalInfoLog.push_back("No files eligible for renaming after applying all filters and checks.");
        }
    }
    else
    {
        results.generalInfoLog.push_back("Calculated " + std::to_string(results.renamePlan.size()) + " file(s) to be renamed.");
    }
    return results;
}