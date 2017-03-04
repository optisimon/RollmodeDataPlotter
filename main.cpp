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
// TODO: Only Y-axis override added yet. What to do with X axis? What if we want to fix
//       certain but not all min/max values?
// TODO: With multiple plots, how do we handle missing data for one of them without timestamps?
// TODO: Refactor sdlDisplayThread

#include <string.h>

#include "StreamProcessors/CappedPeakStorageWaveform.hpp"
#include "StreamProcessors/FIFOStorageWaveform.hpp"
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
#include <sstream>
#include <string>
#include <thread>
#include <mutex>


int verbose_flag = 0;

static std::atomic<bool> quit(false);

struct Waveform {
	Waveform() : peakWaveform(0)
	{ }
	~Waveform() {
		delete peakWaveform;
		peakWaveform = 0;
	}
	Waveform (const Waveform& other) : prefix(other.prefix), peakWaveform(0)
	{
		if (other.peakWaveform)
		{
			peakWaveform = other.peakWaveform->duplicate();
		}
	}
	Waveform& operator=(const Waveform& w)
	{
		prefix = w.prefix;
		peakWaveform = peakWaveform->duplicate();
		return *this;
	}
	std::string prefix;
	IWaveformStorage<double>*  peakWaveform;
};

std::vector<Waveform> g_waveforms;
static std::mutex g_waveforms_mutex;


struct Axis {
	  double minx;
	  double maxx;
	  double miny;
	  double maxy;
	  Axis() :
		  minx(std::numeric_limits<double>::quiet_NaN()),
		  maxx(std::numeric_limits<double>::quiet_NaN()),
		  miny(std::numeric_limits<double>::quiet_NaN()),
		  maxy(std::numeric_limits<double>::quiet_NaN())
	  {

	  }

	  bool isValidX() const { return (minx == minx); }

	  bool isValidY() const { return (miny == miny); }

	  void setX(double minx, double maxx)
	  {
		  this->minx = minx;
		  this->maxx = maxx;
	  }

	  void setY(double miny, double maxy)
	  {
		  this->miny = miny;
		  this->maxy = maxy;
	  }
};

Axis axis;

enum DisplayMode {
	SQUEZE,

	ROLL_NY,
//	ROLL_TY,
//	ROLL_XY,

//	PLOT_XY,
//	PLOT_XYN,
//	PLOT_XYT,
};

DisplayMode displayMode = SQUEZE;

int numSamples = 4096;


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

			double signalMin = std::numeric_limits<double>::max();
			double signalMax = std::numeric_limits<double>::min();

			for (auto & waveform : period_waveforms)
			{
				const std::vector<MinMax<double> > & period_waveform = waveform.peakWaveform->getWaveform();
				for (const auto& val : period_waveform)
				{
					if (val.min < signalMin) { signalMin = val.min; }
					if (val.max > signalMax) { signalMax = val.max; }
				}
			}

			// If external constraints on axis, follow those
			if (axis.isValidY())
			{
				signalMin = axis.miny;
				signalMax = axis.maxy;
			}


			bool ticsWasDrawn = false;

			for (auto & waveform : period_waveforms)
			{
				const std::vector<MinMax<double> > & period_waveform = waveform.peakWaveform->getWaveform();

				int width = win.getWidth();
				int height = win.getHeight();

				const auto & convertY = [&](double sample) {
					double tmp = (sample - signalMin) * (height-30) * 1.0 / (signalMax - signalMin);
					return height - 1 - tmp;
				};
				const auto & convertX = [&](double x) {
					int leftPad = 60;
					double tmp;
					if (displayMode == DisplayMode::ROLL_NY)
						tmp = x * (width - leftPad) *1.0 / numSamples;
					else
						tmp = x * (width - leftPad) *1.0 / period_waveform.size();
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
								width - 1,
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
					if (displayMode == DisplayMode::SQUEZE)
					{
						win.drawLine(
								convertX(i),
								convertY(period_waveform[i].min),
								convertX(i),
								convertY(period_waveform[i].max),
								255, 255, 255, 255
						);
					}
					else if (displayMode == DisplayMode::ROLL_NY)
					{
						int emptySamples = numSamples-period_waveform.size();
						win.drawLine(
								convertX(i + emptySamples),
								convertY(period_waveform[i].min),
								convertX(i + emptySamples),
								convertY(period_waveform[i].max),
								255, 255, 255, 255
						);
					}
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
					if (displayMode == DisplayMode::SQUEZE)
					{
						win.drawLine(
								convertX(i),
								convertY(period_waveform[i].max),
								convertX(i+1),
								convertY(period_waveform[i+1].max),
								255, 255, 255, 255
						);
					}
					else if (displayMode == DisplayMode::ROLL_NY)
					{
						int emptySamples = numSamples-period_waveform.size();
						win.drawLine(
								convertX(i + emptySamples),
								convertY(period_waveform[i].max),
								convertX(i+1 + emptySamples),
								convertY(period_waveform[i+1].max),
								255, 255, 255, 255
						);
					}


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
		  {"axis",    required_argument, 0, 'a'},
		  {"mode",    required_argument, 0, 'm'},
          {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;
      c = getopt_long (argc, argv, "a:vbhf:m:n:x:y:",
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

        case 'a':
        {
        	// --axis "minx maxx miny maxy"
        	std::istringstream is(optarg);
        	is >> axis.minx >> axis.maxx >> axis.miny >> axis.maxy;
        	if ((!is.eof()) || (!is))
        	{
        		std::cout << "ERROR: Unable to parse axis settings \"" << optarg << "\"\n";
        		return 1;
        	}
        	break;
        }

        case 'm':
        {
        	// --mode squeze|roll_ny
        	if (strcmp("squeze", optarg) == 0)
        	{
        		displayMode = DisplayMode::SQUEZE;
        	}
        	else if (strcmp("roll_ny", optarg) == 0)
        	{
        		displayMode = DisplayMode::ROLL_NY;
        	}
        	break;
        }

        case 'n':
        {
        	std::istringstream is(optarg);
        	is >> numSamples;
        	if ((!is.eof()) || (!is))
        	{
        		std::cout << "ERROR: Unable to parse -n setting \"" << optarg << "\"\n";
        		return 1;
        	}
        	break;
        }

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
			  w.peakWaveform = 0;
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
		"-a, --axis \"xmin xmax ymin ymax\" Override plot axis (only caring about Y at the moment)\n"
		"-m, --mode squeze|roll_ny   Sets display mode (squeze is default)\n"
		"    squeze fits all data into the current window\n"
		"    roll_ny rolls the data so only the last n samples are visible (specify n with -n )\n"
		"-n NUMBER Number of samples to span the full screen (in modes supporting that). Defaults to %d\n"
		"\n"
		"Note that the -y argument require a prefix (including everything from the start of the line,\n"
		"even all white spaces before the number, and that the number should be followed by a newline.\n"
		"\n", argv[0], numSamples
		);
		return 1;
	}

    for (auto & waveform : g_waveforms)
    {
    	switch(displayMode)
    	{
    	case DisplayMode::SQUEZE:
    		waveform.peakWaveform = new CappedPeakStorageWaveform<double>(numSamples);
    		break;
    	case DisplayMode::ROLL_NY:
    		waveform.peakWaveform = new FIFOStorageWaveform<double>(numSamples);
    		break;
    	}
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
				g_waveforms[tmp.second].peakWaveform->push(y);
				continue;
			}
		}
	}

	quit = true;
	thread1.join();
	return 0;
}
