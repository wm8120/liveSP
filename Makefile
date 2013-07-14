include common.mk

INCLUDES:=-I$(BOOST_HOME)/include
LIBS:=-L$(BOOST_HOME)/lib -lboost_regex
CFLAGS:=-std=c++11 -g $(INCLUDES) $(LIBS)
all: codegen.cpp codegen.hpp
	$(CC) $(CFLAGS) codegen.cpp -o codegen
clean:
	rm *.o codegen

