DESTDIR?=""

INCLUDE= `sdl-config --cflags`
LIBS= `sdl-config --libs` -lSDL_gfx -lpthread

RollmodeDataPlotter_OBJS= main.o
RollmodeDataPlotter_LIBS= $(LIBS)

unittest_OBJS= \
	unittests/test.o \
	unittests/CappedPeakStorageWaveform_Test.o \
	unittests/MinMaxCheck_Test.o \
	unittests/SlidingAverager_Test.o
unittest_LIBS= $(LIBS) -lboost_unit_test_framework

EXECS= RollmodeDataPlotter unittest
EXEC_installed= RollmodeDataPlotter

COMPILER_FLAGS+= -Wall -O3 -std=c++0x -ggdb

RollmodeDataPlotter: $(RollmodeDataPlotter_OBJS) $(wildcard *.h) $(wildcard *.hpp) Makefile
	$(CXX) $(COMPILER_FLAGS) -o $@ $($@_OBJS) $($@_LIBS)

unittest: $(unittest_OBJS) $(wildcard *.h) $(wildcard *.hpp) Makefile
	$(CXX) $(COMPILER_FLAGS) -o $@ $($@_OBJS) $($@_LIBS) 

%.o:	%.cpp
	$(CXX) -c $(COMPILER_FLAGS) -o $@ $< $(INCLUDE)

all: RollmodeDataPlotter unittest


.PHONY: test
test: unittest
	./unittest

.PHONY: install
install: $(EXEC_installed)
	install $(EXEC_installed) $(DESTDIR)/usr/local/bin

.PHONY: uninstall
uninstall:
	rm $(DESTDIR)/usr/local/bin/$(EXEC_installed)

.PHONY: prepare
prepare:
	apt-get install libsdl1.2-dev libsdl-gfx1.2-dev

.PHONY: prepare-all
prepare-all: prepare
	apt-get install libboost-test-dev


.PHONY: clean
clean:
	rm -f $(EXECS) $(RollmodeDataPlotter_OBJS) $(unittest_OBJS)
