#!/bin/sh
#BINARY=susan
#ARGS="input_small.pgm output_small.smoothing.pgm -s"
BINARY=qsort_large
ARGS="input_large.dat"
INTERVAL=1000000
INTERVAL_NO=512
START=$(( INTERVAL*INTERVAL_NO ))

GEM5=/home/wm/gem5_new
EXE=${GEM5}/build/ARM/gem5.opt
SCRIPT=${GEM5}/configs/example/se.py

${EXE} --debug-flags=Exec --trace-file=${BINARY}_sp${INTERVAL_NO}.dump ${SCRIPT} --synthesize --synthesize-interval=${INTERVAL} --synthesize-start=${START} --cpu-type=mycpu --fastmem -c ${BINARY} -o "${ARGS}"
./codegen ${BINARY}
arm-none-linux-gnueabi-as -march=armv7 -mcpu=cortex-a15 -mfpu=vfpv4 -mfloat-abi=soft synthesis.s -o synthesis.o
arm-none-linux-gnueabi-ld -T linker.x synthesis.o -o synthesis

if [[ ! -e ${BINARY}_${INTERVAL}_sps ]]; then
    mkdir ${BINARY}_${INTERVAL}_sps;
fi
cp synthesis ${BINARY}_${INTERVAL}_sps/syn${INTERVAL_NO}
cp synthesis.s ${BINARY}_${INTERVAL}_sps/syn${INTERVAL_NO}.s
${EXE} ${SCRIPT} --cpu-type=arm_detailed --caches --l2cache -c synthesis
mv m5out/stats.txt m5out/${BINARY}_sp${INTERVAL_NO}.stat
#${EXE} ${SCRIPT} --simpoint-profile --simpoint-interval=1000000 --fastmem -c ${BINARY} -o "${ARGS}"
#,ExecMacro,-ExecMicro
