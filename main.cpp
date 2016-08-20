/*
 * main.cpp
 *
 *  Created on: Jun 29, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */


// TODO: Create a capped storage waveform storing both min and max values
// TODO: how much data should we save? Should we always show everything?
// TODO: Should we allow zoom?
// TODO: Should we have a proper roll mode?
// TODO: Should we allow setting axis somehow?
// TODO: With multiple plots, how do we handle missing data for one of them without timestamps?

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
  std::string inputFileName = "/dev/stdin";
  std::string xPrefix;
  std::map<std::string, size_t> yPrefixes;



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
          inputFileName = optarg;
          break;
          
        case 'x':
          //printf ("option -x with value `%s'\n", optarg);
          //xPrefix = optarg;
          break;

        case 'y':
          printf ("option -y with value `%s'\n", optarg);
          {
			  int size = yPrefixes.size();
			  yPrefixes[optarg] = size;
			  Waveform w;
			  w.prefix = optarg;
			  g_waveforms.push_back(w);
          }
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
		"-h, --help\n"
		"-f, --file file_with_reading\n"
		"-y prefix_of_number_to_plot\n"
		"\n"
		"Note that the -y argument require a prefix (including everything from the start of the line,\n"
		"even all white spaces before the number, and that the number should be followed by a newline.\n"
		"\n", argv[0]
		);
		return 1;
	}

	std::thread thread1(sdlDisplayThread);

	std::string line;
	double y = 0;
	std::ifstream in(inputFileName.c_str());
	while (!quit && getline(in, line))
	{
		usleep(1);

		if (xPrefix.size() && line.find(xPrefix) != line.npos)
		{
			// found an x-prefix.
//			x = std::atof(line.substr(xPrefix.size()).c_str());
			//printf("X_PREFIXED LINE: \"%s\" (%f)\n", line.c_str(), x);
//			newX = true;
			continue;
		}
		
		for (auto& tmp : yPrefixes)
		{
			const std::string& yPrefix = tmp.first;
			if (line.find(yPrefix) != line.npos)
			{
				y = std::atof(line.substr(yPrefix.size()).c_str());
							
				std::lock_guard<std::mutex> guard(g_waveforms_mutex);
				g_waveforms[tmp.second].peakWaveform.push(y);
				continue;
			}
		}
	}

	quit = true;
	thread1.join();
	return 0;
}
