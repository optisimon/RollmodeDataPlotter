/*
 * MinMax.hpp
 *
 *  Created on: Aug 21, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#pragma once

#include <limits>

template<class T>
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
