#ifndef FILE_SYSTEM_DESCRIPTOR_H
#define FILE_SYSTEM_DESCRIPTOR_H


#include "Entity.h"

class Descriptor : public Entity {
public:
    static const int NUM_OF_BLOCKS = 3;
    static const int FILE_SIZE_TYPE_SIZE = sizeof(int);
    static const int BLOCK_INDEX_SIZE = sizeof(int);
    static const int SIZE = FILE_SIZE_TYPE_SIZE + BLOCK_INDEX_SIZE * NUM_OF_BLOCKS;
private:
    int file_size;
    int *blocks_indices;

public:
    Descriptor();
    ~Descriptor() override;

    // Define copy and move operations
    Descriptor(Descriptor const &other);
    Descriptor& operator=(Descriptor const &other);
    Descriptor(Descriptor &&other) noexcept;
    Descriptor& operator=(Descriptor &&other) noexcept;

    void parse(const char *bytes) override;
    void copyBytes(char *buffer) const override;
    void clear() override;

    [[nodiscard]] bool isFree() const;
    [[nodiscard]] int getFileSize() const;
    [[nodiscard]] int getBlockIndex(int i) const;
    void setFileSize(int new_file_size);
    void setBlockIndex(int i, int value);
};


#endif //FILE_SYSTEM_DESCRIPTOR_H
