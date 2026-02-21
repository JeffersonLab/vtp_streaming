# CSH Environment setup for SBS CODA 3

########################################
# User Specific Configuration
########################################
setenv CODA_SCRIPTS ${HOME}/coda/coda_scripts

setenv CODA /site/coda/3.10_devel
source $CODA/.setup

setenv SESSION session
setenv EXPID expid

setenv COOL_HOME ${HOME}/coda/cool
setenv JAVA_HOME ${HOME}/jdk1.8.0_152

setenv REMEX_CMSG_HOST thishost.jlab.org
setenv REMEX_CMSG_PASSWORD ${EXPID}

setenv CODA_COMPONENT_TABLE ${CODA_SCRIPTS}/config/${EXPID}/coda_component_table.cfg

# Add config scripts to path
set path = ($CODA_SCRIPTS $path)
