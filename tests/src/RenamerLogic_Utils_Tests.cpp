#include "pch.h"
#include "../RenameUtility/RenamerLogic.h"
#include <string>
#include <optional>
#include <limits>

TEST(RenamerLogicUtils, ToLowerFunction)
{
    EXPECT_EQ(ToLower("HELLO WORLD"), "hello world");
    EXPECT_EQ(ToLower("Hello World123!"), "hello world123!");
    EXPECT_EQ(ToLower(""), "");
}

TEST(RenamerLogicUtils, EscapeRegexChars)
{
    EXPECT_STREQ(RenamerLogic::EscapeRegexChars(".").c_str(), "\\.");
    EXPECT_STREQ(RenamerLogic::EscapeRegexChars("a*b?c+").c_str(), "a\\*b\\?c\\+");
    EXPECT_STREQ(RenamerLogic::EscapeRegexChars("file(1).txt").c_str(), "file\\(1\\)\\.txt");
    EXPECT_STREQ(RenamerLogic::EscapeRegexChars("").c_str(), "");
}

TEST(RenamerLogicUtils, ConvertWildcardToRegex)
{
    EXPECT_STREQ(RenamerLogic::ConvertWildcardToRegex("*.txt").c_str(), "^.*\\.txt$");
    EXPECT_STREQ(RenamerLogic::ConvertWildcardToRegex("image???.jpg").c_str(), "^image...\\.jpg$");
    EXPECT_STREQ(RenamerLogic::ConvertWildcardToRegex("file(1)*.doc").c_str(), "^file\\(1\\).*\\.doc$");
    EXPECT_STREQ(RenamerLogic::ConvertWildcardToRegex("").c_str(), "^.*$");
}

TEST(RenamerLogicUtils, ParseLastNumber)
{
    EXPECT_EQ(RenamerLogic::ParseLastNumber("file001.txt").value_or(-1), 1);
    EXPECT_EQ(RenamerLogic::ParseLastNumber("image_123_abc.jpg").value_or(-1), 123);
    EXPECT_FALSE(RenamerLogic::ParseLastNumber("photo.png").has_value());
    EXPECT_EQ(RenamerLogic::ParseLastNumber("version1.2.3.zip").value_or(-1), 3);
    std::string largeNumStr = "file" + std::to_string((long long)std::numeric_limits<int>::max() + 1) + ".txt";
    EXPECT_FALSE(RenamerLogic::ParseLastNumber(largeNumStr).has_value());
}

TEST(RenamerLogicUtils, FormatNumber)
{
    EXPECT_STREQ(RenamerLogic::FormatNumber(5, 3).c_str(), "005");
    EXPECT_STREQ(RenamerLogic::FormatNumber(123, 3).c_str(), "123");
    EXPECT_STREQ(RenamerLogic::FormatNumber(7, 1).c_str(), "7");
    EXPECT_STREQ(RenamerLogic::FormatNumber(12345, 3).c_str(), "12345");
    EXPECT_STREQ(RenamerLogic::FormatNumber(-5, 3).c_str(), "-5");
}

TEST(RenamerLogicUtils, IEquals)
{
    EXPECT_TRUE(RenamerLogic::iequals("Test", "test"));
    EXPECT_TRUE(RenamerLogic::iequals("CaseInsensitive", "caseinsensitive"));
    EXPECT_FALSE(RenamerLogic::iequals("Test", "Test1"));
    EXPECT_FALSE(RenamerLogic::iequals("Test", ""));
    EXPECT_TRUE(RenamerLogic::iequals("", ""));
}

TEST(RenamerLogicUtils, PerformFindReplace)
{
    EXPECT_STREQ(RenamerLogic::PerformFindReplace("hello world", "world", "GTest", true).c_str(), "hello GTest");
    EXPECT_STREQ(RenamerLogic::PerformFindReplace("Test Test", "Test", "Check", false).c_str(), "Check Check");
    EXPECT_STREQ(RenamerLogic::PerformFindReplace("Case Test", "test", "Match", true).c_str(), "Case Test");
    EXPECT_STREQ(RenamerLogic::PerformFindReplace("Case Test", "test", "Match", false).c_str(), "Case Match");
}

TEST(RenamerLogicUtils, ApplyCaseConversion)
{
    EXPECT_STREQ(RenamerLogic::ApplyCaseConversion("FileName.Txt", CaseConversionMode::ToUpper).c_str(), "FILENAME.Txt");
    EXPECT_STREQ(RenamerLogic::ApplyCaseConversion("FileName.Txt", CaseConversionMode::ToLower).c_str(), "filename.Txt");
    EXPECT_STREQ(RenamerLogic::ApplyCaseConversion("File.Name.With.Dots.ext", CaseConversionMode::ToUpper).c_str(), "FILE.NAME.WITH.DOTS.ext");
    EXPECT_STREQ(RenamerLogic::ApplyCaseConversion(".hiddenFile", CaseConversionMode::ToLower).c_str(), ".hiddenFile");
}

TEST(RenamerLogicUtils, ReplacePlaceholders_DirScan)
{
    std::string originalFullName = "My Image 01.jpg";
    std::string originalNameStem = "My Image 01";
    std::string originalExtension = ".jpg";
    std::optional<int> origNum = 1;
    std::optional<int> newNum = 10;
    int numWidth = 3;
    std::string pattern = "Photo_<num>_Original_<orig_num>_Name_<orig_name><ext>";
    std::string expected = "Photo_010_Original_001_Name_My Image 01.jpg";
    std::string actual = RenamerLogic::ReplacePlaceholders(pattern, RenamingMode::DirectoryScan, 0, 0,
                                                           originalFullName, originalNameStem, originalExtension,
                                                           origNum, newNum, numWidth);
    EXPECT_STREQ(actual.c_str(), expected.c_str());

    std::string pattern_invalid_chars = "<orig_name>:*?<num><ext>";
    std::string expected_sanitized = "My Image 01___010.jpg";
    actual = RenamerLogic::ReplacePlaceholders(pattern_invalid_chars, RenamingMode::DirectoryScan, 0, 0,
                                               originalFullName, originalNameStem, originalExtension,
                                               origNum, newNum, numWidth);
    EXPECT_STREQ(actual.c_str(), expected_sanitized.c_str());
}

TEST(RenamerLogicUtils, ReplacePlaceholders_Manual)
{
    std::string originalFullName = "Chapter Notes.docx";
    std::string originalNameStem = "Chapter Notes";
    std::string originalExtension = ".docx";
    int currentIndex = 5;
    int totalFiles = 12;
    std::string pattern = "Doc_<index>_<orig_name><orig_ext>";
    std::string expected = "Doc_05_Chapter Notes.docx";
    std::string actual = RenamerLogic::ReplacePlaceholders(pattern, RenamingMode::ManualSelection, currentIndex, totalFiles,
                                                           originalFullName, originalNameStem, originalExtension,
                                                           std::nullopt, std::nullopt, 0);
    EXPECT_STREQ(actual.c_str(), expected.c_str());
}