//
// Created by Nicholas Newdigate on 10/02/2019.
//

#ifndef TEENSYAUDIOLIBRARY_RESAMPLINGLFSREADER_H
#define TEENSYAUDIOLIBRARY_RESAMPLINGLFSREADER_H

#include <cstdint>
#include "IndexableLittleFSFile.h"
#include "ResamplingReader.h"
#include "LittleFS.h"
#include "ResamplingSdReader.h"

namespace newdigate {

static constexpr size_t BUFFER_SIZE_LFS = 2048;
static constexpr size_t BUFFER_COUNT_LFS = 7;

class ResamplingLfsReader : public ResamplingReader< IndexableLittleFSFile<BUFFER_SIZE_LFS, BUFFER_COUNT_LFS>, File > {
public:
    ResamplingLfsReader(LittleFS &fs) : 
        ResamplingReader(),
        _myFS(fs) 
    {
    }
    virtual ~ResamplingLfsReader() {
    }

    int16_t getSourceBufferValue(long index) override {
        return (*_sourceBuffer)[index];
    }

    File open(char *filename) override {
        return _myFS.open(filename);
    }

    void close(void) override
    {
        stop();
        _file_samples = 0;
        if (_sourceBuffer != nullptr) {
            _sourceBuffer->close();
            delete _sourceBuffer;
            _sourceBuffer = nullptr;
        }
        if (_filename != nullptr) {
            delete [] _filename;
            _filename = nullptr;
        }
    }

    IndexableLittleFSFile<BUFFER_SIZE_LFS, BUFFER_COUNT_LFS>* createSourceBuffer() override {
        File f = open(_filename);
        return new IndexableLittleFSFile<BUFFER_SIZE_LFS, BUFFER_COUNT_LFS>(_myFS, _filename, f);
    }
    IndexableLittleFSFile<BUFFER_SIZE_LFS, BUFFER_COUNT_LFS>* createSourceBuffer(File& file) override {
        return new IndexableLittleFSFile<BUFFER_SIZE_LFS, BUFFER_COUNT_LFS>(_myFS, _filename, file);
    }

protected:    
    LittleFS &_myFS;
};

}

#endif //TEENSYAUDIOLIBRARY_RESAMPLINGLFSREADER_H
