#include "pch.h"
#include "TestFixtures.h"
#include "../../src/Logic/RenamerLogic.h"
#include <vector>
#include <optional>

namespace fs = std::filesystem;

TEST_F(RenamerLogicFilesystemTest, PerformRenameAndUndo_Successful)
{
    fs::path oldFile1 = tempTestDir / "original_A.tmp";
    fs::path oldFile2 = tempTestDir / "original_B.tmp";
    CreateDummyFile(oldFile1, "contentA");
    CreateDummyFile(oldFile2, "contentB");

    fs::path newFile1 = tempTestDir / "renamed_X.tmp";
    fs::path newFile2 = tempTestDir / "renamed_Y.tmp";

    std::vector<RenameOperation> plan = {
        {"original_A.tmp", "renamed_X.tmp", oldFile1, newFile1, std::nullopt, 1},
        {"original_B.tmp", "renamed_Y.tmp", oldFile2, newFile2, std::nullopt, 2}};

    RenameExecutionResult renameRes = RenamerLogic::performRename(plan, 0);
    ASSERT_TRUE(renameRes.overallSuccess);
    ASSERT_EQ(renameRes.successfulRenameOps.size(), 2);
    EXPECT_TRUE(fs::exists(newFile1));
    EXPECT_TRUE(fs::exists(newFile2));
    EXPECT_FALSE(fs::exists(oldFile1));
    EXPECT_FALSE(fs::exists(oldFile2));

    UndoResult undoRes = RenamerLogic::performUndo(renameRes.successfulRenameOps);
    ASSERT_TRUE(undoRes.overallSuccess);
    ASSERT_EQ(undoRes.successfulUndos.size(), 2);
    EXPECT_TRUE(fs::exists(oldFile1));
    EXPECT_TRUE(fs::exists(oldFile2));
    EXPECT_FALSE(fs::exists(newFile1));
    EXPECT_FALSE(fs::exists(newFile2));

    std::ifstream ifsA(oldFile1);
    std::string contentA_restored((std::istreambuf_iterator<char>(ifsA)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contentA_restored, "contentA");
    ifsA.close();
}

TEST_F(RenamerLogicFilesystemTest, PerformRename_SourceMissing)
{
    fs::path oldFile = tempTestDir / "non_existent_source.txt";
    fs::path newFile = tempTestDir / "target_for_non_existent.txt";

    std::vector<RenameOperation> plan = {
        {"non_existent_source.txt", "target_for_non_existent.txt", oldFile, newFile, std::nullopt, 1}};

    RenameExecutionResult renameRes = RenamerLogic::performRename(plan, 0);
    ASSERT_FALSE(renameRes.overallSuccess);
    ASSERT_EQ(renameRes.successfulRenameOps.size(), 0);
    ASSERT_EQ(renameRes.failedRenames.size(), 1);
    EXPECT_FALSE(fs::exists(newFile));
}