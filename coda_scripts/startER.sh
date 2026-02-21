#!/bin/bash
#
# Shell script to start coda_emu_*
#
#  If no arguments, use coda_conf_functions to get EMU information
#
#  Otherwise:
#
#   startEMU.sh EMUNAME EMUOPTIONS
#
#   Where EMUNAME is the name of the EMU component to start
#   and EMUOPTIONS (optional) are any other arguments to copy to the coda_emu_*
#   commandline
#

ARGS=$@

DEFAULT="-i -v"
PROG=$(basename $0)
TYPE=
CODA_EMU=

case "$PROG" in
    "startER.sh" )
	TYPE=ER
	CODA_EMU=coda_emu_er
	;;

    "startSEB.sh" )
	TYPE=SEB
	CODA_EMU=coda_emu_seb
	;;

    "startDC.sh" )
	TYPE=DC
	CODA_EMU=coda_emu_dc
	;;

    "startPEB.sh" )
	TYPE=PEB
	CODA_EMU=coda_emu_peb
	;;

    * )
	TYPE=PEB
	CODA_EMU=coda_emu_peb
	;;
esac



HOSTNAME=$(hostname -s)

# CODA 3 EMU needs JAVA_HOME define
if [ -z $JAVA_HOME ]; then
    echo "ERROR: \$JAVA_HOME not defined!"
    exit 1
fi

if [ ${#@} -gt 0 ]; then
    # Get the EMU component name
    EMUNAME=$1

    # Get this EMU's commandline option
    shift 1
    EMUOPTION=$@
else

    . coda_conf_functions

    # Get the EMU component name
    codaconf_get_component_name $HOSTNAME $TYPE
    EMUNAME=$CODA_COMPONENT_NAME

    # Get this ROC's commandline option
    codaconf_get_name_option $HOSTNAME $EMUNAME
    EMUOPTION=$CODA_COMPONENT_OPTION

fi

EMU_ACTIVE=$(pgrep -U $UID ${CODA_EMU})
if [ -n "$EMU_ACTIVE" ]; then
    echo "WARNING: ${CODA_EMU} already running"
    echo "         killing them"
    killall -v ${CODA_EMU}
fi


echo "************************************************************"
echo "Starting EMU on" $HOSTNAME
echo "   Commandline =" $ARGS
echo "   SESSION     =" $SESSION
echo "   EXPID       =" $EXPID
echo "   EMU name    =" $EMUNAME
echo "   EMU option  =" $EMUOPTION
echo "************************************************************"
${CODA_EMU} $EMUNAME $EMUOPTION
