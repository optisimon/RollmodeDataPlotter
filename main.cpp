/*
 * main.cpp
 *
 *  Created on: Jun 29, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#include <string.h>

#include "StreamProcessors/CappedPeakStorageWaveform.hpp"
#include "StreamProcessors/MinMaxCheck.hpp"
#include "StreamProcessors/SlidingAverager.hpp"
#include "SDLWindow.hpp"
#include "SDLEventHandler.hpp"

#include <fstream>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include <iostream>
#include <atomic>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <mutex>

int verbose_flag = 0;

static std::atomic<bool> quit(false);

struct Waveform {
	std::string prefix;
	CappedPeakStorageWaveform<double>  peakWaveform;
};
std::vector<Waveform> g_waveforms;
static std::mutex g_waveforms_mutex;

// TODO: Also update waveform even when not triggering on anything. (roll mode)

class RPMCalculatorFromAudio {
public:
	RPMCalculatorFromAudio(int audioSampleRate, int divisor) :
		_audioSampleRate(audioSampleRate),
		_divisor(divisor),
		_periodCounter(0),
		_minMax(audioSampleRate / 5, 13),
		_slidingAverageRpmCalculator(10),
		_thresholdInPercentage(0),
		_threshold(0),
		_hysteresis(0),
		_state(Uninitialized),
		_numStoredWaveforms(0),
		_numWaveformsBeforeDelivery(3)
	{

	}

	void check(int16_t sample)
	{
		_minMax.check(sample);
		_waveform.push(sample);

		_periodCounter++;
		switch(_state)
		{
		case Uninitialized:
			if (sample >= _threshold)
			{
				_state = WasAbove;
				_threshold = sample;
				_periodCounter = 0;
			}
			break;

		case WasBelow:
			if (sample >= _threshold + _hysteresis)
			{
				_state = WasAbove;
				_periodCounter = std::max<long>(_periodCounter, 1);

				double rpm = ((60.0 * _audioSampleRate) / _periodCounter ) / _divisor;

				std::cout
				<< "PeriodCounter=" << _periodCounter
				<< ", rpm=" << rpm
				<< ", threshold=" << int(_threshold)
				<< ", hysteresis=" << int(_hysteresis)
				<< ", minMax.min=" << _minMax.getMin()
				<< ", minMax.max=" << _minMax.getMax()
				 << "\n";

				_slidingAverageRpmCalculator.push(rpm);
				_periodCounter = 0;
				
				_numStoredWaveforms++;

				if (_numStoredWaveforms >= _numWaveformsBeforeDelivery)
				{
					std::lock_guard<std::mutex> guard(g_period_waveform_mutex);

					const std::vector<int16_t>& waveform = _waveform.getWaveform();
					g_period_waveform.resize(waveform.size());

//					int min = _minMax.getMin();
//					int max = _minMax.getMax();

					for (size_t i = 0; i < waveform.size(); i++)
					{
						g_period_waveform[i] = waveform[i];
					}
					g_period_waveform_updated = true;
					_waveform.clear();
					_numStoredWaveforms = 0;
				}
			}
			break;

		case WasAbove:
			if (sample < _threshold - _hysteresis)
			{
				_state = WasBelow;
			}
			break;
		}
	}

private:
	int _audioSampleRate;
	int _divisor;
	long _periodCounter;

	MinMaxCheck _minMax;
	SlidingAverager _slidingAverageRpmCalculator;
	int _thresholdInPercentage;
	double _threshold;
	double _hysteresis;

	enum State {
		Uninitialized,
		WasBelow,
		WasAbove
	};

	State _state;

	int _numStoredWaveforms;
	const int _numWaveformsBeforeDelivery;

	CappedStorageWaveform _waveform;
};

std::vector<double> getTickmarkSuggestion(double min, double max, int maxNumTicks = 10)
{
	double dy_if_requested_ticks = (max - min) / maxNumTicks;

	double exponent = floor(log10(dy_if_requested_ticks));

	// tmp is the dy increment, but scaled by 10^(-exponent) to always be a single
	// digit in front of the decimal point.
	double tmp = dy_if_requested_ticks / (pow10(exponent));

	//
	// We want ticks to have one of these forms
	//
	double valid[] = {1, 1.25, 1.5, 2, 2.5, 5, 10};

	double ticToUse = 1;
	for (size_t i = 0; i < sizeof(valid)/sizeof(valid [0]); i++)
	{
		if ( tmp <= valid[i])
		{
			ticToUse = valid[i];
			break;
		}

	}
	double dy = ticToUse * pow10(exponent);

	//
	// find closest value below min matching a dy multiplier
	//
	double nMin = floor(min / dy);
	double bottom = nMin * dy;

	std::vector<double> tics;
	for (double tic = bottom; tic <= max; tic += dy)
	{
		tics.push_back(tic);
	}
	return tics;
}


void sdlDisplayThread()
{
	SDLWindow win;
	SDLEventHandler eventHandler;
	while(!quit)
	{
		win.drawTopText();
		{
			std::vector<Waveform> period_waveforms;
			{
				std::lock_guard<std::mutex> guard(g_waveforms_mutex);
				
				period_waveforms = g_waveforms;
//				period_waveform.resize(peakWaveform.size());
				
//				for (size_t i = 0; i < g_period_waveform.size(); i++)
//				{
//					period_waveform[i] = peakWaveform[i];
//				}
			}

			int signalMin = std::numeric_limits<int>::max();
			int signalMax = std::numeric_limits<int>::min();

			for (auto & waveform : period_waveforms)
			{
				const std::vector<CappedPeakStorageWaveform<double>::MinMax> & period_waveform = waveform.peakWaveform.getWaveform();
				for (const auto& val : period_waveform)
				{
					if (val.min < signalMin) { signalMin = val.min; }
					if (val.max > signalMax) { signalMax = val.max; }
				}
			}


			bool ticsWasDrawn = false;

			for (auto & waveform : period_waveforms)
			{
				const std::vector<CappedPeakStorageWaveform<double>::MinMax> & period_waveform = waveform.peakWaveform.getWaveform();

				int width = win.getWidth();
				int height = win.getHeight();

				const auto & convertY = [&](int sample) {
					int tmp = (sample - signalMin) * (height-30) * 1.0 / (signalMax - signalMin);
					return height - 1 - tmp;
				};
				const auto & convertX = [&](int x) {
					int leftPad = 60;
					int tmp = x * (width - leftPad) *1.0 / period_waveform.size();
					return leftPad + tmp;
				};


				if (!ticsWasDrawn)
				{
					//
					// Draw horizontal help lines
					//
					const std::vector<double> tics =
							getTickmarkSuggestion(signalMin, signalMax, /*maxNumTicks*/ 10);

					for (const auto& y : tics)
					{
						win.drawLine(
								convertX(0),
								convertY(y),
								convertX(period_waveform.size() - 1),
								convertY(y),
								64, 64, 64, 255
						);
						char buffer[200];
						snprintf(buffer, sizeof(buffer), "%.2f", y);

						win.drawString(0, convertY(y), buffer);
					}

					ticsWasDrawn = true;
				}


				for (int i = 0; i < int(period_waveform.size())-1; i++)
				{
					win.drawLine(
							convertX(i),
							convertY(period_waveform[i].min),
							convertX(i),
							convertY(period_waveform[i].max),
							255, 255, 255, 255
					);
				}
				
//				for (int i = 0; i < int(period_waveform.size())-1; i++)
//				{
//					win.drawLine(
//							convertX(i),
//							convertY(period_waveform[i].min),
//							convertX(i+1),
//							convertY(period_waveform[i+1].min),
//							32, 32, 255, 255
//					);
//				}
				
				for (int i = 0; i < int(period_waveform.size())-1; i++)
				{
					win.drawLine(
							convertX(i),
							convertY(period_waveform[i].max),
							convertX(i+1),
							convertY(period_waveform[i+1].max),
							255, 255, 255, 255
					);
				}
			}
		}

		win.flip();
		win.clear();
		usleep(20000);

		eventHandler.refresh();

		if (eventHandler.shouldQuit())
		{
			quit = true;
		}


		bool wantFullscreen = eventHandler.shouldGoFullscreen();
		bool isFullscreen = win.isFullscreen();
		if (wantFullscreen != isFullscreen)
		{
			win.setFullscreenMode(wantFullscreen);
		}
	}
}

int main (int argc, char *argv[])
{
  std::string xPrefix;
  std::string yPrefix;
  int showHelp_flag = 0;

  while(true)
  {
	  int c;
	  static struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,   &verbose_flag, 1},
          {"brief",   no_argument,   &verbose_flag, 0},
          {"help",    no_argument,   &showHelp_flag, 1},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"file",    required_argument, 0, 'f'},
          {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;
      c = getopt_long (argc, argv, "vbhf:x:y:",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {
        case 0:
          /* If this option set a flag, do nothing else now. */
          if (long_options[option_index].flag != 0)
            break;
          printf ("*option %s", long_options[option_index].name);
          if (optarg)
            printf (" with arg %s", optarg);
          printf ("\n");
          break;

        case 'v':
          verbose_flag = 1;
          puts ("option -v\n");
          break;

        case 'b':
          verbose_flag = 0;
          puts ("option -b\n");
          break;

        case 'h':
		  showHelp_flag = 1;
		  printf ("option -h\n");
		  break;

        case 'f':
          printf ("option -f with value `%s'\n", optarg);
          break;
          
        case 'x':
          printf ("option -x with value `%s'\n", optarg);
          xPrefix = optarg;
          break;

        case 'y':
          printf ("option -y with value `%s'\n", optarg);
          yPrefix = optarg;
          break;

        case '?':
          /* getopt_long already printed an error message. */
          break;

        default:
          abort ();
        }
    }
    
    if (showHelp_flag)
    {
		printf(
		"%s [options]\n"
		"-v, --verbose \n"
		"-b, --brief\n"
		"-m, --mic          Use mic directly (alsa hw:0,0)\n"
		"-d, --rpm_divisor  Number to divide pulse frequency with"
		"-h, --help\n"
		"-f, --file FILENAME.WAV\n"
		"\n", argv[0]
		);
		return 1;
	}


//	sdlDisplayThread();
	std::thread thread1(sdlDisplayThread);

	std::string line;
	double x = 0;
	double y = 0;
	bool newX = false;
	bool newY = false;
	while (getline(std::cin, line))
	{
		if (newY)
		{
			std::lock_guard<std::mutex> guard(g_period_waveform_mutex);
			g_period_waveform.push_back(y*1024);
			newY = false;
		}

		if (xPrefix.size() && line.find(xPrefix) != line.npos)
		{
			// found an x-prefix.
			x = std::atof(line.substr(xPrefix.size()).c_str());
			std::cout << "X_PREFIXED LINE: \"" << line << "\" (" << x << ")\n";
			newX = true;
			continue;
		}
		else if (yPrefix.size() && line.find(yPrefix) != line.npos)
		{
			y = std::atof(line.substr(yPrefix.size()).c_str());
			std::cout << "Y_PREFIXED LINE: \"" << line << "\" (" << y << ")\n";
			newY = true;
			continue;
		}
		else
		{
			std::cout << "SKIPPED LINE: " << line << "\n";
		}
	}

	quit = true;
	thread1.join();
	return 0;
}
