#!/bin/sh
BINARY=qsort_large
ARGS="input_large.dat"
INTERVAL=1000000
#MAX=100000000

GEM5=/home/wm/gem5_new
EXE=${GEM5}/build/ARM/gem5.opt
SCRIPT=${GEM5}/configs/example/se.py

${EXE} ${SCRIPT} --simpoint-profile --simpoint-interval=${INTERVAL} --fastmem -c ${BINARY} -o "${ARGS}" #-I ${MAX}
#${EXE} ${SCRIPT} --cpu-type=arm_detailed --caches --l2cache -c ${BINARY} -o "${ARGS}" -I ${MAX}
#cp m5out/stats.txt m5out/${BINARY}.stat
