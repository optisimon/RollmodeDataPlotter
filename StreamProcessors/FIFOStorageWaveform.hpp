/*
 * FIFOStorageWaveform.hpp
 *
 *  Created on: Jun 21, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#pragma once


#include "IWaveformStorage.hpp"
#include "MinMax.hpp"

#include <vector>
#include <deque>

#include <assert.h>
#include <stdint.h>


template<class T>
class FIFOStorageWaveform : public IWaveformStorage<T> {
public:
	
	FIFOStorageWaveform(int maxWaveformSize = 4096) :
	_maxWaveformSize(maxWaveformSize)
	{
		clear();
	}
	
	void push(T val)
	{
		if (_waveform.size() == _maxWaveformSize)
		{
			_waveform.erase(_waveform.begin());
		}

		_waveform.push_back(MinMax<T>(val, val));
		_lastSample = val;
	}

	const std::vector<MinMax<T> >& getWaveform() const {
		return _waveform;
	}

	const T getLastSample() const {
		return _lastSample;
	}

	IWaveformStorage<T>* duplicate() const
	{
		FIFOStorageWaveform* w = new FIFOStorageWaveform(_maxWaveformSize);
		w->_waveform = _waveform;
		w->_lastSample = _lastSample;
		return w;
	}

	void clear()
	{
		_waveform.clear();
	}

private:
	size_t _maxWaveformSize;
	std::vector<MinMax<T> > _waveform; // TODO: Decide what to return. This class would prefer a std::deque, or iterators. Others would prefer std::vector
	T _lastSample;
};
