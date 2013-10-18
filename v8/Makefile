include ../common.mk

INCLUDES:=-I$(BOOST_HOME)/include
LIBS:=-L$(BOOST_HOME)/lib -lboost_regex -lboost_filesystem -lboost_system
CFLAGS:=-std=c++11 -c -g $(LIBS) $(INCLUDES)
SOURCES:=codegen.cpp trace.cpp detail_trace.cpp lstream.cpp
OBJS:=$(SOURCES:.cpp=.o)

ifeq ($(big-endian), y)
    CFLAGS+=-DBIG
endif

all: synth
utilities: split bbv

bbv: bbv.o trace.o lstream.o
	$(CC) bbv.o trace.o lstream.o $(LIBS) -o bbv

bbv.o: trace.cpp trace.hpp bbv.cpp
	$(CC) $(CFLAGS) bbv.cpp

synth: $(OBJS)
	$(CC) $(OBJS) $(LIBS) -o codegen

codegen.o: detail_trace.o codegen.cpp
	$(CC) $(CFLAGS) codegen.cpp

lstream.o: lstream.hpp lstream.cpp
	$(CC) $(CFLAGS) lstream.cpp

detail_trace.o: trace.o detail_trace.hpp detail_trace.cpp
	$(CC) $(CFLAGS) detail_trace.cpp

trace.o: trace.cpp trace.hpp types.hpp
	$(CC) $(CFLAGS) trace.cpp 

split: split.cpp
	$(CC) $(CFLAGS) split.cpp -o split.o
	$(CC) split.o -o split

installv8: 
	cp codegen ../../v8exps
	cp split ../../v8exps

clean:
	rm *.o codegen bbv split
