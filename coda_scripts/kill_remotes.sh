#!/bin/bash
#
#  Kill the remote_vme's
#
#
#

. coda_conf_functions

# Get list of ROC hosts
coda_conf_get_component_list TS
if [ $? == 1 ] ; then
    ROC_hosts=${CODA_HOSTNAME_LIST[@]}


    # kill the remote_vme connections for the ROCs
    echo "Killing local remote_vme startTS.sh connections"
    pkill -U $UID -f "remote_vme.*startTS.sh"


    # now kill the ROC, if it's still runnin gon the remote host
    for host in $ROC_hosts
    do
	echo "Killing ROC on $host"
	ssh $host pkill coda_ts
    done
fi

# Get list of ROC hosts
coda_conf_get_component_list ROC
if [ $? == 1 ] ; then
    ROC_hosts=${CODA_HOSTNAME_LIST[@]}


    # kill the remote_vme connections for the ROCs
    echo "Killing local remote_vme startROC.sh connections"
    pkill -U $UID -f "remote_vme.*startROC.sh"


    # now kill the ROC, if it's still runnin gon the remote host
    for host in $ROC_hosts
    do
	echo "Killing ROC on $host"
	ssh $host pkill coda_roc
    done
fi

# Get list of FPGA hosts
coda_conf_get_component_list FPGA
if [ $? == 1 ] ; then
    ROC_hosts=${CODA_HOSTNAME_LIST[@]}


    # kill the remote_vme connections for the FPGAs
    echo "Killing local remote_vme startFPGA.sh connections"
    pkill -U $UID -f "remote_vme.*startFPGA.sh"


    # now kill the ROC, if it's still runnin gon the remote host
    for host in $ROC_hosts
    do
	echo "Killing ROC on $host"
	ssh $host pkill coda_roc
    done
fi

# Get list of PEB hosts
coda_conf_get_component_list PEB
if [ $? == 1 ] ; then
    PEB_hosts=${CODA_HOSTNAME_LIST[@]}

    # kill the remote_vme connections for the PEBs
    echo "Killing local remote_vme startPEB.sh connections"
    pkill -U $UID -f "remote_vme.*startPEB.sh"

    # now kill the ROC, if it's still runnin gon the remote host
    for host in $PEB_hosts
    do
	echo "Killing PEB on $host"
	ssh $host pkill -f org.jlab.coda.emu.EmuFactory
    done
fi

# Get list of PAGG hosts
coda_conf_get_component_list PAGG
if [ $? == 1 ] ; then
    PAGG_hosts=${CODA_HOSTNAME_LIST[@]}

    # kill the remote_vme connections for the PAGGs
    echo "Killing local remote_vme startPAGG.sh connections"
    pkill -U $UID -f "remote_vme.*startPAGG.sh"

    # now kill the ROC, if it's still runnin gon the remote host
    for host in $PAGG_hosts
    do
	echo "Killing PAGG on $host"
	ssh $host pkill -f org.jlab.coda.emu.EmuFactory
    done
fi
