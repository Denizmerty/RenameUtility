#pragma once
#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error> // For std::error_code

namespace fs = std::filesystem;

class RenamerLogicFilesystemTest : public ::testing::Test
{
protected:
    fs::path tempTestDir;

    void SetUp() override
    {
        tempTestDir = fs::temp_directory_path() / "RenameUtilityGTests_FS";
        std::error_code ec;
        fs::remove_all(tempTestDir, ec); // Clean up from previous runs
        fs::create_directories(tempTestDir, ec);
        if (ec)
        {
            FAIL() << "Failed to create temporary test directory: " << tempTestDir.string() << " Error: " << ec.message();
        }
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tempTestDir, ec); // Clean up
    }

    void CreateDummyFile(const fs::path &path, const std::string &content = "")
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                FAIL() << "Failed to create parent directory for dummy file: " << path.parent_path().string() << " Error: " << ec.message();
            }
        }
        std::ofstream outfile(path);
        if (!outfile)
        {
            FAIL() << "Failed to open dummy file for writing: " << path.string();
        }
        if (!content.empty())
        {
            outfile << content;
        }
        outfile.close();
    }
};
