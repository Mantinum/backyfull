#ifndef ISTORAGETARGET_H
#define ISTORAGETARGET_H

#include <string>
#include <vector> // Or some other appropriate type for metadata
#include <cstdint>

// Forward declaration if metadata is a complex type defined elsewhere
// struct FileMetadata; 

struct FileMetadata {
    std::string name;
    uint64_t size;
    int64_t modificationTime; // Unix timestamp
    bool isDirectory;
};

class IStorageTarget {
public:
    virtual ~IStorageTarget() = default;

    virtual bool beginSession() = 0;
    virtual bool sendFile(const std::string& relativePath, const FileMetadata& metadata) = 0;
    virtual bool deleteFile(const std::string& relativePath) = 0;
    virtual std::vector<FileMetadata> listFiles(const std::string& prefix) = 0; // Corrected return type
    virtual bool downloadFile(const std::string& remotePath, const std::string& localPath) = 0;
    virtual bool endSession() = 0;
};

#endif // ISTORAGETARGET_H
