/*
 * IWaveFormStorage.hpp
 *
 *  Created on: Aug 21, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#pragma once

#include "MinMax.hpp"

#include <assert.h>

#include <vector>

template<class T>
class IWaveformStorage {
public:
	virtual ~IWaveformStorage() {};
	virtual void push(T y) = 0;
	virtual void push(T x, T y)
	{
		assert(0 && "IWaveformStorage::push(T x, T y) not supported yet");
	}
	virtual const std::vector<MinMax<T> >& getWaveform() const = 0;

	virtual IWaveformStorage<T>* duplicate() const = 0;
	virtual void clear() = 0;
};
