//
// Created by Nicholas Newdigate on 10/02/2019.
//

#ifndef TEENSYAUDIOLIBRARY_RESAMPLINGSERIALFLASHREADER_H
#define TEENSYAUDIOLIBRARY_RESAMPLINGSERIALFLASHREADER_H

#include <cstdint>
#include "spi_interrupt.h"
#include "loop_type.h"
#include "interpolation.h"
#include "IndexableSerialFlashFile.h"
#include "ResamplingReader.h"
#include "SerialFlash.h"

namespace newdigate {

static constexpr size_t BUFFER_SIZE_SERIALFLASH = 128;
static constexpr size_t BUFFER_COUNT_SERIALFLASH = 2;

class ResamplingSerialFlashReader : public ResamplingReader< IndexableSerialFlashFile<BUFFER_SIZE_SERIALFLASH, BUFFER_COUNT_SERIALFLASH>, SerialFlashFile > {
public:
    ResamplingSerialFlashReader(SerialFlashChip &fs) : 
        ResamplingReader(),
        _myFS(fs) 
    {
    }

    virtual ~ResamplingSerialFlashReader() {
    }

    int16_t getSourceBufferValue(long index) override {
        return (*_sourceBuffer)[index];
    }

    SerialFlashFile open(char *filename) override {
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

    IndexableSerialFlashFile<BUFFER_SIZE_SERIALFLASH, BUFFER_COUNT_SERIALFLASH>* createSourceBuffer() override {
        SerialFlashFile f = open(_filename);
        return new IndexableSerialFlashFile<BUFFER_SIZE_SERIALFLASH, BUFFER_COUNT_SERIALFLASH>(_myFS, _filename, f);
    }

    IndexableSerialFlashFile<BUFFER_SIZE_SERIALFLASH, BUFFER_COUNT_SERIALFLASH>* createSourceBuffer(SerialFlashFile& file) override {
        return new IndexableSerialFlashFile<BUFFER_SIZE_SERIALFLASH, BUFFER_COUNT_SERIALFLASH>(_myFS, _filename, file);
    }

protected:    
    SerialFlashChip &_myFS;
};

}

#endif //TEENSYAUDIOLIBRARY_RESAMPLINGSERIALFLASHREADER_H
