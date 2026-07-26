#ifndef TEENSYAUDIOLIBRARY_RESAMPLINGREADER_H
#define TEENSYAUDIOLIBRARY_RESAMPLINGREADER_H

#include <Arduino.h>
#include <cstdint>
#include "loop_type.h"
#include "interpolation.h"
#include "waveheaderparser.h"

namespace newdigate {

template<class TArray, class TFile>
class ResamplingReader {
public:

    constexpr uint32_t B2M() {
      return (uint32_t)((double)4294967296000.0 / double(_file_sample_rate) / 2.0);
      // 97352592 when sample rate is 44.1kHz
    }

    enum PlayState {
        STOPPED = 0,
        PLAYING = 1,
        PAUSED, // loaded and parsed, but not playing
    };

    ResamplingReader() {
    }
    virtual ~ResamplingReader() {       
    }

    virtual TFile open(char *filename) = 0;
    virtual TArray* createSourceBuffer() = 0;
    virtual TArray* createSourceBuffer(TFile& file) {return nullptr;};
    virtual int16_t getSourceBufferValue(long index) = 0;
    virtual void close(void) = 0;
    virtual void retrig_process(void) { };

    void begin(void) 
    {
        if (_interpolationType != ResampleInterpolationType::resampleinterpolation_none) {
            initializeInterpolationPoints();
        }
        _play_state = STOPPED;
        _crossfade = 0.0;
        if (_play_start == play_start::play_start_sample)
            _bufferPosition1 = _header_offset;
        else
            _bufferPosition1 = _header_offset + _loop_start;
        _file_size = 0;
        _file_samples = 0;
    }

    // length is in samples
    bool playRaw(TArray *array, uint32_t length, uint16_t numChannels)
    {
        _sourceBuffer = array;
        stop();

        _header_offset = 0;
        _file_samples = length;
        _file_size = length * 2 * numChannels;
        _loop_start = 0;
        _loop_finish = length;
        setNumChannels(numChannels);

        reset();
        _play_state = PLAYING;
        return true;
    }

    bool playRaw(TArray *array, uint16_t numChannels) {
        return playRaw(array, false, numChannels); 
    }

    bool playWav(TArray *array, uint32_t length) {
        return playRaw(array, true); 
    }

    bool playWav(TArray *array) {
        bool result = false;
        
        do
        {
            wav_header hdr;
            unsigned infoTagsSize;
            wav_data_header wav_data_hdr;           
            
            if (!WaveHeaderParser::readWaveHeaderFromBuffer((char*) array, hdr))
                break;
            
            // make unwarranted assumptions about the header format
            // by using a magic number...
            if (!WaveHeaderParser::readChunk((unsigned char*) array, 36, infoTagsSize))
                break;
            
            if (!WaveHeaderParser::readDataHeader((unsigned char*) array, 36 + infoTagsSize, wav_data_hdr))
                break;
                        
            result = playRaw((TArray*)((char*) array + (36 + infoTagsSize + sizeof wav_data_hdr)), 
                             wav_data_hdr.data_bytes / hdr.num_channels / 2, 
                             hdr.num_channels); 
        } while (0);
        
        return result;
    }

    bool play(const char *filename, bool isWave, uint16_t numChannelsIfRaw = 0, bool startPaused = true)
    {
        close();

        if (!isWave) // if raw file, then hardcode the numChannels as per the parameter
            setNumChannels(numChannelsIfRaw);

        _filename = new char[strlen(filename)+1] {0};
        memcpy(_filename, filename, strlen(filename) + 1);

        TFile file = open(_filename);
		
        if (!file) {
            //Serial.printf("Not able to open file: %s\n", _filename);
            if (_filename) delete [] _filename;
            _filename = nullptr;
            return false;
        }

        _tempo_bpm = 0.0;
        _file_size = file.size();
        if (isWave) {
            wav_header wav_header;
            wav_data_header data_header;

            // first 12 bytes should be "RIFF", size, "WAVE"
            char buffer[32];
            size_t bytesRead = file.read(buffer, 12);
            if (bytesRead < 12) {
                //Serial.println("premature EOF!");
                close();
                return false;
            }
            if ( !WaveHeaderParser::readWaveHeaderFromBuffer((const char *) buffer, wav_header) ) {
                close();
                return false;
            }

            // read in next chunk header
            bytesRead = file.read(buffer, 8);
            if (bytesRead != 8) return false;

            unsigned dataChunkOffset = 12, chunkSize = 0;
            while (!WaveHeaderParser::readChunk((unsigned char *)buffer, 0, chunkSize))
            {
                if (   buffer[0] == 'f'
                    && buffer[1] == 'm'
                    && buffer[2] == 't'
                    && buffer[3] == ' ' )
                {
                  //Serial.printf("Found 'fmt ' chunk!");
                  // found the format header, need 16 more bytes
                  bytesRead = file.read(buffer+8, 16);
                  WaveHeaderParser::parseFormatChunk(buffer, wav_header);

                  if (wav_header.bit_depth != 16) {
                      //Serial.printf("Needs 16 bit audio! Aborting.... (got %d)", wav_header.bit_depth);
                      return false;
                  }
                  setNumChannels(wav_header.num_channels);
                  _file_sample_rate = wav_header.sample_rate;
                } else if (
                        buffer[0] == 'i'
                     && buffer[1] == 'd'
                     && buffer[2] == '3' )
                {
                  //Serial.println("Found 'id3' chunk");

                  size_t sz = 512;
                  char id3buf[sz];
                  sz = file.read(id3buf, chunkSize > sz ? sz : chunkSize);
                  uint16_t tempo = WaveHeaderParser::getBPMfromID3(id3buf, sz);
                  if (tempo) _tempo_bpm = tempo;
                } else if (
                       buffer[0] == 'a'
                    && buffer[1] == 'c'
                    && buffer[2] == 'i'
                    && buffer[3] == 'd' )
                {
                  //Serial.printf("Found 'acid' chunk, size=%u\n", chunkSize);
                  // acid chunk should always be 24 bytes, excluding 8-byte header
                  bytesRead = file.read(buffer, 24);
                  if (24 == bytesRead) {
                    float tempo = WaveHeaderParser::getBPMfromAcid(buffer);
                    if (tempo) _tempo_bpm = tempo;
                  }
                }

                // other chunks, including "JUNK" are simply ignored

                //Serial.printf("Done checking chunk, size %u bytes\n", chunkSize);
                dataChunkOffset += chunkSize; // includes 8-byte header size

                // read in next chunk header
                file.seek(dataChunkOffset);
                bytesRead = file.read(buffer, 8);
                if (bytesRead != 8) return false;
            }
            //Serial.printf("Found 'data' chunk at %u, size: %u bytes", dataChunkOffset, chunkSize);

            if (!_tempo_bpm) {
              // check for metadata after the data chunk
              unsigned afterData = dataChunkOffset + chunkSize;
              do {
                file.seek(afterData);
                bytesRead = file.read(buffer, 8);
                if (bytesRead != 8) break;

                if (    buffer[0] == 'i'
                     && buffer[1] == 'd'
                     && buffer[2] == '3' )
                {
                  //Serial.println("Found 'id3' chunk after 'data'");

                  size_t sz = 512;
                  char id3buf[sz];
                  sz = file.read(id3buf, chunkSize > sz ? sz : chunkSize);
                  uint16_t tempo = WaveHeaderParser::getBPMfromID3(id3buf, sz);
                  if (tempo) _tempo_bpm = tempo;

                  break;
                }

                afterData += static_cast<uint32_t>(buffer[7] << 24 | buffer[6] << 16 | buffer[5] << 8 | buffer[4]);
                afterData += 8;
              } while (afterData < _file_size);
            }

            // jump back to data chunk
            file.seek(dataChunkOffset);
            bytesRead = file.read(buffer, 8);
            if (bytesRead != 8) return false;

            if (!WaveHeaderParser::readDataHeader((unsigned char *)buffer, 0, data_header)) {
                //Serial.println("Not able to read header! Aborting...");
                return false;
            }

            _header_offset = (8 + dataChunkOffset) / 2;
            _file_samples = ((data_header.data_bytes) / 2);
        } else
            _file_samples = _file_size / 2;

        if (_file_size <= _header_offset * sizeof(int16_t)) {
            file.close();
            _play_state = STOPPED;
            if (_filename) delete [] _filename;
            _filename =  nullptr;
            //Serial.printf("Wave file contains no samples: %s\n", filename);
            return false;
        }

        _file_samples /= _numChannels;
        _loop_start = 0;
        _loop_finish = _file_samples;

        _sourceBuffer = createSourceBuffer(file);
        setLoopType(_loopType);
        setLoopStart(/*_samples_to_start*/(_loop_start));
        setLoopFinish(/*_samples_to_start*/(_loop_finish));
        reset(); // sets _bufferPosition1 ready for playback

        _play_state = startPaused ? PAUSED : PLAYING;
        return true;
    }
	
    size_t preLoadBuffers(int firstSample, bool bufferInPSRAM, bool forwards = true)
    {			
        return _sourceBuffer->preLoadBuffers(firstSample, bufferInPSRAM, forwards);
    }
	
	size_t getBufferSize(void) { return _sourceBuffer?_sourceBuffer->getBufferSize():-1; }
	void resetStatus(void) { if (_sourceBuffer) _sourceBuffer->resetStatus(); }
	void getStatus(char* buf) { if (_sourceBuffer) _sourceBuffer->getStatus(buf); }
	void triggerReload(void) { if (_sourceBuffer) _sourceBuffer->triggerReload(_playbackRate); }

    bool playRaw(const char *filename, uint16_t numChannelsIfRaw){
        return play(filename, false, numChannelsIfRaw);
    }

    bool playWav(const char *filename){
        return play(filename, true);
    }

    bool play()
    {
        if (isPlaying()) {
            stop();
        }
        if (available()) {
            reset();
            _play_state = PLAYING;
            return true;
        }
        return false;
    }
    void stop(void)
    {
        _play_state = STOPPED;
    }

    bool isPlaying(void) { return PLAYING == _play_state; }

    unsigned int read(void **buf, uint16_t nsamples) {
        if (PLAYING != _play_state) return 0;

        int16_t *index[_numChannels];
        unsigned int count = 0;

        for (int channel=0; channel < _numChannels; channel++) {
            index[channel] = (int16_t*)buf[channel];
        }

        while (count < nsamples) 
		{
			// only needs doing once per sample, not repeatedly for every channel
			if (_useDualPlaybackHead)
				_updateCrossfade();

			// get a new sample for each channel
            for (int channel=0; channel < _numChannels; channel++) 
			{
				bool readOK = false;
				
				while (!readOK)
				{
					readOK = readNextValue(index[channel], channel);
					
					if (readOK) 
					{
						if (channel == _numChannels - 1)
							count++;
						index[channel]++;
					}
					else 
					{
						// we have reached the end of the file: this is true for all channels
						// so the early return if we're not looping doesn't fail to load
						// data that it should
						_crossfadeState = 0;
						switch (_loopType) 
						{
							case looptype_repeat:
							{
								if (_playbackRate >= 0.0) 
									_bufferPosition1 = _samples_to_start(_loop_start);
								else
									_bufferPosition1 = _samples_to_start(_loop_finish - _numChannels);
								break;
							}

							case looptype_pingpong:
							{
								if (_playbackRate >= 0.0) {
									_bufferPosition1 = _samples_to_start(_loop_finish - _numChannels);
								}
								else {
									if (_play_start == play_start::play_start_sample)
										_bufferPosition1 = _header_offset;
									else
										_bufferPosition1 = _samples_to_start(_loop_start);
								}
								_playbackRate = -_playbackRate;
								break;
							}            

							case looptype_none:            
							default:
							{
								//Serial.printf("end of loop...\n");
								/* no looping - return the number of (resampled) bytes returned... */
								// close(); // can't close(), called from update interrupt!
								return count;
							}
						}   
					}
				}
			}		
			
			// All channels done: step buffer positions on
            _remainder += _playbackRate;

            auto delta = static_cast<signed int>(_remainder);
            _remainder -= static_cast<double>(delta);
            _bufferPosition1 += (delta * _numChannels);
			if (0 != _crossfadeState)  // doing a cross-fade
			{
				if (_loopType == loop_type::looptype_pingpong) // bufferPosition2 is going opposite to bufferPosition1
					_bufferPosition2 -= (delta * _numChannels);
				else
					_bufferPosition2 += (delta * _numChannels);
			}
        }
        return count;
    }

private:	
	void _updateCrossfade(void)
	{           
		if (_playbackRate >= 0.0) {
    // read the sample value for given channel and store it at the location pointed to by the pointer 'value'
			if (_crossfadeState == 0 && _bufferPosition1 >= _samples_to_start(_loop_finish - _crossfadeDurationInSamples)) {
				_bufferPosition2 = _loopType == loop_type::looptype_pingpong
											?_loop_finish
											:_loop_start;
				_bufferPosition2 = _samples_to_start(_bufferPosition2);
				_crossfadeState = 1;
			}
			
			//*/
		} else {
			// See if playback has reached start of crossfade zone
			if (_crossfadeState == 0 && _bufferPosition1 <= _samples_to_start(_loop_start + _crossfadeDurationInSamples)) {
				_bufferPosition2 = _loopType == loop_type::looptype_pingpong
											?_loop_start
											:_loop_finish;
				_bufferPosition2 = _samples_to_start(_bufferPosition2);
				_crossfadeState = 1;
			}
		}
		
		// Compute or terminate crossfade
		if (_crossfadeState == 1) 
		{
			if ((_loopType == loop_type::looptype_pingpong) != (_playbackRate > 0.0)) // bufferPosition2 is going forwards from start
			{
				if (_bufferPosition2 > _samples_to_start(_loop_start + _crossfadeDurationInSamples))
				{
					_bufferPosition1 = _bufferPosition2;
					_crossfadeState = 0; // stop crossfade
				}
				else // amount of audio from _bufferPosition1 to use						
					_crossfade = 1.0 - ((_bufferPosition2 - _samples_to_start(_loop_start) ) / _numChannels / static_cast<double>(_crossfadeDurationInSamples));
			}
			else // bufferPosition2 is going backwards from finish
			{
				if (_bufferPosition2 < _samples_to_start(_loop_finish - _crossfadeDurationInSamples))
				{
					_bufferPosition1 = _bufferPosition2;
					_crossfadeState = 0; // stop crossfade
				}
				else // amount of audio from _bufferPosition1 to use						
					_crossfade = 1.0 - ((_samples_to_start(_loop_finish) - _bufferPosition2) / _numChannels / static_cast<double>(_crossfadeDurationInSamples));
			}
			
			// We never cause the caller to reverse playback when using 
			// crossfade, so we have to do it ourselves here
			if (0 == _crossfadeState && _loopType == loop_type::looptype_pingpong)
				_playbackRate = -_playbackRate;
		}
	}

    // read the sample value for given channel and store it at the location pointed to by the pointer 'value'
    bool readNextValue(int16_t *value, uint16_t channel) {
        if (!_useDualPlaybackHead) {
            if (_playbackRate >= 0 ) {
                // forward playback ...
                if (looptype_none == _loopType) // ...not looping
				{
					if (_bufferPosition1 >=  _samples_to_start(_file_samples) )
						return false;
				}
				else // ... looping
				{
					if (_bufferPosition1 >=  _samples_to_start(_loop_finish) )
						return false;
				}
            } else if (_playbackRate < 0) {
                // reverse playback
                if (_play_start == play_start::play_start_sample) {
                    if (_bufferPosition1 < _header_offset)
                        return false;
                } else {
                    if (_bufferPosition1 < _samples_to_start(_loop_start)) 
                        return false;    
                }
            }
        }

        int16_t result = 0, resx = -1;
        if (!_useDualPlaybackHead || _crossfadeState == 0) {
			resx = _bufferPosition1+channel;
            result =  _getSourceBufferValue(_bufferPosition1, 0, channel);
        } else if (_crossfade == 0.0) {
            result =  _getSourceBufferValue(_bufferPosition2, 0, channel);
        } else{
            int result1 =  _getSourceBufferValue(_bufferPosition1, 0, channel);
            int result2 =  _getSourceBufferValue(_bufferPosition2, 0, channel);
            result = ((1.0 - _crossfade ) * result2) + ((_crossfade) * result1);
        }

        if (_interpolationType == ResampleInterpolationType::resampleinterpolation_linear) {
            double abs_remainder = fabs(_remainder);
            if (abs_remainder > 0.0) {

                if (_playbackRate > 0) {
                    if (_remainder - _playbackRate < 0.0){
                        // we crossed over a whole number, make sure we update the samples for interpolation
                        if (!_useDualPlaybackHead) {
                            if ( _numInterpolationPoints[channel] < 2 &&_playbackRate > 1.0 && _bufferPosition1 - _numChannels > _header_offset * 2 ) {
                                // need to update last sample
                                _interpolationPoints[channel][1].y = _getSourceBufferValue(_bufferPosition1, -1, channel);
                            }
                        }
						_addInterpolationPoint(channel,-resx,result);
                    }
                } 
                else if (_playbackRate < 0) {
                    if (_remainder - _playbackRate > 0.0){
                        // we crossed over a whole number, make sure we update the samples for interpolation
                        if (!_useDualPlaybackHead) {
                            if (_numInterpolationPoints[channel] < 2  && _playbackRate < -1.0) {
                                // need to update last sample
                                _interpolationPoints[channel][1].y = _getSourceBufferValue(_bufferPosition1, 1, channel);
                            }
                        }
						_addInterpolationPoint(channel,-resx,result);
                    }
                }

                if (_numInterpolationPoints[channel] > 1) {
                    result = abs_remainder * _interpolationPoints[channel][1].y + (1.0 - abs_remainder) * _interpolationPoints[channel][0].y;
                }
            } else {
				_addInterpolationPoint(channel,-resx,result);				
                result =_interpolationPoints[channel][0].y;
            }
        } 
        else if (_interpolationType == ResampleInterpolationType::resampleinterpolation_quadratic) {
            double abs_remainder = fabs(_remainder);
            if (true || abs_remainder > 0.0) {
                if (_playbackRate > 0) {                
                    if (_remainder - _playbackRate < 0.0){
                        // we crossed over a whole number, make sure we update the samples for interpolation
                        int numberOfSamplesToUpdate = - floor(_remainder - _playbackRate);
                        if (numberOfSamplesToUpdate > 4) 
                            numberOfSamplesToUpdate = 4; // if playbackrate > 4, only need to pop last 4 samples
                        for (int i=numberOfSamplesToUpdate; i > 0; i--) {
							int16_t y = _getSourceBufferValue(_bufferPosition1, 1-i, channel);
							
							if (_useDualPlaybackHead && 0 != _crossfadeState)
								y = y * _crossfade
									+ _getSourceBufferValue(_bufferPosition2, _loopType == loop_type::looptype_pingpong?(i-1):(1-i), channel) * (1.0 - _crossfade);
									
							_addInterpolationPoint(channel,_bufferPosition1 + (1-i)*_numChannels+channel,y);
							
                            }
                        }
                } 
                else if (_playbackRate < 0) {                
                    if (_remainder - _playbackRate > 0.0){
                        // we crossed over a whole number, make sure we update the samples for interpolation
                        int numberOfSamplesToUpdate =  ceil(_remainder - _playbackRate);
                        if (numberOfSamplesToUpdate > 4) 
                            numberOfSamplesToUpdate = 4; // if playbackrate > 4, only need to pop last 4 samples
                        for (int i=numberOfSamplesToUpdate; i > 0; i--) {
							int16_t y = _getSourceBufferValue(_bufferPosition1, i-1, channel);
							
							if (_useDualPlaybackHead && 0 != _crossfadeState)
								y = y * _crossfade
									+ _getSourceBufferValue(_bufferPosition2, _loopType == loop_type::looptype_pingpong?(1-i):(i-1), channel) * (1.0 - _crossfade);
									
							_addInterpolationPoint(channel,_bufferPosition1 + (i-1)*_numChannels+channel,y);
                            }
                        }
                    }
                
                if (_numInterpolationPoints[channel] >= 4) {
                    //int16_t interpolation = interpolate(_interpolationPoints, 1.0 + abs_remainder, 4);
                    int16_t interpolation 
                        = fastinterpolate(
                            _interpolationPoints[channel][0].y, 
                            _interpolationPoints[channel][1].y, 
                            _interpolationPoints[channel][2].y, 
                            _interpolationPoints[channel][3].y, 
                            1.0 + abs_remainder); 
                    result = interpolation;
                    //Serial.printf("[%f]\n", interpolation);
                } else 
                    result = 0;
            } else {
                int numberOfSamplesToUpdate = (_playbackRate < 0.0)? ceil(_playbackRate) : floor(_playbackRate);
                if (numberOfSamplesToUpdate > 4) 
                    numberOfSamplesToUpdate = 4; // if playbackrate > 4, only need to pop last 4 samples
                for (int i=numberOfSamplesToUpdate; i > 0; i--) {
					_addInterpolationPoint(channel,-resx,result);
                }

                if (_numInterpolationPoints[channel] < 4) {
                    _numInterpolationPoints[channel]++;
                    result = 0;
                } else 
                    result = _interpolationPoints[channel][1].y;
                //Serial.printf("%f\n", result);
            }
        }
  
        *value = result;

        return true;
    }

public:	
    void setPlaybackRate(double f) {
        _playbackRate = f * _file_sample_rate / AUDIO_SAMPLE_RATE_EXACT;
        if (!_useDualPlaybackHead) {
            if (f < 0.0) {
                if  (_bufferPosition1 <= _header_offset) {
                    if (_play_start == play_start::play_start_sample)
                        _bufferPosition1 = _samples_to_start(_file_samples - 1);
                    else
                        _bufferPosition1 = _samples_to_start(_loop_finish - 1);
                } 
            } else {
                if (f >= 0.0 && _bufferPosition1 < _header_offset) {
                    if (_play_start == play_start::play_start_sample) 
                        _bufferPosition1 = _samples_to_start(0);
                    else
                        _bufferPosition1 = _samples_to_start(_loop_start);
                }
            }
        } else { 
            // _useDualPlaybackHead == true 
            if (_crossfade == 0.0) {
                if (f < 0.0) { 
                    if( _bufferPosition1 <= _header_offset) {
                        if (_play_start == play_start::play_start_sample)
                            _bufferPosition1 = _samples_to_start(_file_samples - 1);
                        else
                            _bufferPosition1 = _samples_to_start(_loop_finish - 1);
                    }
                } else {
                    if (f >= 0.0 && _bufferPosition1 < _header_offset) {
                        _bufferPosition1 = _header_offset;
                    }
                }
            }
        }
    }

    float playbackRate() {
        return _playbackRate;
    }

    void loop(uint32_t numSamples) {
        int bufferPosition = ((_crossfade < 1.0)? _bufferPosition1 : _bufferPosition2) - _header_offset;
        _loop_start = bufferPosition / _numChannels;
        _loop_finish = bufferPosition / _numChannels + numSamples;
        _loopType = loop_type::looptype_repeat;
    }

    void setLoopType(loop_type loopType)
    {
		if (nullptr != _sourceBuffer)
			_sourceBuffer->setLoopType(loopType);
        _loopType = loopType;
    }

    loop_type getLoopType() {
        return _loopType;    
    }

    bool available(void) {
        return (_play_state != STOPPED && _file_samples > 0);
    }

    void retrigger(void) {
        retrig_process();
    }
    bool reload(void) {
        if (nullptr != _sourceBuffer)
          return _sourceBuffer->preLoadBuffers(_bufferPosition1, _bufferInPSRAM, _playbackRate >= 0.0f);
        return false;
    }
    void reset(void) {
        if (_file_samples == 0) return;

        if (_interpolationType != ResampleInterpolationType::resampleinterpolation_none) {
            initializeInterpolationPoints();
        }

        for (size_t i=0;i<MAX_CHANNELS;i++)
            _numInterpolationPoints[i] = 0;

        if (_playbackRate >= 0.0)
        {
            // forward playback - set _file_offset to
            switch (_play_start)
            {
                case play_start::play_start_sample: // first audio block in file
                    _bufferPosition1 = _samples_to_start(0);
                    break;
                case play_start::play_start_loop:   // loop start
                    _bufferPosition1 = _samples_to_start(_loop_start);
                    break;
                case play_start::play_start_arbitrary: // user-defined position
                    _bufferPosition1 = _samples_to_start(_playback_start);
                    break;
            }
        }
        else
        {
            // reverse playback - forward _file_offset to last audio block in file
			switch (_play_start)
			{
				case play_start::play_start_sample:
					_bufferPosition1 = _file_size/2 - _numChannels;
					break;
				case play_start::play_start_loop:
					_bufferPosition1 = _samples_to_start(_loop_finish - _numChannels);
					break;
				case play_start::play_start_arbitrary: // user-defined position
					_bufferPosition1 = _samples_to_start(_playback_start);
					break;
			}
        }

        if (reload())
            _play_state = PAUSED;

        _crossfade = 0.0;
        _crossfadeState = 0;
    }

	// This only works once we know how many channels we have, 
	// i.e. NOT before playback has been triggered and we have a file
	uint32_t _samples_to_start(uint32_t samples)
	{
		return _header_offset + (samples * _numChannels);
	}

    void setLoopStart(uint32_t loop_start) {
        _loop_start = loop_start;
		if (nullptr != _sourceBuffer)
			_sourceBuffer->setLoopStart(_samples_to_start(_loop_start));
    }

    void setLoopFinish(uint32_t loop_finish) {
        // sample number, (NOT byte number)
        _loop_finish = loop_finish;
		if (nullptr != _sourceBuffer)
			_sourceBuffer->setLoopFinish(_samples_to_start(_loop_finish));
    }

    void setUseDualPlaybackHead(bool useDualPlaybackHead) {
        _useDualPlaybackHead = useDualPlaybackHead;
    }

    void setCrossfadeDurationInSamples(unsigned int crossfadeDurationInSamples) {
        _crossfadeDurationInSamples = crossfadeDurationInSamples;
    }

    void setInterpolationType(ResampleInterpolationType interpolationType) {
        if (interpolationType != _interpolationType) {
            _interpolationType = interpolationType;
            initializeInterpolationPoints();
        }
    }

    int16_t getNumChannels() {
        return _numChannels;
    }

    void setNumChannels(uint16_t numChannels) {
        if (numChannels != _numChannels) {
            _numChannels = numChannels;
            initializeInterpolationPoints();
        }
    }

    void setHeaderSizeInBytes(uint32_t headerSizeInBytes) {
        _header_offset = headerSizeInBytes / 2;
    }

    void setBeatStart(uint16_t beatnum) {
        if (_tempo_bpm == 0.0) {
          _play_start = play_start::play_start_sample;
          _playback_start = 0;
          return;
        }
        const int samples_per_beat = _file_sample_rate * 60 / _tempo_bpm;
        _play_start = play_start::play_start_arbitrary;
        _playback_start = samples_per_beat * beatnum;
    }

    void setPlayStart(play_start start, uint32_t playback_start) {
        _play_start = start;
        _playback_start = playback_start;
    }

    uint32_t positionMillis()
    {
        if (_file_size == 0) return 0;
        return ((uint64_t)getPosition() * B2M() * 2) >> 32;
    }
    uint32_t lengthMillis()
    {
        return ((uint64_t)_file_samples * B2M() * 2) >> 32;
    }

    uint32_t getPosition() {
        return (((_crossfade < 0.5)? _bufferPosition1 : _bufferPosition2) - _header_offset) / _numChannels;
    }
    int getBufferPosition1() {
        return _bufferPosition1;
    }

    int getBufferPosition2() {
        return _bufferPosition2;
    }

    double getCrossfade() {
        return _crossfade;
    }

    bool getUseDualPlaybackHead(){
        return _useDualPlaybackHead;
    }

    unsigned int getCrossfadeDurationInSamples() {
        return _crossfadeDurationInSamples;
    } 

    uint32_t getLoopStart() {
        return  _loop_start;// / _numChannels - _header_offset;
    }

    uint32_t getLoopFinish() {
        return  _loop_finish;// / _numChannels - _header_offset;
    }

    void setBufferInPSRAM(bool flag){
        _bufferInPSRAM = flag;
    }

    bool getBufferInPSRAM(){
        return _bufferInPSRAM;
    }

    float getBPM() {
      return _tempo_bpm;
    }

    void matchTempo(float target) {
      if (_tempo_bpm > 0.0 && millis() > stall_time)
        setPlaybackRate(target / _tempo_bpm);
    }
    void syncTrig() {
      if (isPlaying() && _tempo_bpm > 0.0 && _playbackRate > 0.0) {
        const int samples_per_beat = _file_sample_rate * 60 / _tempo_bpm;
        const int pos = getPosition() - ((_play_start == play_start::play_start_loop) * getLoopStart());
        int diff = pos % samples_per_beat;

        if (diff > samples_per_beat/2) {
            // closest beat is the next one
            diff -= samples_per_beat; // this should be negative
        }

        // don't act on sufficiently small drift... 200 samples == 4.5ms
        if (abs(diff) < 200) return;

        if (diff < 0) {
            // jump forward
            _bufferPosition1 -= (diff * 2 * _numChannels);
        } else {
            // closest beat is the previous one - we need to stall
            stall_time = millis() + uint32_t(diff * 1000 / (_playbackRate * _file_sample_rate));
            setPlaybackRate(0.0);
        }
      }
    }

protected:
    volatile PlayState _play_state = STOPPED;

    uint32_t _file_size;
    uint32_t _header_offset = 0; // == (header size in bytes ) / 2

    uint32_t stall_time = 0;
    float _tempo_bpm = 0.0;
    double _playbackRate = 1.0;
    double _remainder = 0.0;
    loop_type _loopType = loop_type::looptype_none;
    play_start _play_start = play_start::play_start_sample;
    uint32_t _bufferPosition1 = 0;
    uint32_t _bufferPosition2 = 0;
    double _crossfade = 0.0;
    bool _useDualPlaybackHead = false;
    unsigned int _crossfadeDurationInSamples = 256; 
    int _crossfadeState = 0;
    uint32_t _playback_start = 0;
    uint32_t _loop_start = 0;
    uint32_t _loop_finish = 0;
    uint32_t _file_samples = 0;
    int _file_sample_rate = AUDIO_SAMPLE_RATE_EXACT;
    int16_t _numChannels = -1;
    char *_filename = nullptr;
    TArray *_sourceBuffer = nullptr;
	bool _bufferInPSRAM = false;

    ResampleInterpolationType _interpolationType = ResampleInterpolationType::resampleinterpolation_none;
	static const size_t MAX_CHANNELS = 8;
    unsigned int _numInterpolationPoints[MAX_CHANNELS] = {0};
    InterpolationData _interpolationPoints[MAX_CHANNELS][4] = {0};
    
    void initializeInterpolationPoints(void) {}
	
	int16_t _getSourceBufferValue(int pos, int offset, uint16_t channel) 
		{ return getSourceBufferValue(pos + offset*_numChannels + channel); }

	void _addInterpolationPoint(uint16_t channel, uint32_t x, int16_t y)
	{
		switch (_interpolationType)
		{
			default:
				break;
				
			case resampleinterpolation_linear:
				_interpolationPoints[channel][0] = _interpolationPoints[channel][1];
				_interpolationPoints[channel][1].x = x;
				_interpolationPoints[channel][1].y = y;
				if (_numInterpolationPoints[channel] < 2) _numInterpolationPoints[channel]++;
				break;
				
			case resampleinterpolation_quadratic:
				_interpolationPoints[channel][0] = _interpolationPoints[channel][1];
				_interpolationPoints[channel][1] = _interpolationPoints[channel][2];
				_interpolationPoints[channel][2] = _interpolationPoints[channel][3];
				_interpolationPoints[channel][3].x = x;
				_interpolationPoints[channel][3].y = y;
				if (_numInterpolationPoints[channel] < 4) _numInterpolationPoints[channel]++;
				break;
		}
	}

};

// Explicitly specialize some instantiations of functions where
// they need to be different for the supplied type. e.g. it makes
// no sense to track buffer statuses for an in-memory "file".
template<>
inline void ResamplingReader<short int,File>::resetStatus(void) {}
template<>
inline void ResamplingReader<short int,File>::getStatus(char* buf) { strcpy(buf,"int[]"); }
template<>
inline void ResamplingReader<short int,File>::triggerReload(void) {}
template<>
inline bool ResamplingReader<short int,File>::reload(void) { return true; }
template<>
inline void ResamplingReader<short int,File>::setLoopType(loop_type loopType) { _loopType = loopType; }
template<>
inline void ResamplingReader<short int,File>::setLoopStart(uint32_t loop_start) { _loop_start = loop_start; }
template<>
inline void ResamplingReader<short int,File>::setLoopFinish(uint32_t loop_finish) { _loop_finish = loop_finish; }
template<>
inline size_t ResamplingReader<short int,File>::preLoadBuffers(int , bool , bool) {return 0;}
}

#endif //TEENSYAUDIOLIBRARY_RESAMPLINGREADER_H
