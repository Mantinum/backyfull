#ifndef ISTORAGETARGET_H
#define ISTORAGETARGET_H

#include <string>
#include <vector> // Or some other appropriate type for metadata
#include <cstdint>

// Forward declaration if metadata is a complex type defined elsewhere
// struct FileMetadata; 

// Definition of FileMetadata struct
struct FileMetadata {
    std::string name;
    uint64_t size = 0;
    int64_t modificationTime = 0; // Unix timestamp
    bool isDirectory = false;

    // Optional: Default constructor
    FileMetadata() = default;

    // Optional: Parameterized constructor
    FileMetadata(std::string n, uint64_t s, int64_t mt, bool isDir)
        : name(std::move(n)), size(s), modificationTime(mt), isDirectory(isDir) {}
};

// Then, the IStorageTarget class definition follows
class IStorageTarget {
public:
    virtual ~IStorageTarget() = default;

    virtual bool beginSession() = 0;
    // Ensure sendFile uses the struct FileMetadata
    virtual bool sendFile(const std::string& localPath, const FileMetadata& metadata) = 0;
    virtual bool deleteFile(const std::string& remotePath) = 0;
    // Ensure listFiles returns a vector of the struct FileMetadata
    virtual std::vector<FileMetadata> listFiles(const std::string& path) = 0;
    virtual bool downloadFile(const std::string& remotePath, const std::string& localPath) = 0;
    virtual bool endSession() = 0;
};

#endif // ISTORAGETARGET_H
