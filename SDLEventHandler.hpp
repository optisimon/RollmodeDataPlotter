/*
 * SDLEventHandler.hpp
 *
 *  Created on: Jun 19, 2016
 *
 *  Copyright (c) 2016 Simon Gustafsson (www.optisimon.com)
 *  Do whatever you like with this code, but please refer to me as the original author.
 */

#pragma once

#include <SDL/SDL.h>


class SDLEventHandler {
public:
	SDLEventHandler() :
		_should_quit(false),
		_should_go_fullscreen(false)
	{
		refresh();
	}

	void refresh()
	{
		SDL_Event event;

		while ( SDL_PollEvent(&event) )
		{
			if ( event.type == SDL_QUIT )
			{
				_should_quit = true;
			}

			if ( event.type == SDL_KEYDOWN )
			{
				switch(event.key.keysym.sym)
				{
				case SDLK_ESCAPE:
					_should_quit = true;
					break;

				case SDLK_F11:
					_should_go_fullscreen ^= true;
					break;

				case SDLK_SPACE:
					break;
				default:
					break;
				}
			}

			if (event.type == SDL_MOUSEBUTTONDOWN)
			{
				if(event.button.button == 1 &&
				   event.button.state == SDL_PRESSED)
				{
					_should_go_fullscreen ^= true;
				}
			}

			if (event.type == SDL_VIDEORESIZE)
			{
				int w = event.resize.w;
				int h = event.resize.h;
				std::cout
				<< "Resize events not supported. (resize to "
				<< w << "x" << h << " requested)\n";
			}
		}
	}

	bool shouldQuit() const { return _should_quit; }

	bool shouldGoFullscreen() const { return _should_go_fullscreen; }


private:
	bool _should_quit;
	bool _should_go_fullscreen;
};
