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
    // In M0, metadata is not used yet.
    (void)metadata; // Mark as unused

    // This is a simplified sendFile for M0. It expects relativePath to be the FULL SOURCE PATH of the file.
    // This will be improved when BackupEngine is implemented, which will provide the correct source file path.
    // For now, to make the CLI one-shot work, we assume relativePath is the source.
    const std::filesystem::path sourcePath = relativePath; 
    const std::filesystem::path destinationFile = std::filesystem::path(currentBasePath_) / sourcePath.filename();

    try {
        // Ensure the subdirectory structure exists in the destination
        if (destinationFile.has_parent_path()) {
            std::filesystem::create_directories(destinationFile.parent_path());
        }

        // Copy the file
        // For M0, this is a simple copy. Error handling can be improved.
        // std::filesystem::copy_file(sourcePath, destinationFile, std::filesystem::copy_options::overwrite_existing);
        // Using fstream for a more manual copy to avoid potential issues with std::filesystem::copy_file on some systems/compilers initially
        std::ifstream src(sourcePath, std::ios::binary);
        if (!src.is_open()) {
            std::cerr << "LocalTarget: Error opening source file " << sourcePath << std::endl;
            return false;
        }
        std::ofstream dst(destinationFile, std::ios::binary);
        if (!dst.is_open()) {
            std::cerr << "LocalTarget: Error opening destination file " << destinationFile << std::endl;
            return false;
        }
        dst << src.rdbuf();
        src.close();
        dst.close();

        std::cout << "LocalTarget: Copied " << sourcePath << " to " << destinationFile << std::endl;
        return true;
    } catch (const std::exception& e) { // Catching std::exception for broader filesystem errors
        std::cerr << "LocalTarget: Error copying file " << sourcePath << " to " << destinationFile << ": " << e.what() << std::endl;
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
