#include "gtest/gtest.h"
#include "targets/SftpTarget.h" // The class we're testing
#include "core/IStorageTarget.h" // For FileMetadata type
// #include "util/CredentialManager.h" // Not directly mocking here, but SftpTarget uses it.

#include <fstream>  // For std::ofstream to create dummy files
#include <iostream> // For std::cerr in tests if needed
#include <filesystem> // For std::filesystem::temp_directory_path, ::remove
#include <map>
#include <string>

// Helper function to create a dummy file for testing uploads
std::string createDummyFile(const std::string& fileName, size_t size) {
    std::filesystem::path tempFilePath = std::filesystem::temp_directory_path() / fileName;
    std::ofstream outfile(tempFilePath, std::ios::binary);
    if (outfile.is_open()) {
        std::vector<char> buffer(size, 'A'); // Fill with 'A's
        outfile.write(buffer.data(), size);
        outfile.close();
        return tempFilePath.string();
    }
    return ""; // Return empty if failed
}

class SftpTargetTest : public ::testing::Test {
protected:
    std::map<std::string, std::string> baseConfig;
    std::string dummyFileName = "test_upload_file.txt";
    std::string dummyFilePath;

    void SetUp() override {
        // Basic valid configuration
        baseConfig["host"] = "localhost"; // Default, will fail for actual connection but fine for some offline tests
        baseConfig["username"] = "testuser";
        baseConfig["password"] = "testpass"; // SftpTarget will try to store this
        baseConfig["remoteBasePath"] = "backy_x_tests"; // Corrected typo here
        baseConfig["port"] = "2222"; // Non-standard port often used for testing

        // Create a dummy file for tests that need one
        dummyFilePath = createDummyFile(dummyFileName, 1024); // 1KB file
        ASSERT_FALSE(dummyFilePath.empty()) << "Failed to create dummy file for testing.";
    }

    void TearDown() override {
        // Remove the dummy file
        if (!dummyFilePath.empty()) {
            std::filesystem::remove(dummyFilePath);
        }
    }
};

// --- Test Construction ---
TEST_F(SftpTargetTest, ConstructorValidMinimalConfig) {
    std::map<std::string, std::string> config = {
        {"host", "sftphost"},
        {"username", "user"}
        // Password and remoteBasePath are optional for construction, SftpTarget might try to get pass from CM
    };
    SftpTarget target(config);
    // No direct way to check members without getters or friend class,
    // but constructor should not crash or throw.
    // SftpTarget logs if essential members are missing, which is a side effect.
    SUCCEED(); // If it constructs without error
}

TEST_F(SftpTargetTest, ConstructorMissingHost) {
    std::map<std::string, std::string> config = {
        {"username", "user"}
    };
    // SftpTarget constructor logs to cerr if host is missing.
    // We can't easily capture cerr with gtest without more setup.
    // For now, just expect it not to crash.
    // A more robust test would involve checking an error state if SftpTarget had one.
    SftpTarget target(config);
    // If SftpTarget set an internal error flag, we'd check it here.
    // For now, we're relying on its internal logging for this case.
    // The `beginSession` will fail later, which is an indirect check.
    EXPECT_FALSE(target.beginSession()) << "beginSession should fail if host was missing in config.";
}

TEST_F(SftpTargetTest, ConstructorMissingUsername) {
    std::map<std::string, std::string> config = {
        {"host", "sftphost"}
    };
    SftpTarget target(config);
    EXPECT_FALSE(target.beginSession()) << "beginSession should fail if username was missing in config.";
}

TEST_F(SftpTargetTest, ConstructorInvalidPort) {
    std::map<std::string, std::string> config = {
        {"host", "sftphost"},
        {"username", "user"},
        {"port", "notAPort"}
    };
    // SftpTarget constructor logs error and defaults to 22.
    SftpTarget target(config);
    // No direct way to check port without getter, but it should not crash.
    SUCCEED();
}

// --- Offline Behavior Tests ---
TEST_F(SftpTargetTest, BeginSessionInvalidHost) {
    std::map<std::string, std::string> config = baseConfig;
    config["host"] = "nonexistent.invalid.host.for.testing"; // Definitely not resolvable
    SftpTarget target(config);
    EXPECT_FALSE(target.beginSession());
}

TEST_F(SftpTargetTest, EndSessionNoBegin) {
    SftpTarget target(baseConfig);
    // endSession should be safe to call even if beginSession was not called or failed
    // SftpTarget's endSession currently returns true even if no handle exists.
    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetTest, SendFileNoSession) {
    SftpTarget target(baseConfig);
    // Attempt to send file without calling beginSession
    FileMetadata dummy{};
    EXPECT_FALSE(target.sendFile(dummyFilePath, dummy))
        << "sendFile should fail if beginSession was not called.";
}

TEST_F(SftpTargetTest, SendFileAfterFailedBeginSession) {
    std::map<std::string, std::string> config = baseConfig;
    config["host"] = ""; // Ensure beginSession fails
    SftpTarget target(config);
    ASSERT_FALSE(target.beginSession()); // Precondition: beginSession failed
    FileMetadata dummy{};
    EXPECT_FALSE(target.sendFile(dummyFilePath, dummy))
        << "sendFile should fail if beginSession failed.";
}

TEST_F(SftpTargetTest, SendNonExistentLocalFile) {
    SftpTarget target(baseConfig);
    // We can't fully test beginSession offline without a mock server,
    // so we assume it might "succeed" in setting up curl handle,
    // but sendFile should fail on local file issues.
    // For a true offline test, we'd need to ensure beginSession returns false.
    // Let's use a config that makes beginSession likely to fail.
    baseConfig["host"] = "invalid-host-for-offline-test";
    SftpTarget offlineTarget(baseConfig);
    
    // Even if beginSession were to somehow pass in a test setup (e.g. mock curl),
    // the file operation itself should fail.
    // If beginSession fails (as it should with invalid host), sendFile also fails.
    if (!offlineTarget.beginSession()) { // This is expected
         FileMetadata dummy{};
         EXPECT_FALSE(offlineTarget.sendFile("non_existent_local_file.txt", dummy))
            << "sendFile should fail if beginSession failed.";
    } else { // Should not happen with invalid host, but if it did:
        FileMetadata dummy{};
        EXPECT_FALSE(offlineTarget.sendFile("non_existent_local_file.txt", dummy))
            << "sendFile should fail for non-existent local file even if session began.";
        offlineTarget.endSession();
    }
}

// --- Online Behavior Tests (Placeholders) ---
// These tests require a live SFTP server.
// Configure environment variables or a local config file for credentials and server details.
// For example: SFTP_TEST_HOST, SFTP_TEST_PORT, SFTP_TEST_USER, SFTP_TEST_PASSWORD, SFTP_TEST_BASEPATH

class SftpTargetOnlineTest : public SftpTargetTest {
protected:
    std::map<std::string, std::string> onlineConfig;
    bool configured = false;

    void SetUp() override {
        SftpTargetTest::SetUp(); // Call base SetUp for dummy file

        const char* host = std::getenv("SFTP_TEST_HOST");
        const char* port_str = std::getenv("SFTP_TEST_PORT");
        const char* user = std::getenv("SFTP_TEST_USER");
        const char* pass = std::getenv("SFTP_TEST_PASSWORD");
        const char* path = std::getenv("SFTP_TEST_BASEPATH");

        if (host && port_str && user && pass && path) {
            onlineConfig["host"] = host;
            onlineConfig["port"] = port_str;
            onlineConfig["username"] = user;
            onlineConfig["password"] = pass;
            onlineConfig["remoteBasePath"] = path;
            configured = true;
        } else {
            std::cout << "SFTP Online Test: Environment variables SFTP_TEST_HOST, _PORT, _USER, _PASSWORD, _BASEPATH not set. Skipping online tests." << std::endl;
        }
    }
};

TEST_F(SftpTargetOnlineTest, ConnectDisconnect) {
    if (!configured) {
        GTEST_SKIP() << "SFTP server not configured. Skipping test.";
        return;
    }
    SftpTarget target(onlineConfig);
    EXPECT_TRUE(target.beginSession());
    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadSingleFile) {
    if (!configured) {
        GTEST_SKIP() << "SFTP server not configured. Skipping test.";
        return;
    }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());
    std::string remoteFileName = "uploaded_test_file.txt";
    FileMetadata dummy{};
    EXPECT_TRUE(target.sendFile(dummyFilePath, dummy));
    // TODO: Add verification step (e.g., list directory or try to download)
    // For now, also test delete.
    EXPECT_TRUE(target.deleteFile(remoteFileName)) << "Failed to delete the uploaded file. Manual cleanup may be required.";
    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadToNonExistentDirectoryAndCreate) {
    if (!configured) {
        GTEST_SKIP() << "SFTP server not configured. Skipping test.";
        return;
    }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());
    std::string remotePathWithDir = "new_test_dir/another_uploaded_test_file.txt";
    FileMetadata dummy_subdir{};
    EXPECT_TRUE(target.sendFile(dummyFilePath, dummy_subdir))
        << "Failed to upload file, possibly due to directory creation failure.";
    
    // Cleanup: delete the file, then attempt to delete the directory.
    // Note: libcurl SFTP delete is for files. Directory deletion might need different commands (e.g. "rmdir")
    // and might fail if directory is not empty.
    EXPECT_TRUE(target.deleteFile(remotePathWithDir)) << "Failed to delete the uploaded file in new_test_dir.";
    // Add a step to remove "new_test_dir" if possible/needed, or ensure server cleans it up.
    // For now, we rely on CURLOPT_FTP_CREATE_MISSING_DIRS for creation.
    // Deleting directories is more complex with libcurl's SFTP file-oriented commands.
    // SftpTarget::deleteFile is also file-oriented.
    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, AuthenticationFailure) {
    if (!configured) {
        GTEST_SKIP() << "SFTP server not configured. Skipping test.";
        return;
    }
    std::map<std::string, std::string> badConfig = onlineConfig;
    badConfig["password"] = "wrongpassword";
    SftpTarget target(badConfig);
    EXPECT_FALSE(target.beginSession()) << "beginSession should fail with incorrect password.";
}

TEST_F(SftpTargetOnlineTest, DeleteNonExistentFile) {
     if (!configured) {
        GTEST_SKIP() << "SFTP server not configured. Skipping test.";
        return;
    }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());
    // SftpTarget::deleteFile returns true for errSecItemNotFound (macOS keychain)
    // For SFTP, libcurl might return an error for file not found.
    // Current SftpTarget::deleteFile returns true if curl_easy_perform is OK.
    // A robust SFTP delete would check server response codes.
    // Let's assume for now it should return false if the file truly isn't there and RM fails.
    // However, some servers might return success for RM on non-existent file.
    // This test's expectation might need adjustment based on typical server behavior.
    // Some SFTP servers return success for 'rm' on a non-existent file.
    // If that's the case for the test server, this might need to be EXPECT_TRUE or have server-specific logic.
    // For now, assuming a strict server that errors on 'rm non_existent_file'.
    EXPECT_FALSE(target.deleteFile("this_file_should_not_exist_ever.txt"));
    EXPECT_TRUE(target.endSession());
}

// --- Tests for filenames with special characters ---

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_Spaces) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    meta.name = "test file with spaces.txt";
    // meta.size and meta.modificationTime are not strictly needed for sendFile/deleteFile path tests.

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload file with spaces in name.";
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete file with spaces in name.";

    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_Apostrophe) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    meta.name = "file_with_apostrophe'.txt";

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload file with apostrophe in name.";
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete file with apostrophe in name.";

    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_Accent) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    meta.name = "fichier_avec_accent_à.txt"; // UTF-8 encoded string

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload file with accent in name.";
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete file with accent in name.";

    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_CaptureDEcran) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    // This specific string "Capture d’écran test.png" uses:
    // ’ : RIGHT SINGLE QUOTATION MARK (U+2019)
    // é : LATIN SMALL LETTER E WITH ACUTE (U+00E9) - precomposed
    // Or it could be e + combining acute accent : e (U+0065) +  ́ (U+0301)
    // We'll use the precomposed version as it's more common in direct typing.
    // C++ string literals with UTF-8 characters need to be handled correctly by the compiler.
    // Ensure the source file is saved as UTF-8.
    meta.name = "Capture d’écran test.png";

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload 'Capture d’écran test.png'.";
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete 'Capture d’écran test.png'.";

    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_ComplexName) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    meta.name = "complex name ' & à # % .txt"; // Includes various characters

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload file with complex name.";
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete file with complex name.";

    EXPECT_TRUE(target.endSession());
}

TEST_F(SftpTargetOnlineTest, UploadDeleteSpecialCharFile_FileNameWithSlash) {
    if (!configured) { GTEST_SKIP() << "SFTP server not configured. Skipping test."; return; }
    SftpTarget target(onlineConfig);
    ASSERT_TRUE(target.beginSession());

    FileMetadata meta;
    // This tests if a filename containing a slash (which should be percent-encoded) works.
    // The SftpTarget::buildSftpUrl is expected to encode the '/' within the filename part.
    meta.name = "file/with/slash.txt";

    EXPECT_TRUE(target.sendFile(dummyFilePath, meta)) << "Failed to upload file with slash in its name.";
    // After upload, the file should exist on SFTP server as "file%2Fwith%2Fslash.txt" in the remoteBasePath.
    // The deleteFile will use the same meta.name, and buildSftpUrl should generate the same encoded path.
    EXPECT_TRUE(target.deleteFile(meta.name)) << "Failed to delete file with slash in its name.";

    EXPECT_TRUE(target.endSession());
}


// Main function for Google Test (if not linking with gtest_main)
// int main(int argc, char **argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
