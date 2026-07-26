//
// Created by Nicholas Newdigate on 20/07/2020.
//
#ifndef TEENSY_RESAMPLING_SDREADER_WAVEHEADERPARSER_H
#define TEENSY_RESAMPLING_SDREADER_WAVEHEADERPARSER_H

#include <string>
#include <cstdint>
#include <SD.h>
#include "spi_interrupt.h"

// from https://gist.github.com/Jon-Schneider/8b7c53d27a7a13346a643dac9c19d34f
struct wav_header {
    // RIFF Header
    char riff_header[4] = {0,0,0,0};    // 00 - 03 - Contains "RIFF"
    uint32_t header_chunk_size = 0;  // 04 - 07 - Size of the wav portion of the file, which follows the first 8 bytes. File size - 8
    char wave_header[4] = {0,0,0,0};    // 08 - 11 - Contains "WAVE"

    // "JUNK" Header
    // - a padding sub-chunk may or may not be present before the "fmt " chunk...

    // Format Header
    char fmt_header[4] = {0,0,0,0};     // 12 - 15 - Contains "fmt " (includes trailing space)
    uint32_t fmt_chunk_size = 0;     // 16 - 19 - Should be 16 for PCM
    uint16_t audio_format = 0;     // 20 - 21 - Should be 1 for PCM. 3 for IEEE Float
    uint16_t num_channels = 0;     // 22 - 23
    uint32_t sample_rate = 0;        // 24 - 27
    uint32_t byte_rate = 0;          // 28 - 31
    uint16_t sample_alignment = 0; // 32 - 33
    uint16_t bit_depth  = 0;        // 34 - 35

    wav_header(bool prefilled = false) {
      if (prefilled) {
        riff_header[0] = 'R';
        riff_header[1] = 'I';
        riff_header[2] = 'F';
        riff_header[3] = 'F';
        wave_header[0] = 'W';
        wave_header[1] = 'A';
        wave_header[2] = 'V';
        wave_header[3] = 'E';
        fmt_header[0] = 'f';
        fmt_header[1] = 'm';
        fmt_header[2] = 't';
        fmt_header[3] = ' ';
        fmt_chunk_size = 16;
        audio_format = 1;
        num_channels = 2;
        sample_rate = AUDIO_SAMPLE_RATE;
        sample_alignment = 4;
        byte_rate = sample_rate * sample_alignment;
        bit_depth = 16;
      }
    }
};

struct wav_data_header {
    // Data
    char data_header[4] = {0,0,0,0};    // 36 - 39
    unsigned int data_bytes = 0;// 40 - 43

    wav_data_header(bool prefilled = false) {
      if (prefilled) {
        data_header[0] = 'd';
        data_header[1] = 'a';
        data_header[2] = 't';
        data_header[3] = 'a';
      }
    }
};

// merely a collection of functions
namespace WaveHeaderParser {
    static float getBPMfromAcid(char *buf) {
      /** The acid chunk goes a little something like this:
      **
        [8-byte chunk header]
      ** 4 bytes          'acid'
      ** 4 bytes (int)     length of chunk starting at next byte
      **
        [24-byte chunk]
      ** 4 bytes (int)     type of file:
      **        this appears to be a bit mask,however some combinations
      **        are probably impossible and/or qualified as "errors"
      **
      **        0x01 On: One Shot         Off: Loop
      **        0x02 On: Root note is Set Off: No root
      **        0x04 On: Stretch is On,   Off: Strech is OFF
      **        0x08 On: Disk Based       Off: Ram based
      **        0x10 On: ??????????       Off: ????????? (Acidizer puts that ON)
      **
      ** 2 bytes (short)      root note
      **        if type 0x10 is OFF : [C,C#,(...),B] -> [0x30 to 0x3B]
      **        if type 0x10 is ON  : [C,C#,(...),B] -> [0x3C to 0x47]
      **         (both types fit on same MIDI pitch albeit different octaves, so who cares)
      **
      ** 2 bytes (short)      ??? always set to 0x8000
      ** 4 bytes (float)      ??? seems to be always 0
      ** 4 bytes (int)        number of beats
      ** 2 bytes (short)      meter denominator   //always 4 in SF/ACID
      ** 2 bytes (short)      meter numerator     //always 4 in SF/ACID
      **                      //are we sure about the order?? usually its num/denom
      ** 4 bytes (float)      tempo
      **/
      Serial.println("Parsing Tempo from ACID chunk...");

      float tempo = *(float *)(buf+20);
      return tempo;
    }

    static uint16_t getBPMfromID3(char* buf, size_t len = 1024) {
      Serial.println("Looking for Tempo...");
      uint16_t val = 0;
      size_t idx = 0;

      do {
        if (buf[idx] == 'T') {
          if (   buf[idx+1] == 'B'
              && buf[idx+2] == 'P'
              && buf[idx+3] == 'M')
          {
            break;
          }
        }
      } while (++idx < len);

      if (idx < len) {
        Serial.println("Found TBPM string...\n");
        idx += 10;
        for (size_t i = idx; i < idx + 8; ++i) {
          if (buf[i] >= '0' && buf[i] <= '9') {
            val *= 10;
            val += (buf[i] - '0');
          }
        }
        return val;
      } else
        return 0;
    }

    static bool parseFormatChunk(const char *buffer, wav_header &header);

    static bool readWaveHeaderFromBuffer(const char *buffer, wav_header &header) {
        if (buffer[0] != 'R' || buffer[1] != 'I' || buffer[2] != 'F' || buffer[3] != 'F') {
            Serial.printf("expected RIFF (was '%.4s')\n", buffer);
            return false;
        }
        for (int i=0; i < 4; i++)
            header.riff_header[i] = buffer[i];

        // this is easier, but maybe not portable? endianness, etc.
        header.header_chunk_size = *(uint32_t*)(buffer+4);

        if (buffer[8] != 'W' || buffer[9] != 'A' || buffer[10] != 'V' || buffer[11] != 'E') {
            Serial.printf("expected WAVE (was '%.4s')\n", buffer[8]);
            return false;
        }
        for (int i=0; i < 4; i++)
            header.wave_header[i] = buffer[i+8];

        //return parseFormatChunk(buffer+12, header);
        return true;
    }

    static bool parseFormatChunk(const char *buf, wav_header &header) {
        if (buf[0] != 'f' || buf[1] != 'm' || buf[2] != 't' || buf[3] != ' ') {
            Serial.printf("expected 'fmt ' (was '%.4s')\n", buf[0]);
            return false;
        }
        for (int i=0; i < 4; i++)
            header.fmt_header[i] = buf[i];

        // chunk size should be 16 for standard PCM, but might be 18 or 40 with "extensions"
        /*
        auto fmt_chunk_size = static_cast<unsigned long>(buf[7] << 24 | buf[6] << 16 | buf[5] << 8 | buf[4]);
        if (fmt_chunk_size != 16) {
            Serial.printf("chunk size should be 16 for PCM wave data... (was %d)\n", fmt_chunk_size);
            return false;
        }
        */

        // I don't care about wave format extensions, so we'll ignore that and assume 16
        header.fmt_chunk_size = 16;

        auto audio_format = static_cast<unsigned long>((buf[9] << 8) | buf[8]);
        header.audio_format = audio_format;

        auto num_channels = static_cast<unsigned long>((buf[11] << 8) | buf[10]);
        header.num_channels = num_channels;

        uint32_t sample_rate = static_cast<uint32_t>(buf[15] << 24 | buf[14] << 16 | buf[13] << 8 | buf[12]);
        header.sample_rate = sample_rate;

        uint32_t byte_rate = static_cast<uint32_t>(buf[19] << 24 | buf[18] << 16 | buf[17] << 8 | buf[16]);
        header.byte_rate = byte_rate;

        auto sample_alignment = static_cast<unsigned long>((buf[21] << 8) | buf[20]);
        header.sample_alignment = sample_alignment;

        auto bit_depth = static_cast<unsigned long>(buf[23] << 8 | buf[22]);
        header.bit_depth = bit_depth;

        // extra wave format extensions may exist beyond 24 bytes...

        return true;
    }

    /*
    static bool readWaveHeader(const char *filename, wav_header &header, File &wavFile) {
        char buffer[36];
        int bytesRead = wavFile.read(buffer, 36);
        if (bytesRead != 36) {
            Serial.printf("expected 36 bytes (was %d)\n", bytesRead);
            return false;
        }
        return readWaveHeaderFromBuffer(buffer, header);
    }
    */

    /* returns true for 'data' chunk */
    static bool readChunk(unsigned char *buffer, size_t offset, unsigned &chunkSize) {
        // report chunk size
        chunkSize = static_cast<uint32_t>(buffer[offset+7] << 24 | buffer[offset+6] << 16 | buffer[offset+5] << 8 | buffer[offset+4]);
        chunkSize += 8;

        if (    buffer[offset+0] == 'd' 
             && buffer[offset+1] == 'a' 
             && buffer[offset+2] == 't' 
             && buffer[offset+3] == 'a') {
            return true;
        }
        Serial.println("expected 'data'... skipping chunk");
        return false;
    }

    static bool readDataHeader(unsigned char *buffer, size_t offset, wav_data_header &data_header) {

      for (int i=0; i < 4; i++)
            data_header.data_header[i] = buffer[i+offset];

        if (buffer[offset+0] != 'd' || buffer[offset+1] != 'a' || buffer[offset+2] != 't' || buffer[offset+3] != 'a') {
            Serial.printf("expected data... (was %d)\n", buffer);
            return false;
        }

        auto data_bytes = static_cast<unsigned long>(buffer[offset+7] << 24 | buffer[offset+6] << 16 | buffer[offset+5] << 8 | buffer[offset+4]);
        data_header.data_bytes = data_bytes;
        return true;
    }

} // namespace WaveHeaderParser

#endif //TEENSY_RESAMPLING_SDREADER_WAVEHEADERPARSER_H
