#include "targets/LocalTarget.h" // Adjust path as needed
#include <filesystem> // For C++17 filesystem operations
#include <fstream>   // For file copying
#include <iostream>  // For simple logging/error reporting in M0

// Constructor
LocalTarget::LocalTarget(const std::string& destinationPath)
    : destinationPath_(destinationPath) {}

std::string LocalTarget::destinationPathStdStr() const {
    return destinationPath_;
}

bool LocalTarget::beginSession() {
    try {
        // Ensure the base destination directory exists
        std::filesystem::create_directories(destinationPath_);
        // For M0, currentBasePath_ can be the same as destinationPath_
        // In later stages, a session-specific subdirectory might be created.
        currentBasePath_ = destinationPath_; 
        std::cout << "LocalTarget: Session started. Destination: " << currentBasePath_ << std::endl;
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "LocalTarget: Error creating directory " << destinationPath_ << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalTarget::sendFile(const std::string& relativePath, const FileMetadata& metadata) {
    // Treat 'relativePath' (first param) as the absoluteSourcePath for this function's logic
    const std::filesystem::path absoluteSourceFsPath(relativePath); 
    // Treat 'metadata' (second param, which is a std::string from FileMetadata) as the targetRelativePath
    const std::filesystem::path targetRelativeFsPath(metadata); 

    // Construct the full destination path including any subdirectories from targetRelativeFsPath
    const std::filesystem::path destinationFile = std::filesystem::path(currentBasePath_) / targetRelativeFsPath;

    try {
        // Ensure the parent directory for the destination file exists
        if (destinationFile.has_parent_path()) {
            if (!std::filesystem::exists(destinationFile.parent_path())) {
                std::filesystem::create_directories(destinationFile.parent_path());
                std::cout << "LocalTarget: Created directory " << destinationFile.parent_path().string() << std::endl;
            }
        }

        // Copy the file using fstream, ensuring overwrite
        std::ifstream src(absoluteSourceFsPath, std::ios::binary);
        if (!src.is_open()) {
            std::cerr << "LocalTarget: Error opening source file " << absoluteSourceFsPath.string() << std::endl;
            return false;
        }
        std::ofstream dst(destinationFile, std::ios::binary | std::ios::trunc);
        if (!dst.is_open()) {
            std::cerr << "LocalTarget: Error opening destination file " << destinationFile.string() << std::endl;
            src.close();
            return false;
        }
        dst << src.rdbuf();
        src.close();
        dst.close();

        std::cout << "LocalTarget: Copied " << absoluteSourceFsPath.string() << " to " << destinationFile.string() << std::endl;
        return true;
    } catch (const std::filesystem::filesystem_error& e) { 
        std::cerr << "LocalTarget: Filesystem error copying file " << absoluteSourceFsPath.string() << " to " << destinationFile.string() << ": " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) { 
        std::cerr << "LocalTarget: Generic error copying file " << absoluteSourceFsPath.string() << " to " << destinationFile.string() << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalTarget::deleteFile(const std::string& relativePath) {
    // For M0, this can be a placeholder or simple implementation.
    // It would take a path relative to currentBasePath_
    std::filesystem::path fileToDelete = std::filesystem::path(currentBasePath_) / relativePath;
    try {
        if (std::filesystem::exists(fileToDelete)) {
            if (std::filesystem::remove(fileToDelete)) {
                std::cout << "LocalTarget: Deleted " << fileToDelete << std::endl;
                return true;
            } else {
                std::cerr << "LocalTarget: Error deleting file " << fileToDelete << std::endl;
                return false;
            }
        } else {
            std::cout << "LocalTarget: File to delete not found " << fileToDelete << std::endl;
            return true; // Or false if not finding it is an error
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "LocalTarget: Error deleting file " << fileToDelete << ": " << e.what() << std::endl;
        return false;
    }
}

bool LocalTarget::endSession() {
    std::cout << "LocalTarget: Session ended." << std::endl;
    // No specific cleanup needed for local target in M0
    return true;
}

std::vector<std::string> LocalTarget::listFiles(const std::string& prefix) {
    std::cerr << "LocalTarget: listFiles is not implemented." << std::endl;
    // Return an empty vector as per the requirement
    return {};
}
