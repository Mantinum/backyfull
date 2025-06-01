#ifndef LOCALTARGET_H
#define LOCALTARGET_H

#include "core/IStorageTarget.h" // Adjust path as needed based on CMake setup
#include <string>

class LocalTarget : public IStorageTarget {
public:
    explicit LocalTarget(const std::string& destinationPath);
    ~LocalTarget() override = default;

    bool beginSession() override;
    bool sendFile(const std::string& relativePath, const FileMetadata& metadata) override;
    // For M0, deleteFile might not be fully implemented or needed for simple copy
    bool deleteFile(const std::string& relativePath) override; 
    std::vector<IStorageTarget::FileMetadata> listFiles(const std::string& prefix) override;
    bool downloadFile(const std::string& remotePath, const std::string& localPath) override;
    bool endSession() override;

    // Getter for the destination path
    std::string destinationPathStdStr() const;

private:
    std::string destinationPath_;
    std::string currentBasePath_; // To store the base path for the current session
};

#endif // LOCALTARGET_H
