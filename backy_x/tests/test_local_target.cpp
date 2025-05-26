#include "gtest/gtest.h"
#include "targets/LocalTarget.h" // Adjust if necessary
#include <filesystem>
#include <fstream>
#include <string>
#include <vector> // For cleaning up
#include <sstream> // For readFileContent

// Helper function to create a dummy file for testing
void createDummyFile(const std::filesystem::path& filePath, const std::string& content = "test content") {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream outFile(filePath);
    outFile << content;
    outFile.close();
}

// Helper function to read file content
std::string readFileContent(const std::filesystem::path& filePath) {
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        return ""; // Or throw an exception
    }
    std::stringstream buffer;
    buffer << inFile.rdbuf();
    return buffer.str();
}

// Define a test fixture for LocalTarget tests if needed for setup/teardown
class LocalTargetTest : public ::testing::Test {
protected:
    const std::string testDirName = "_test_output_LocalTargetTest";
    // std::filesystem::path testBaseDir; // Will be initialized in SetUp
    std::filesystem::path sourceDir;
    std::filesystem::path destDir;

    void SetUp() override {
        // Create a unique base directory for test files to avoid collisions
        // Using a subdirectory in the build or test area is common
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::filesystem::path testBaseDir = currentPath / testDirName;
        sourceDir = testBaseDir / "source";
        destDir = testBaseDir / "destination";

        // Clean up before test (in case of previous failed run)
        std::filesystem::remove_all(testBaseDir);

        // Create directories for the test
        std::filesystem::create_directories(sourceDir);
        std::filesystem::create_directories(destDir);
    }

    void TearDown() override {
        // Clean up test files and directories after the test
        // std::filesystem::remove_all(std::filesystem::current_path() / testDirName);
        // Commented out to allow inspection of test output files if needed.
        // For automated CI, it should ideally clean up.
    }
};


TEST_F(LocalTargetTest, CopySingleFile) {
    const std::string fileName = "test_file.txt";
    const std::string fileContent = "This is a test file for LocalTarget.";
    std::filesystem::path sourceFile = sourceDir / fileName;
    std::filesystem::path expectedDestFile = destDir / fileName;

    createDummyFile(sourceFile, fileContent);

    ASSERT_TRUE(std::filesystem::exists(sourceFile)) << "Source file was not created: " << sourceFile;

    LocalTarget target(destDir.string());

    ASSERT_TRUE(target.beginSession()) << "LocalTarget beginSession failed.";

    // As per M0 LocalTarget::sendFile, pass the full source path as the first argument.
    // Metadata is an empty string for M0.
    ASSERT_TRUE(target.sendFile(sourceFile.string(), "")) << "LocalTarget sendFile failed for " << sourceFile;

    ASSERT_TRUE(target.endSession()) << "LocalTarget endSession failed.";

    // Verify that the file was copied
    ASSERT_TRUE(std::filesystem::exists(expectedDestFile)) << "Destination file was not created: " << expectedDestFile;

    // Verify file content
    std::string destContent = readFileContent(expectedDestFile);
    ASSERT_EQ(fileContent, destContent) << "File content mismatch.";

    // Optional: Test deleteFile
    ASSERT_TRUE(target.beginSession()); // Need a session to delete
    ASSERT_TRUE(target.deleteFile(fileName)); // deleteFile takes relative path to destDir
    ASSERT_FALSE(std::filesystem::exists(expectedDestFile)) << "File was not deleted by deleteFile.";
    ASSERT_TRUE(target.endSession());
}

// Minimal main for running tests if not part of a larger test runner setup yet.
// The CI will typically handle calling the test executable.
// int main(int argc, char **argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
