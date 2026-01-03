#include "pch.h"
#include "TestFixtures.h"
#include "../../src/Logic/RenamerLogic.h"
#include <vector>
#include <optional>

namespace fs = std::filesystem;

TEST_F(RenamerLogicFilesystemTest, CalculatePlan_DirScan_Basic)
{
    CreateDummyFile(tempTestDir / "test_01.txt");
    CreateDummyFile(tempTestDir / "test_02.log");
    CreateDummyFile(tempTestDir / "another_03.txt");
    CreateDummyFile(tempTestDir / "sub" / "sub_test_04.txt");

    InputParams params;
    params.mode = RenamingMode::DirectoryScan;
    params.targetDirectory = tempTestDir;
    params.filenamePattern = "*.txt";
    params.recursiveScan = false;
    params.namingPattern = "New_<orig_name><ext>";
    params.filterExtensions = "";
    params.lowestNumber = 0;
    params.highestNumber = 0;
    params.findText = "";
    params.replaceText = "";
    params.findCaseSensitive = false;
    params.caseConversionMode = CaseConversionMode::NoChange;
    params.increment = 0;

    OutputResults results = RenamerLogic::calculateRenamePlan(params);

    ASSERT_TRUE(results.success);
    ASSERT_EQ(results.renamePlan.size(), 2);

    bool found1 = false, found3 = false;
    for (const auto &op : results.renamePlan)
    {
        if (op.OldName == "test_01.txt")
        {
            EXPECT_STREQ(op.NewName.c_str(), "New_test_01.txt");
            found1 = true;
        }
        else if (op.OldName == "another_03.txt")
        {
            EXPECT_STREQ(op.NewName.c_str(), "New_another_03.txt");
            found3 = true;
        }
    }
    EXPECT_TRUE(found1 && found3);
}

TEST_F(RenamerLogicFilesystemTest, CalculatePlan_DirScan_RecursiveAndFilter)
{
    CreateDummyFile(tempTestDir / "img_05.jpg");
    CreateDummyFile(tempTestDir / "data_011.jpg");
    CreateDummyFile(tempTestDir / "sub" / "img_007.jpg");
    CreateDummyFile(tempTestDir / "sub" / "img_20.png");

    InputParams params;
    params.mode = RenamingMode::DirectoryScan;
    params.targetDirectory = tempTestDir;
    params.filenamePattern = "*.jpg";
    params.filterExtensions = ".jpg";
    params.lowestNumber = 1;
    params.highestNumber = 10;
    params.recursiveScan = true;
    params.namingPattern = "pic_<num><ext>";
    params.increment = 1;
    params.findText = "";
    params.replaceText = "";
    params.findCaseSensitive = false;
    params.caseConversionMode = CaseConversionMode::NoChange;

    OutputResults results = RenamerLogic::calculateRenamePlan(params);
    ASSERT_TRUE(results.success);
    ASSERT_EQ(results.renamePlan.size(), 2);

    bool found5 = false, found7 = false;
    for (const auto &op : results.renamePlan)
    {
        if (op.OldName == "img_05.jpg")
        {
            EXPECT_STREQ(op.NewName.c_str(), "pic_06.jpg");
            found5 = true;
        }
        else if (op.OldName == "img_007.jpg" && op.OldFullPath.parent_path().filename() == "sub")
        {
            EXPECT_STREQ(op.NewName.c_str(), "pic_08.jpg");
            found7 = true;
        }
    }
    EXPECT_TRUE(found5 && found7);
}

TEST_F(RenamerLogicFilesystemTest, CalculatePlan_ManualMode)
{
    fs::path file1_path = tempTestDir / "manual_file_A.txt";
    fs::path file2_path = tempTestDir / "manual_file_B.log";
    CreateDummyFile(file1_path);
    CreateDummyFile(file2_path);

    InputParams params;
    params.mode = RenamingMode::ManualSelection;
    params.manualFiles = {file1_path, file2_path};
    params.namingPattern = "<index>-<orig_name><ext>";
    params.findText = "";
    params.replaceText = "";
    params.findCaseSensitive = false;
    params.caseConversionMode = CaseConversionMode::NoChange;
    params.increment = 0;

    OutputResults results = RenamerLogic::calculateRenamePlan(params);
    ASSERT_TRUE(results.success);
    ASSERT_EQ(results.renamePlan.size(), 2);

    EXPECT_STREQ(results.renamePlan[0].OldName.c_str(), "manual_file_A.txt");
    EXPECT_STREQ(results.renamePlan[0].NewName.c_str(), "1-manual_file_A.txt");
    EXPECT_EQ(results.renamePlan[0].Index, 1);

    EXPECT_STREQ(results.renamePlan[1].OldName.c_str(), "manual_file_B.log");
    EXPECT_STREQ(results.renamePlan[1].NewName.c_str(), "2-manual_file_B.log");
    EXPECT_EQ(results.renamePlan[1].Index, 2);
}

TEST_F(RenamerLogicFilesystemTest, CalculatePlan_TargetExistsSkip)
{
    CreateDummyFile(tempTestDir / "source.txt");
    CreateDummyFile(tempTestDir / "target.txt");

    InputParams params;
    params.mode = RenamingMode::DirectoryScan;
    params.targetDirectory = tempTestDir;
    params.filenamePattern = "source.txt";
    params.namingPattern = "target<ext>";
    params.recursiveScan = false;
    params.filterExtensions = "";
    params.lowestNumber = 0;
    params.highestNumber = 0;
    params.findText = "";
    params.replaceText = "";
    params.findCaseSensitive = false;
    params.caseConversionMode = CaseConversionMode::NoChange;
    params.increment = 0;

    OutputResults results = RenamerLogic::calculateRenamePlan(params);
    EXPECT_TRUE(results.success);
    EXPECT_EQ(results.renamePlan.size(), 0);
    EXPECT_EQ(results.potentialOverwritesLog.size(), 1);
    EXPECT_EQ(results.missingSourceFilesLog.size(), 1);
}