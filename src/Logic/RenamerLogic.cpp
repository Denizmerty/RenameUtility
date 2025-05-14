#include "RenamerLogic.h"
#include <filesystem> // Needed for fs::path

namespace fs = std::filesystem;

// Provides a default directory path, primarily for development and testing purposes,
// or as an initial fallback if no user-configured path is available
const fs::path RenamerLogic::DefaultPath = R"(C:\Temp\RenameTest)";