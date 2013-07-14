include ../common.mk

INCLUDES:=-I$(BOOST_HOME)/include
LIBS:=-L$(BOOST_HOME)/lib -lboost_regex
CFLAGS:=-std=c++11 -c -g $(LIBS) $(INCLUDES)
SOURCES:=codegen.cpp trace.cpp detail_trace.cpp
OBJS:=$(SOURCES:.cpp=.o)

all: synth

synth: $(OBJS)
	$(CC) $(OBJS) $(LIBS) -o codegen

codegen.o: detail_trace.o codegen.cpp
	$(CC) $(CFLAGS) codegen.cpp

detail_trace.o: trace.o detail_trace.hpp detail_trace.cpp
	$(CC) $(CFLAGS) detail_trace.cpp

trace.o: trace.cpp trace.hpp types.hpp
	$(CC) $(CFLAGS) trace.cpp 

clean:
	rm *.o codegen
