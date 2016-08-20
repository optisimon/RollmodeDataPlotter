/*
 * CappedPeakStorageWaveform.hpp
 *
 *  Created on: Jun 21, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#pragma once

#include <assert.h>

#include <limits>
#include <vector>
#include <stdint.h>

template<class T>
class CappedPeakStorageWaveform {
public:
	struct MinMax {
		MinMax() {
			reset();
		}
		MinMax(T min, T max) : min(min), max(max)
		{ }
		void update (T val)
		{
			if (val > max) { max = val; }
			if (val < min) { min = val; }
		}
		void reset()
		{
			min = std::numeric_limits<T>::max();
			max = std::numeric_limits<T>::min();
		}
		T min;
		T max;
	};
	
	CappedPeakStorageWaveform(int maxWaveformSize = 4096) :
	_maxWaveformSize(maxWaveformSize),
	_waveformNumSamplesSkip(0), // 0 = everysample, 1 = every other sample
	_skipCounter(0)
	{
		assert((_maxWaveformSize & 1) == 0); // Needs to be even
		clear();
	}
	
	void push(T val)
	{
		_currentMinMax.update(val);
		if (_skipCounter == _waveformNumSamplesSkip)
		{
			if (_waveform.size() < _maxWaveformSize)
			{
				_waveform.push_back(_currentMinMax);
				_currentMinMax.reset();
				_skipCounter = 0;
				return;
			}
			else
			{
				// Compact the vector to only use every second sample.
				// double _waveformNumSamplesSkip,
				for (size_t i = 0; 2*i < _maxWaveformSize; i++)
				{
					_waveform[i] = _waveform[2*i];
				}
				_waveform.resize(_maxWaveformSize/2);
				_waveformNumSamplesSkip = (_waveformNumSamplesSkip + 1) * 2 - 1;

				// Don't forget the current sample as well
				_waveform.push_back(_currentMinMax);
				_currentMinMax.reset();
				_skipCounter = 0;
				return;
			}
		}

		_skipCounter++;
	}

	const std::vector<MinMax>& getWaveform() const {
		return _waveform;
	}

	void clear()
	{
		_waveform.clear();
		_waveformNumSamplesSkip = 0;
		_skipCounter = 0;
		_currentMinMax.reset();
	}

private:
	size_t _maxWaveformSize;
	size_t _waveformNumSamplesSkip;
	size_t _skipCounter;
	MinMax _currentMinMax;
	std::vector<MinMax> _waveform;
};
