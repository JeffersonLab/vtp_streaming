#!/bin/bash -f

#Starts Afecs platform
# author: vhg, 23-jul-2009
#         Moffit, 01aug2018

source $HOME/coda/coda_scripts/setupCODA3.bash

#DTIME=$(date +%d%b%Y_%R)
#OUTFILE=/scr/idaq/platform/PLATFORM_output_${DTIME}.txt
OUTFILE=/dev/null

# set classpath
export CLASSPATH="$CODA/common/jar/*:$CODA/common/jar/jena/*"

# start the platform
exec &> >(tee $OUTFILE)
${JAVA_HOME}/bin/java -Xms200m -Xmx2048m -XX:-UseConcMarkSweepGC -Djava.net.preferIPv4Stack=true org.jlab.coda.afecs.platform.APlatform $1 $2 $3 &
JAVAPID=$!

trap "kill $JAVAPID" INT TERM

wait
