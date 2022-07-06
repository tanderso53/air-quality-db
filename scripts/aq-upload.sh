#! /usr/bin/env bash

umask 0077

if [ -f $1 ]; then
    source $1
fi

## From conf file
app=${app:-./air-quality-db}
port=${port:-333}
ip=${ip:-172.0.0.1}
sqlrole=${sqlrole:-user}
sqlpass=${sqlpass:-user}
sqldb=${sqldb:-air-quality}
sqlhost=${sqlhost:-localhost}
sqldir=${sqldir:-./sql}
tmpdir=${tmpdir:-/tmp}

## Internal variables
tmpfileroot=air-quality-data.json
sqlupdate=round-robin-update.sql
exitreq=0

signal_handler() {
    exitreq=1
    echo "Received interupt, waiting for commands to complete"
    pkill -SIGINT $(basename ${app})
}

trap signal_handler SIGINT

while true; do
    ${app} ${ip} ${port} > ${tmpdir}/${tmpfileroot}

    if [ $? -lt 0 ]; then
	exit -1
    fi

    if [ ${exitreq} -eq 1 ]; then
	exit 0
    fi

    if [ -s ${tmpdir}/${tmpfileroot} ]; then
	cat ${sqldir}/${sqlupdate} | psql -U ${sqlrole} -h ${sqlhost} -d ${sqldb}
    fi
done
