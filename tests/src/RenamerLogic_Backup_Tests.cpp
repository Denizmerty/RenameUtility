#include "pch.h"
#include "TestFixtures.h"
#include "../RenameUtility/RenamerLogic.h"
#include <vector>
#include <optional>

namespace fs = std::filesystem;

TEST_F(RenamerLogicFilesystemTest, PerformBackup_Basic)
{
    fs::path sourceDir = tempTestDir / "sourceForBackup";
    fs::create_directories(sourceDir);
    CreateDummyFile(sourceDir / "file1.txt", "backup_content1");
    CreateDummyFile(sourceDir / "sub" / "file2.txt", "backup_content2");

    BackupResult backupRes = RenamerLogic::performBackup(sourceDir, "TestContextBackup");

    ASSERT_TRUE(backupRes.success) << "Backup Error: " << backupRes.errorMessage;
    ASSERT_FALSE(backupRes.backupPath.empty());
    EXPECT_TRUE(fs::exists(backupRes.backupPath));
    EXPECT_TRUE(fs::exists(backupRes.backupPath / "file1.txt"));
    EXPECT_TRUE(fs::exists(backupRes.backupPath / "sub" / "file2.txt"));

    std::error_code ec;
    if (fs::exists(backupRes.backupPath))
    {
        fs::remove_all(backupRes.backupPath, ec);
    }
}