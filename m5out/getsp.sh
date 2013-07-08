#!/bin/bash
if [[ ! $1 ]]; then
    echo "Does not specify benchmark name!"
    exit
elif [[ ! -e simpoint.bb ]]; then
    echo "Simpoint.bb doesn't exit!"
    exit
#elif [[ -e $1.simpoints || -e $1.weights ]]; then
#    echo "Non-empty simpoint file or weight file exists!"
#    exit
fi

SPHOME=/home/wm/workspace/SimPoint.3.2
EXE=${SPHOME}/bin/simpoint
BBVFILE=$1.bb
SIMPOINTSFILE=$1.simpoints
WEIGHTSFILE=$1.weights

cd ${SPHOME}/output
if [[ -e ${SIMPOINTSFILE} || -e ${WEIGHTSFILE} ]]; then
    rm ${SIMPOINTSFILE} ${WEIGHTSFILE}
fi
cd -

cp simpoint.bb ${SPHOME}/input/${BBVFILE}
${EXE} -loadFVFile ${SPHOME}/input/${BBVFILE} -k 1:30 -saveSimpoints ${SPHOME}/output/${SIMPOINTSFILE} -saveSimpointWeights ${SPHOME}/output/${WEIGHTSFILE}
cp ${SPHOME}/output/${SIMPOINTSFILE} ${SPHOME}/output/${WEIGHTSFILE} .
