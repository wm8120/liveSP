include common.mk

INCLUDES:=-I$(BOOST_HOME)/include
LIBS:=-L$(BOOST_HOME)/lib -lboost_regex
CFLAGS:=-std=c++11 -g $(INCLUDES) $(LIBS)
all: codegen.cpp codegen.hpp
	$(CC) $(CFLAGS) codegen.cpp -o codegen
install:
	cp codegen $(INSTALL)
install_gem5:
	cp -r src/* $(GEM5)/src/
	cp -r build_opts/ARM $(GEM5)/build_opts/
	cp -r configs/* $(GEM5)/configs/
clean:
	rm *.o codegen

