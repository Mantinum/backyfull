#include <iostream>
#include <string>
#include <vector>
#include <filesystem> // Required for checking if source is a file or directory

// It's good practice to include headers from your own project with a path
// relative to a root include directory, assuming CMake is set up for this.
// For example, if "backy_x/include" is an include directory:
#include "targets/LocalTarget.h" 
// If not, you might need a relative path like:
// #include "../../include/targets/LocalTarget.h" 
// This depends on how CMake will be configured to add include directories.
// Let's assume "include" will be an include path for the executable.

void printUsage() {
    std::cout << "Usage: backy_x_cli --src <source_path> --dst <destination_path>" << std::endl;
    std::cout << "  --src: Path to the source file or directory to back up." << std::endl;
    std::cout << "  --dst: Path to the local destination directory." << std::endl;
}

int main(int argc, char* argv[]) {
    std::string sourcePath;
    std::string destinationPath;

    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        printUsage();
        return 1;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--src" && i + 1 < args.size()) {
            sourcePath = args[++i];
        } else if (args[i] == "--dst" && i + 1 < args.size()) {
            destinationPath = args[++i];
        }
    }

    if (sourcePath.empty() || destinationPath.empty()) {
        std::cerr << "Error: Source and destination paths must be provided." << std::endl;
        printUsage();
        return 1;
    }

    std::cout << "BackyFull CLI - M0 One-Shot Backup" << std::endl;
    std::cout << "Source: " << sourcePath << std::endl;
    std::cout << "Destination: " << destinationPath << std::endl;

    // --- Perform Backup ---
    LocalTarget localTarget(destinationPath);

    if (!localTarget.beginSession()) {
        std::cerr << "Error: Could not begin backup session with LocalTarget." << std::endl;
        return 1;
    }

    // For M0, we handle a single file or files directly in a directory.
    // No recursive directory traversal yet.
    // The BackupEngine will handle this properly later.
    try {
        std::filesystem::path srcFsPath(sourcePath);
        if (std::filesystem::is_regular_file(srcFsPath)) {
            // In M0, metadata is an empty string for LocalTarget.
            // As per LocalTarget M0 implementation, pass the full sourcePath as the first argument.
            if (!localTarget.sendFile(sourcePath, "")) {
                std::cerr << "Error: Failed to backup file " << sourcePath << std::endl;
                localTarget.endSession();
                return 1;
            }
        } else if (std::filesystem::is_directory(srcFsPath)) {
            // For M0, if it's a directory, we'll just iterate over immediate files.
            // No recursive behavior for now.
            std::cout << "Source is a directory. Backing up immediate files (non-recursive M0 behavior)..." << std::endl;
            for (const auto& entry : std::filesystem::directory_iterator(srcFsPath)) {
                if (entry.is_regular_file()) {
                    // As per LocalTarget M0 implementation, pass the full path of the file.
                    if (!localTarget.sendFile(entry.path().string(), "")) {
                        std::cerr << "Error: Failed to backup file " << entry.path().string() << std::endl;
                        // Decide if one file failure should stop the whole backup for M0
                    }
                }
            }
        } else {
            std::cerr << "Error: Source path " << sourcePath << " is not a regular file or directory." << std::endl;
            localTarget.endSession();
            return 1;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error accessing source path " << sourcePath << ": " << e.what() << std::endl;
        localTarget.endSession();
        return 1;
    }


    if (!localTarget.endSession()) {
        std::cerr << "Error: Could not properly end backup session with LocalTarget." << std::endl;
        // Continue to return 0 for M0 if files were copied, or decide on a different error code.
    }

    std::cout << "Backup process completed." << std::endl;

    return 0;
}
