#include "RenamerLogic.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// Ensures 'input' is treated as a literal string within a regex pattern by
// escaping metacharacters
std::string RenamerLogic::EscapeRegexChars(const std::string &input) {
  static const std::regex regex_escape_chars(R"([.^$|()\[\]{}+*?\\])");
  return std::regex_replace(input, regex_escape_chars, R"(\$&)");
}

// Converts a filename wildcard pattern (using '*' and '?') into its equivalent
// regular expression
std::string RenamerLogic::ConvertWildcardToRegex(const std::string &pattern) {
  if (pattern.empty()) {
    return "^.*$"; // An empty pattern implies matching any string
  }
  std::string regex_pattern;
  regex_pattern.reserve(pattern.length() * 2); // Optimize for typical expansion
  regex_pattern += '^';                        // Anchor to the start
  for (char c : pattern) {
    switch (c) {
    case '*':
      regex_pattern += ".*";
      break;
    case '?':
      regex_pattern += '.';
      break;
    // Escape regex metacharacters to treat them literally
    case '.':
    case '^':
    case '$':
    case '|':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '+':
    case '\\':
      regex_pattern += '\\';
      regex_pattern += c;
      break;
    default:
      regex_pattern += c;
      break;
    }
  }
  regex_pattern += '$'; // Anchor to the end
  return regex_pattern;
}

// Extracts the last integer found in 'filename', useful for filtering or
// sequence manipulation
std::optional<int> RenamerLogic::ParseLastNumber(const std::string &filename) {
  // Regex to capture the final numeric sequence
  static const std::regex last_number_regex(R"(.*?(\d+)[^\d]*$)");
  std::smatch match;
  if (std::regex_match(filename, match, last_number_regex)) {
    if (match.size() == 2) // Ensures the number group was captured
    {
      try {
        // Use long long for intermediate conversion to detect overflow against
        // int limits
        long long num_ll = std::stoll(match[1].str());
        if (num_ll >= std::numeric_limits<int>::min() &&
            num_ll <= std::numeric_limits<int>::max()) {
          return static_cast<int>(num_ll);
        }
      } catch (const std::exception &) {
        // Handles stoll conversion errors (e.g., out_of_range)
      }
    }
  }
  return std::nullopt;
}

// Formats 'number' with leading zeros to match a specified 'width'
std::string RenamerLogic::FormatNumber(int number, int width) {
  // Negative numbers typically aren't zero-padded in filename contexts
  if (number < 0) {
    return std::to_string(number);
  }
  if (width < 1) // Ensure a sensible minimum width
  {
    width = 1;
  }
  std::stringstream ss;
  ss << std::setw(width) << std::setfill('0') << number;
  return ss.str();
}

// Case-insensitive string comparison
bool RenamerLogic::iequals(const std::string &a, const std::string &b) {
  if (a.length() != b.length()) {
    return false;
  }
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](char char_a, char char_b) {
                      return std::tolower(static_cast<unsigned char>(char_a)) ==
                             std::tolower(static_cast<unsigned char>(char_b));
                    });
}

// Converts string to lowercase
std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

namespace // Anonymous namespace for internal linkage helper functions
{
// Replaces characters invalid in filenames with an underscore
inline char sanitise_char(unsigned char c) noexcept {
  // Defines typical invalid filename characters and control characters
  constexpr std::string_view bad = R"(\/:*?"<>|)";
  return (c <= 31 || bad.find(c) != std::string_view::npos) ? '_' : c;
}

// Sanitizes a filename stem, ensuring it's valid for filesystem use
std::string sanitise_stem(std::string_view stem_sv) {
  std::string out;
  if (stem_sv.empty()) {
    return "_"; // Provide a default for empty stems
  }
  out.reserve(stem_sv.size());
  for (unsigned char c : stem_sv) {
    out.push_back(sanitise_char(c));
  }
  // Ensure the stem doesn't become empty or a reserved name (e.g., ".")
  if (out.empty() || out == "." || out == "..") {
    out = "_";
  }
  return out;
}
} // namespace

// Replaces all occurrences of 'find' with 'replace' in 'subject', respecting
// 'caseSensitive' flag. If useRegex is true, treats 'find' as a regex pattern.
std::string RenamerLogic::PerformFindReplace(std::string subject,
                                             const std::string &find,
                                             const std::string &replace,
                                             bool caseSensitive,
                                             bool useRegex) {
  if (find.empty() || subject.empty()) {
    return subject; // No change if find or subject is empty
  }

  // Use regex if requested
  if (useRegex) {
    try {
      std::regex::flag_type flags = std::regex::ECMAScript;
      if (!caseSensitive) {
        flags |= std::regex::icase;
      }
      std::regex pattern(find, flags);
      return std::regex_replace(subject, pattern, replace);
    } catch (const std::regex_error &) {
      // Invalid regex, return original
      return subject;
    }
  }

  size_t pos = 0;
  while (pos < subject.length()) {
    size_t found_pos;
    if (caseSensitive) {
      found_pos = subject.find(find, pos);
    } else {
      auto it = std::search(subject.begin() + pos, subject.end(), find.begin(),
                            find.end(), [](unsigned char c1, unsigned char c2) {
                              return std::tolower(c1) == std::tolower(c2);
                            });
      found_pos = (it == subject.end())
                      ? std::string::npos
                      : static_cast<size_t>(it - subject.begin());
    }

    if (found_pos == std::string::npos) {
      break;
    }

    subject.replace(found_pos, find.length(), replace);
    // Advance position past the replacement to prevent infinite loops with
    // overlapping patterns
    pos = found_pos + replace.length();
  }
  return subject;
}

// Constructs new filenames by substituting placeholders in 'pattern' with
// runtime values
std::string RenamerLogic::ReplacePlaceholders(
    const std::string &pattern, RenamingMode mode, int index,
    int totalManualFiles, const std::string &originalFullName,
    const std::string &originalNameStem, const std::string &originalExtension,
    const std::optional<int> &dirScanOriginalNum,
    const std::optional<int> &dirScanNewNum, int dirScanNumberWidth,
    const std::string &parentDirName, const fs::path &fullFilePath) {
  std::string result = pattern;
  size_t pos;

  // Replace <parent_dir> placeholder (available in both modes)
  pos = result.find("<parent_dir>");
  while (pos != std::string::npos) {
    result.replace(pos, 12, parentDirName);
    pos = result.find("<parent_dir>", pos + parentDirName.length());
  }

  // File-based placeholders (require valid file path)
  if (!fullFilePath.empty() && fs::exists(fullFilePath)) {
    std::error_code ec;

    // <file_size> - file size in bytes
    pos = result.find("<file_size>");
    if (pos != std::string::npos) {
      auto fileSize = fs::file_size(fullFilePath, ec);
      std::string sizeStr = ec ? "0" : std::to_string(fileSize);
      while (pos != std::string::npos) {
        result.replace(pos, 11, sizeStr);
        pos = result.find("<file_size>", pos + sizeStr.length());
      }
    }

    // <file_size_kb> - file size in KB
    pos = result.find("<file_size_kb>");
    if (pos != std::string::npos) {
      auto fileSize = fs::file_size(fullFilePath, ec);
      std::string sizeStr = ec ? "0" : std::to_string(fileSize / 1024);
      while (pos != std::string::npos) {
        result.replace(pos, 14, sizeStr);
        pos = result.find("<file_size_kb>", pos + sizeStr.length());
      }
    }

    // <modified_date> - file modification date (YYYYMMDD format)
    pos = result.find("<modified_date>");
    if (pos != std::string::npos) {
      auto lastWrite = fs::last_write_time(fullFilePath, ec);
      std::string dateStr = "00000000";
      if (!ec) {
        auto sctp =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lastWrite - fs::file_time_type::clock::now() +
                std::chrono::system_clock::now());
        auto time_c = std::chrono::system_clock::to_time_t(sctp);
        std::tm time_tm = {};
#ifdef _WIN32
        localtime_s(&time_tm, &time_c);
#else
        localtime_r(&time_c, &time_tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&time_tm, "%Y%m%d");
        dateStr = oss.str();
      }
      while (pos != std::string::npos) {
        result.replace(pos, 15, dateStr);
        pos = result.find("<modified_date>", pos + dateStr.length());
      }
    }
  }

  // Replace <random:N> placeholder - generates N random alphanumeric characters
  static const std::string alphanumChars =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static std::mt19937 rng(static_cast<unsigned int>(
      std::chrono::system_clock::now().time_since_epoch().count()));
  std::regex randomRegex(R"(<random:(\d+)>)");
  std::smatch match;
  while (std::regex_search(result, match, randomRegex)) {
    int numChars = std::stoi(match[1].str());
    numChars = std::min(numChars, 64); // Cap at 64 characters
    std::string randomStr;
    randomStr.reserve(numChars);
    std::uniform_int_distribution<size_t> dist(0, alphanumChars.size() - 1);
    for (int i = 0; i < numChars; ++i) {
      randomStr += alphanumChars[dist(rng)];
    }
    result = match.prefix().str() + randomStr + match.suffix().str();
  }

  // Efficiently check for and replace date/time placeholders if any are present
  if (result.find("<YYYY>") != std::string::npos ||
      result.find("<MM>") !=
          std::string::npos || /* ... other time placeholders ... */
      result.find("<DD>") != std::string::npos ||
      result.find("<hh>") != std::string::npos ||
      result.find("<mm>") != std::string::npos ||
      result.find("<ss>") != std::string::npos) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = {};
#ifdef _WIN32
    localtime_s(&now_tm, &now_c); // Thread-safe time conversion
#else
    localtime_r(&now_c, &now_tm); // Thread-safe time conversion
#endif
    std::stringstream ss;
    std::string tempStr;
    auto replaceTimePlaceholder = // Lambda to centralize replacement logic
        [&](const std::string &placeholder, const char *format) {
          size_t current_pos = result.find(placeholder);
          while (current_pos != std::string::npos) {
            ss.str("");
            ss.clear();
            ss << std::put_time(&now_tm, format);
            tempStr = ss.fail() ? "" : ss.str();
            result.replace(current_pos, placeholder.length(), tempStr);
            current_pos =
                result.find(placeholder, current_pos + tempStr.length());
          }
        };
    replaceTimePlaceholder("<YYYY>", "%Y");
    replaceTimePlaceholder("<MM>", "%m");
    replaceTimePlaceholder("<DD>", "%d");
    replaceTimePlaceholder("<hh>", "%H");
    replaceTimePlaceholder("<mm>", "%M");
    replaceTimePlaceholder("<ss>", "%S");
  }

  if (mode == RenamingMode::DirectoryScan) {
    // DirectoryScan-specific placeholders
    pos = result.find("<ext>");
    while (pos != std::string::npos) {
      result.replace(pos, 5, originalExtension);
      pos = result.find("<ext>", pos + originalExtension.length());
    }
    pos = result.find("<orig_ext>");
    while (pos != std::string::npos) {
      result.replace(pos, 10, originalExtension);
      pos = result.find("<orig_ext>", pos + originalExtension.length());
    }
    std::string newNumStr =
        dirScanNewNum.has_value()
            ? FormatNumber(dirScanNewNum.value(), dirScanNumberWidth)
            : "";
    pos = result.find("<num>");
    while (pos != std::string::npos) {
      result.replace(pos, 5, newNumStr);
      pos = result.find("<num>", pos + newNumStr.length());
    }
    std::string origNumStr =
        dirScanOriginalNum.has_value()
            ? FormatNumber(dirScanOriginalNum.value(), dirScanNumberWidth)
            : "";
    pos = result.find("<orig_num>");
    while (pos != std::string::npos) {
      result.replace(pos, 10, origNumStr);
      pos = result.find("<orig_num>", pos + origNumStr.length());
    }
    pos = result.find("<orig_name>");
    while (pos != std::string::npos) {
      result.replace(pos, 11, originalNameStem);
      pos = result.find("<orig_name>", pos + originalNameStem.length());
    }

    // Clear placeholders not used in this mode
    const std::vector<std::string> unusedPlaceholders = {"<index>"};
    for (const auto &ph : unusedPlaceholders) {
      pos = result.find(ph);
      while (pos != std::string::npos) {
        result.replace(pos, ph.length(), "");
        pos = result.find(ph, pos);
      }
    }
  } else // RenamingMode::ManualSelection
  {
    // ManualSelection-specific placeholders
    int indexWidth =
        (totalManualFiles > 0)
            ? static_cast<int>(std::floor(std::log10(totalManualFiles))) + 1
            : 1;
    indexWidth = std::max(1, indexWidth); // Ensure index width is at least 1
    std::string indexStr = FormatNumber(index, indexWidth);
    pos = result.find("<index>");
    while (pos != std::string::npos) {
      result.replace(pos, 7, indexStr);
      pos = result.find("<index>", pos + indexStr.length());
    }
    pos = result.find("<orig_name>");
    while (pos != std::string::npos) {
      result.replace(pos, 11, originalNameStem);
      pos = result.find("<orig_name>", pos + originalNameStem.length());
    }
    pos = result.find("<orig_ext>");
    while (pos != std::string::npos) {
      result.replace(pos, 10, originalExtension);
      pos = result.find("<orig_ext>", pos + originalExtension.length());
    }
    pos = result.find("<ext>");
    while (pos != std::string::npos) {
      result.replace(pos, 5, originalExtension);
      pos = result.find("<ext>", pos + originalExtension.length());
    }

    // Clear placeholders not used in this mode
    const std::vector<std::string> unusedPlaceholders = {"<num>", "<orig_num>"};
    for (const auto &ph : unusedPlaceholders) {
      pos = result.find(ph);
      while (pos != std::string::npos) {
        result.replace(pos, ph.length(), "");
        pos = result.find(ph, pos);
      }
    }
  }

  // Sanitize the generated filename stem, preserving the extension
  std::string stem_to_sanitize, preserved_ext;
  const std::size_t last_dot_pos = result.find_last_of('.');
  // Correctly identify stem vs extension (e.g. ".bashrc" has no stem for this
  // logic)
  if (last_dot_pos != std::string::npos && last_dot_pos != 0 &&
      last_dot_pos + 1 < result.size()) {
    stem_to_sanitize.assign(result, 0, last_dot_pos);
    preserved_ext.assign(result, last_dot_pos);
  } else {
    stem_to_sanitize = result; // No discernible extension, treat whole as stem
  }
  std::string sanitized_stem = sanitise_stem(stem_to_sanitize);
  // Ensure a valid, non-empty filename results
  if ((sanitized_stem == "_" && preserved_ext.empty()) ||
      (sanitized_stem.empty() && preserved_ext.empty())) {
    return "_";
  }
  return sanitized_stem + preserved_ext;
}

// Modifies the case of the filename's stem (part before extension) according to
// 'mode'
std::string RenamerLogic::ApplyCaseConversion(std::string filename,
                                              CaseConversionMode mode) {
  if (mode == CaseConversionMode::NoChange || filename.empty()) {
    return filename;
  }
  fs::path p(filename);
  // Avoid misinterpreting dotfiles (e.g., ".profile") as having an empty stem
  if (!filename.empty() && filename[0] == '.' &&
      p.stem().string() == filename) {
    return filename;
  }
  std::string stem = p.stem().string();
  std::string ext = p.extension().string(); // Extension case is preserved
  if (!stem.empty()) {
    if (mode == CaseConversionMode::ToUpper) {
      std::transform(stem.begin(), stem.end(), stem.begin(),
                     [](unsigned char c) { return std::toupper(c); });
    } else if (mode == CaseConversionMode::ToLower) {
      std::transform(stem.begin(), stem.end(), stem.begin(),
                     [](unsigned char c) { return std::tolower(c); });
    }
  }
  return stem + ext;
}

// Gets the path to the history log file in user's app data directory
fs::path RenamerLogic::getHistoryLogPath() {
  wxString stdPath = wxStandardPaths::Get().GetUserDataDir();
  fs::path logDir = fs::path(stdPath.ToStdWstring());
  std::error_code ec;
  if (!fs::exists(logDir, ec)) {
    fs::create_directories(logDir, ec);
  }
  return logDir / "rename_history.log";
}

// Writes rename operations to history log file with timestamp
bool RenamerLogic::writeHistoryLog(
    const std::vector<RenameOperation> &operations,
    const std::string &operationType) {
  if (operations.empty())
    return true;

  fs::path logPath = getHistoryLogPath();
  std::ofstream logFile(logPath, std::ios::app);
  if (!logFile.is_open())
    return false;

  // Get current timestamp
  auto now = std::chrono::system_clock::now();
  auto now_c = std::chrono::system_clock::to_time_t(now);
  std::tm now_tm = {};
#ifdef _WIN32
  localtime_s(&now_tm, &now_c);
#else
  localtime_r(&now_c, &now_tm);
#endif

  std::ostringstream timestamp;
  timestamp << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");

  logFile << "\n=== " << operationType << " at " << timestamp.str() << " ===\n";
  logFile << "Files: " << operations.size() << "\n";

  for (const auto &op : operations) {
    logFile << "  " << op.OldFullPath.string() << " -> "
            << op.NewFullPath.string() << "\n";
  }

  logFile.close();
  return true;
}