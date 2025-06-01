#include "gtest/gtest.h"
#include "targets/LocalTarget.h" // Adjust if necessary
#include "core/IStorageTarget.h" // For FileMetadata
#include <filesystem>
#include <fstream>
#include <string>
#include <vector> 
#include <sstream> 

// Helper function to create a dummy file for testing
void createDummyFile(const std::filesystem::path& filePath, const std::string& content = "test content") {
    if (filePath.has_parent_path()) { // Ensure parent directory exists
        std::filesystem::create_directories(filePath.parent_path());
    }
    std::ofstream outFile(filePath);
    ASSERT_TRUE(outFile.is_open()) << "Failed to open dummy file for writing: " << filePath;
    outFile << content;
    outFile.close();
}

// Helper function to read file content
std::string readFileContent(const std::filesystem::path& filePath) {
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        // Return a distinct string or throw to indicate error, GTest macros prefer EXPECT/ASSERT in the test body
        return "ERROR_COULD_NOT_OPEN_FILE_FOR_READING"; 
    }
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    return buffer.str();
}

class LocalTargetTest : public ::testing::Test {
protected:
    const std::string testDirName = "_test_output_LocalTargetTest";
    std::filesystem::path testBaseDirFullPath; // Full path to the unique test directory
    std::filesystem::path sourceDir;    // testBaseDirFullPath / "source"
    std::filesystem::path destDir;      // testBaseDirFullPath / "destination" (this is passed to LocalTarget constructor)

    void SetUp() override {
        testBaseDirFullPath = std::filesystem::current_path() / testDirName;
        sourceDir = testBaseDirFullPath / "source";
        destDir = testBaseDirFullPath / "destination";

        std::filesystem::remove_all(testBaseDirFullPath); // Clean up before test

        std::filesystem::create_directories(sourceDir);
        std::filesystem::create_directories(destDir); // This is the directory LocalTarget will use as its root
    }

    void TearDown() override {
        // std::filesystem::remove_all(testBaseDirFullPath); // Comment out for inspection
    }
};

// Test copying a single file to the root of the LocalTarget's destination
TEST_F(LocalTargetTest, CopySingleFileToRoot) {
    const std::string fileName = "test_file.txt";
    const std::string fileContent = "This is a test file for LocalTarget.";
    std::filesystem::path sourceFile = sourceDir / fileName;
    
    createDummyFile(sourceFile, fileContent);
    ASSERT_TRUE(std::filesystem::exists(sourceFile)) << "Source file was not created: " << sourceFile;

    LocalTarget target(destDir.string()); // destDir is the base for LocalTarget

    ASSERT_TRUE(target.beginSession()) << "LocalTarget beginSession failed.";

    // Call sendFile with absolute source path and relative target path (just filename for root)
    FileMetadata dummy{};
    ASSERT_TRUE(target.sendFile(sourceFile.string(), dummy)) << "LocalTarget sendFile failed for " << sourceFile;
    ASSERT_TRUE(target.endSession()) << "LocalTarget endSession failed.";

    // Verify that the file was copied to destDir / fileName
    std::filesystem::path expectedDestFile = destDir / fileName;
    ASSERT_TRUE(std::filesystem::exists(expectedDestFile)) << "Destination file was not created: " << expectedDestFile;
    EXPECT_EQ(readFileContent(expectedDestFile), fileContent) << "File content mismatch.";

    // Test deleteFile (relative path from LocalTarget's root)
    ASSERT_TRUE(target.beginSession()); 
    ASSERT_TRUE(target.deleteFile(fileName)); 
    ASSERT_FALSE(std::filesystem::exists(expectedDestFile)) << "File was not deleted by deleteFile.";
    ASSERT_TRUE(target.endSession());
}

// Test copying a file into a subdirectory within the LocalTarget's destination
TEST_F(LocalTargetTest, CopyFileToSubDirectory) {
    const std::string subDirName = "subdir";
    const std::string fileName = "test_file_in_subdir.txt";
    const std::string relativeTargetPath = subDirName + "/" + fileName; // "subdir/test_file_in_subdir.txt"
    const std::string fileContent = "This is a test file for a subdirectory.";
    
    std::filesystem::path sourceFile = sourceDir / fileName; // Source file can be at root of sourceDir for simplicity
    createDummyFile(sourceFile, fileContent);
    ASSERT_TRUE(std::filesystem::exists(sourceFile)) << "Source file was not created: " << sourceFile;

    LocalTarget target(destDir.string()); // destDir is the base for LocalTarget

    ASSERT_TRUE(target.beginSession()) << "LocalTarget beginSession failed.";
    
    // Call sendFile with absolute source path and relative target path including subdirectory
    FileMetadata dummy_subdir{}; // Use a different name or ensure scope is limited if concerned
    ASSERT_TRUE(target.sendFile(sourceFile.string(), dummy_subdir)) << "LocalTarget sendFile failed for " << sourceFile << " to " << relativeTargetPath;
    ASSERT_TRUE(target.endSession()) << "LocalTarget endSession failed.";

    // Verify that the file was copied to destDir / subDirName / fileName
    std::filesystem::path expectedDestFile = destDir / relativeTargetPath;
    ASSERT_TRUE(std::filesystem::exists(expectedDestFile)) << "Destination file was not created: " << expectedDestFile;
    EXPECT_EQ(readFileContent(expectedDestFile), fileContent) << "File content mismatch.";
    ASSERT_TRUE(std::filesystem::exists(destDir / subDirName)) << "Subdirectory was not created in destination: " << (destDir / subDirName);

    // Test deleteFile (relative path from LocalTarget's root)
    ASSERT_TRUE(target.beginSession());
    ASSERT_TRUE(target.deleteFile(relativeTargetPath));
    ASSERT_FALSE(std::filesystem::exists(expectedDestFile)) << "File was not deleted by deleteFile.";
    // Check if subdir is removed - current deleteFile only removes files. 
    // To remove empty parent dirs would be extra logic not yet in LocalTarget::deleteFile.
    // So, the directory "subdir" might still exist if it became empty.
    // For now, we only assert the file is gone.
    ASSERT_TRUE(target.endSession());
}
