#include "FileSystem.h"
#include "entities/DirectoryEntry.h"
#include "utils/BlockParser.h"
#include "../io_system/IOSystem.h"
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <io_system/IOUtils.h>


/**TODO*/
FileSystem::FileSystem() {
    init();
}

FileSystem::~FileSystem() {
    for (OFT::Entry &entry : oft.entries) {
        delete[] entry.block;
    }
}

void FileSystem::initOFTEntry(OFT::Entry &entry, int descriptor_index) {
    entry.descriptor_index = descriptor_index;
    loadDescriptor(entry);
    entry.current_position = 0;
    entry.block = new char[IOSystem::BLOCK_SIZE];
    loadBlock(entry, 0);
}

void FileSystem::clearOFTEntry(OFT::Entry &entry) {
    entry.descriptor_index = -1;
    entry.descriptor.clear();
    entry.current_position = -1;
    entry.modified = false;
    delete[] entry.block;
    entry.block = nullptr;
}

bool FileSystem::init(const char *path) {
    closeAllFiles();

    // RESTORE
    std::ifstream fin(path);
    if (fin.is_open()) {
        fin.close();
        IOUtils::restore(io_system, path);

        // open directory
        initOFTEntry(oft.entries[0], 0);
        // cache bitmap
        loadBitmap();

        return true;
    }

    // INITIALIZE
    bitMap.clear();
    // reserve service blocks
    for (int i = 0; i < 7; i++) {
        bitMap.setBitValue(i, true);
    }
    saveBitmap();

    // reset all descriptors in service blocks
    char *descriptor_block = new char[IOSystem::BLOCK_SIZE];
    Descriptor descriptor;
    for (int descriptorIndexInBlock = 0; descriptorIndexInBlock < 4; descriptorIndexInBlock++) {
        descriptor.copyBytes(descriptor_block + descriptorIndexInBlock * Descriptor::SIZE);
    }
    for (int descriptorsBlocksIndex = 5; descriptorsBlocksIndex < 10; descriptorsBlocksIndex++) {
        io_system.writeBlock(descriptorsBlocksIndex, descriptor_block);
    }

    // separately reset directory descriptor
    descriptor.setFileSize(0);
    descriptor.copyBytes(descriptor_block);
    io_system.writeBlock(4, descriptor_block);
    // open directory
    initOFTEntry(oft.entries[0], 0);

    delete[] descriptor_block;
    return false;
}

void FileSystem::save(const char *path) {
    closeAllFiles();
    IOUtils::save(io_system, path);
}

void FileSystem::createFile(const char* file_name) {
    const int descriptors_num = IOSystem::BLOCK_SIZE / Descriptor::SIZE;
    const int directory_entries_num = IOSystem::BLOCK_SIZE / DirectoryEntry::SIZE;

    // if file name is incorrect
    if (strlen(file_name) > DirectoryEntry::MAX_FILE_NAME_SIZE || file_name[0] == '\0'){
        throw std::length_error("File name must contain up to 4 characters.");
    }

    // find a free file descriptor
    int free_descriptor_index = -1;
    char *descriptor_block = new char[IOSystem::BLOCK_SIZE];
    Descriptor descriptor;

    for (int block_index = 4; block_index < 10 && free_descriptor_index == -1; block_index++) {
        io_system.readBlock(block_index, descriptor_block);
        for (int i = 0; i < descriptors_num; i++) {
            descriptor.parse(descriptor_block + i * Descriptor::SIZE);
            if (descriptor.isFree()){
                free_descriptor_index = (block_index - 4) * descriptors_num + i;
                descriptor.setFileSize(0);
                break;
            }
        }
    }

    // if there is no free file descriptor
    if (free_descriptor_index == -1) {
        delete[] descriptor_block;
        throw std::length_error("Cannot create more than 24 files.");
    }

    // find a free directory entry
    int free_directory_entry_index = -1;
    DirectoryEntry directory_entry, current;

    for (int i = 0; i < 3; i++) {
        replaceBlock(oft.entries[0], i);
        for (int j = 0; j < directory_entries_num; j++) {
            current.parse(oft.entries[0].block + j * DirectoryEntry::SIZE);

            if (free_directory_entry_index == -1 && current.isFree()) {
                free_directory_entry_index = j;
                directory_entry.setFileName(file_name);
                directory_entry.setDescriptorIndex(free_descriptor_index);
            } else {
                char file_name2[DirectoryEntry::MAX_FILE_NAME_SIZE + 1];
                current.copyFileName(file_name2);
                if (std::strcmp(file_name2, file_name) == 0) {
                    delete[] descriptor_block;
                    char message[100];
                    std::sprintf(message, "File with name \"%s\" already exists.", file_name);
                    throw std::invalid_argument(message);
                }
            }
        }
    }

    if (free_directory_entry_index == -1) {
        delete[] descriptor_block;
        throw std::length_error("Cannot create more than 24 files.");
    }

    // fill descriptor
    descriptor.copyBytes(descriptor_block + (free_descriptor_index % descriptors_num) * Descriptor::SIZE);
    io_system.writeBlock(free_descriptor_index / descriptors_num + 4, descriptor_block);
    delete[] descriptor_block;

    // fill directory entry
    replaceBlock(oft.entries[0], free_directory_entry_index / 8);
    directory_entry.copyBytes(oft.entries[0].block + free_directory_entry_index * DirectoryEntry::SIZE);
    oft.entries[0].modified = true;
}

void FileSystem::destroyFile(const char* file_name) {
    const int descriptors_num = IOSystem::BLOCK_SIZE / Descriptor::SIZE;
    const int directory_entries_num = IOSystem::BLOCK_SIZE / DirectoryEntry::SIZE;

    // if file name is incorrect
    if (strlen(file_name) > DirectoryEntry::MAX_FILE_NAME_SIZE || file_name[0] == '\0'){
        throw std::length_error("File name must contain up to 4 characters.");
    }

    // find the file descriptor by searching the directory
    // find a directory entry
    bool found = false;
    int descriptor_index = -1;

    for (int i = 0; i < 3 && !found; i++) {
        replaceBlock(oft.entries[0], i);
        for (int j = 0; j < directory_entries_num; j++) {
            DirectoryEntry directory_entry;
            directory_entry.parse(oft.entries[0].block + j * 8);

            if (!directory_entry.isFree()) {
                char file_name2[DirectoryEntry::MAX_FILE_NAME_SIZE + 1];
                directory_entry.copyFileName(file_name2);
                if (std::strcmp(file_name2, file_name) == 0) {
                    found = true;
                    descriptor_index = directory_entry.getDescriptorIndex();
                    //remove the directory entry
                    directory_entry.clear();
                    directory_entry.copyBytes(oft.entries[0].block + j * 8);
                    oft.entries[0].modified = true;
                    break;
                }
            }
        }
    }

    // if file was not found
    if (!found) {
        char message[100];
        std::sprintf(message, "File with name \"%s\" does not exist.", file_name);
        throw std::invalid_argument(message);
    }

    int oft_index = 1;
    while (oft.entries[oft_index].descriptor_index != descriptor_index && oft_index < 4) {
        oft_index++;
    }

    Descriptor descriptor;

    // if file wasn't opened
    if (oft_index == 4) {
        char *descriptor_block = new char[IOSystem::BLOCK_SIZE];
        io_system.readBlock(descriptor_index / descriptors_num + 4, descriptor_block);

        // free the file descriptor
        descriptor.copyBytes(descriptor_block + (descriptor_index % descriptors_num) * Descriptor::SIZE);
        io_system.writeBlock(descriptor_index / descriptors_num + 4, descriptor_block);
        delete[] descriptor_block;
    } else {
        OFT::Entry* entry = &oft.entries[oft_index];
        descriptor = entry->descriptor;
        entry->descriptor.clear();
        // save clean descriptor on disk
        saveDescriptor(*entry);
        entry->reserved_block_index = -1;
        // reset modification bit, in order that cached block doesn't get saved when closing file
        entry->modified = false;
        // reset OFT::Entry fields
        close(oft_index);
    }

    // update the bitmap to reflect the freed blocks
    for (int i = 0; i < Descriptor::NUM_OF_BLOCKS && descriptor.getBlockIndex(i) != -1; i++) {
        bitMap.resetBit(descriptor.getBlockIndex(i));
    }
    // save changes to disk (write-through caching)
    saveBitmap();
}

int FileSystem::open(const char *file_name) {
    int descriptor_index = -1;
    char* directory_entry_mem = new char[DirectoryEntry::SIZE];
    DirectoryEntry directory_entry;

    doSeek(oft.entries[0], 0);

    while (doRead(oft.entries[0], directory_entry_mem, DirectoryEntry::SIZE) == DirectoryEntry::SIZE) {
        directory_entry.parse(directory_entry_mem);

        char file_name2[DirectoryEntry::MAX_FILE_NAME_SIZE + 1];
        directory_entry.copyFileName(file_name2);
        if (std::strcmp(file_name2, file_name) == 0) {
            descriptor_index = directory_entry.getDescriptorIndex();
            break;
        }
    }

    delete[] directory_entry_mem;

    if (descriptor_index == -1) {
        char message[100];
        std::sprintf(message, "File with name \"%s\" doesn't exist.", file_name);
        throw std::invalid_argument(message);
    }

    int oft_index = 1;
    while (oft.entries[oft_index].descriptor_index != -1 && oft_index < 4) {
        oft_index++;
    }

    if (oft_index == 4) {
        throw std::length_error("Can't open one more file.");
    } else {
        initOFTEntry(oft.entries[oft_index], descriptor_index);
    }

    return oft_index;
}

void FileSystem::close(int index) {
    checkOFTIndex(index);
    OFT::Entry* entry = &oft.entries[index];

    if (entry->modified) {
        saveBlock(*entry);
    } else if (entry->reserved_block_index != -1) {
        freeReservation(*entry);
    }
    clearOFTEntry(*entry);
}

int FileSystem::read(int index, char *mem_area, int count) {
    checkOFTIndex(index);
    OFT::Entry* entry = &oft.entries[index];

    return doRead(*entry, mem_area, count);
}

int FileSystem::doRead(OFT::Entry &entry, char* mem_area, int count) {
    int file_size = entry.descriptor.getFileSize();
    int bytes_read = 0;
    int shift = entry.current_position % IOSystem::BLOCK_SIZE;
    int i = 0;

    while (true) {
        for (; i + shift < IOSystem::BLOCK_SIZE && i + shift < file_size && i < count; i++) {
            mem_area[i] = entry.block[i + shift];
            bytes_read++;
            entry.current_position++;
        }

        // if whole block was processed and file hasn't reached its max size, do read-ahead
        if (i + shift == IOSystem::BLOCK_SIZE && entry.current_position != MAX_FILE_SIZE) {
            entry.current_position--;
            replaceBlock(entry, entry.current_position / IOSystem::BLOCK_SIZE);
        }

        if (i == count) {
            break;
        }
        i = 0;
        shift = 0;
    }

    return bytes_read;
}

int FileSystem::write(int index, const char *mem_area, int count) {
    checkOFTIndex(index);
    OFT::Entry* entry = &oft.entries[index];

    return doWrite(*entry, mem_area, count);
}

int FileSystem::doWrite(OFT::Entry &entry, const char* mem_area, int count) {
    int file_size = entry.descriptor.getFileSize();
    int bytes_read = 0;
    int shift = entry.current_position % IOSystem::BLOCK_SIZE;
    int i = 0;

    while (true) {
        entry.modified = true;
        // if current block is reserved, then use it and release reservation
        if (shift == 0 && entry.current_position == file_size) {
            saveDescriptor(entry);
            entry.reserved_block_index = -1;
        }

        for (; i + shift < IOSystem::BLOCK_SIZE && i + shift < file_size && i < count; i++) {
            entry.block[i + shift] = mem_area[i];
            bytes_read++;
            entry.current_position++;
            if (entry.current_position > file_size) {
                file_size++;
            }
        }

        // if whole block was processed and file hasn't reached its max size, do read-ahead
        if (i + shift == IOSystem::BLOCK_SIZE && entry.current_position != MAX_FILE_SIZE) {
            entry.current_position--;
            replaceBlock(entry, entry.current_position / IOSystem::BLOCK_SIZE);
        }

        if (i == count) {
            entry.descriptor.setFileSize(file_size);
            break;
        }
        i = 0;
        shift = 0;
    }

    return bytes_read;
}

void FileSystem::lseek(int index, int pos) {
    checkOFTIndex(index);
    OFT::Entry* entry = &oft.entries[index];

    if (pos > entry->descriptor.getFileSize()) {
        char message[100];
        std::sprintf(message, "Operation unsuccessful: file is out of bounds. File size: %d bytes.",
                     entry->descriptor.getFileSize());
        throw std::out_of_range(message);
    }

    doSeek(*entry, pos);
}

void FileSystem::doSeek(OFT::Entry &entry, int pos) {
    int new_block_oft_index = pos / IOSystem::BLOCK_SIZE;
    int current_block_oft_index = entry.current_position / IOSystem::BLOCK_SIZE;

    // if we are at the end of file, then we are on the last possible block
    if (current_block_oft_index == Descriptor::NUM_OF_BLOCKS) {
        current_block_oft_index--;
    }
    if (new_block_oft_index != current_block_oft_index) {
        replaceBlock(entry, new_block_oft_index);
    }

    entry.current_position = pos;
}

std::vector<std::string> FileSystem::directory() const {
    std::vector<std::string> filenames;
    char *block = new char[64];

    DirectoryEntry directory_entries[8];
    Descriptor descriptors[4];
    int descriptor_block_index = 4;

    io_system.readBlock(descriptor_block_index, block);
    BlockParser::parseBlock(block, descriptors);

    for (int i = 1, j; i < 4; i++) {
        io_system.readBlock(i, block);
        BlockParser::parseBlock(block, directory_entries);

        for (DirectoryEntry &directory_entry : directory_entries) {
            if (!directory_entry.isFree()) {
                char file_name[DirectoryEntry::MAX_FILE_NAME_SIZE + 1];
                directory_entry.copyFileName(file_name);
                int descriptor_index = directory_entry.getDescriptorIndex();
                j = descriptor_index % 4;

                // if descriptor doesn't belong to currently cached block
                // (then it necessarily belongs to one of the next blocks)
                if (descriptor_index / 4 != descriptor_block_index - 4) {
                    descriptor_block_index = descriptor_index / 4 + 4;
                    io_system.readBlock(descriptor_block_index, block);
                    BlockParser::parseBlock(block, descriptors);
                }

                std::string file_name_size(file_name);
                file_name_size.push_back(' ');
                file_name_size += std::to_string(descriptors[j].getFileSize());
                filenames.push_back(file_name_size);
            }
        }
    }

    delete[] block;
    return filenames;
}


void FileSystem::checkOFTIndex(int index) const {
    if (index < 1 || index > 3) {
        char message[100];
        std::sprintf(message, "Invalid index. File index must be an integer from 1 to 3. Provided: %d.", index);
        throw std::out_of_range(message);
    } else if (oft.entries[index].descriptor_index == -1) {
        char message[100];
        std::sprintf(message, "File with index %d wasn't opened.", index);
        throw std::invalid_argument(message);
    }
}

void FileSystem::loadDescriptor(OFT::Entry &entry) {
    char *descriptors_block = new char[IOSystem::BLOCK_SIZE];
    int shift = (entry.descriptor_index % 4) * Descriptor::SIZE;

    io_system.readBlock(entry.descriptor_index / 4 + 4, descriptors_block);
    entry.descriptor.parse(descriptors_block + shift);

    delete[] descriptors_block;
}

void FileSystem::saveDescriptor(OFT::Entry const &entry) {
    char *block = new char[IOSystem::BLOCK_SIZE];
    int block_index = entry.descriptor_index / 4 + 4;
    int shift = (entry.descriptor_index % 4) * Descriptor::SIZE;

    io_system.readBlock(block_index, block);
    entry.descriptor.copyBytes(block + shift);
    io_system.writeBlock(block_index, block);
    delete[] block;
}

void FileSystem::loadBitmap() {
    char *bitmap_block = new char[IOSystem::BLOCK_SIZE];
    io_system.readBlock(0, bitmap_block);
    bitMap.parse(bitmap_block);
    delete[] bitmap_block;
}

void FileSystem::saveBitmap() {
    char *block = new char[IOSystem::BLOCK_SIZE];
    bitMap.copyBytes(block);
    io_system.writeBlock(0, block);
    delete[] block;
}

int FileSystem::reserveBlock(OFT::Entry &entry) {
    // find first free position in descriptor
    int new_block_oft_index = -1;
    for (int i = 0; i < Descriptor::NUM_OF_BLOCKS; i++) {
        if (entry.descriptor.getBlockIndex(i) == -1) {
            new_block_oft_index = i;
            break;
        }
    }
    if (new_block_oft_index == -1) {
        throw std::out_of_range("Allocation impossible. Maximal number of blocks per file is reached.");
    }

    // find first free position in bitmap
    int absolute_block_index = bitMap.findZeroBit();
    if (absolute_block_index == -1) {
        throw std::length_error("Cannot allocate memory for file. Disk is full.");
    }
    bitMap.setBit(absolute_block_index);
    saveBitmap();
    entry.descriptor.setBlockIndex(new_block_oft_index, absolute_block_index);

    return absolute_block_index;
}

void FileSystem::freeReservation(OFT::Entry &entry) {
    int absolute_block_index = entry.descriptor.getBlockIndex(entry.reserved_block_index);
    bitMap.resetBit(absolute_block_index);
    saveBitmap();
    entry.descriptor.setBlockIndex(entry.reserved_block_index, -1);
    entry.reserved_block_index = -1;
}

void FileSystem::loadBlock(OFT::Entry &entry, int relative_block_index) {
    int absolute_block_index = entry.descriptor.getBlockIndex(relative_block_index);

    if (absolute_block_index == -1) {
        // read-ahead with block reservation
        absolute_block_index = reserveBlock(entry);
        entry.reserved_block_index = relative_block_index;
    }

    io_system.readBlock(absolute_block_index, entry.block);
    // set position at the beginning of the block
    entry.current_position = IOSystem::BLOCK_SIZE * relative_block_index;
}

void FileSystem::saveBlock(OFT::Entry const &entry) {
    if (entry.descriptor.getFileSize() == 0) {
        throw std::invalid_argument("No block to save");
    }

    // save file block
    int relative_block_index = entry.current_position / 64;
    int absolute_block_index = entry.descriptor.getBlockIndex(relative_block_index);
    io_system.writeBlock(absolute_block_index, entry.block);
    // save cached descriptor
    saveDescriptor(entry);
}

void FileSystem::replaceBlock(OFT::Entry &entry, int relative_block_index) {
    // save current block only if modified
    if (entry.modified) {
        saveBlock(entry);
        entry.modified = false;
    } else if (entry.reserved_block_index != -1) {
        freeReservation(entry);
    }
    loadBlock(entry, relative_block_index);
}

void FileSystem::closeAllFiles() {
    int oft_index = 1;
    while (oft_index < 4) {
        if (oft.entries[oft_index].descriptor_index != -1) {
            close(oft_index);
        }
        oft_index++;
    }

    if (oft.entries[0].modified) {
        saveBlock(oft.entries[0]);
    }
}