#ifndef ISTORAGETARGET_H
#define ISTORAGETARGET_H

#include <string>
#include <vector> // Or some other appropriate type for metadata

// Forward declaration if metadata is a complex type defined elsewhere
// struct FileMetadata; 

class IStorageTarget {
public:
    virtual ~IStorageTarget() = default;

    // Placeholder for metadata structure. For now, a string or map might suffice.
    // For M0, metadata might not be strictly needed for LocalTarget, 
    // but the interface should have it.
    // Let's use std::string as a placeholder for metadata for now.
    // It could represent e.g. a JSON string or a simple file hash.
    using FileMetadata = std::string; 

    virtual bool beginSession() = 0;
    virtual bool sendFile(const std::string& relativePath, const FileMetadata& metadata) = 0;
    virtual bool deleteFile(const std::string& relativePath) = 0;
    virtual bool endSession() = 0;
};

#endif // ISTORAGETARGET_H
