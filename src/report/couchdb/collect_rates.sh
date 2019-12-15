#!/bin/sh
set -e

if [ `which jq` = "" ] ; then 
	echo "Error: The program 'jq' not found"
	exit 1
fi

if [ `which curl` = "" ] ; then 
	echo "Error: The program 'curl' not found"
	exit 1
fi


. `dirname $0`/collect_rates.conf

CRYPTO=`curl -s "https://billboard.service.cryptowat.ch/assets?quote=btc&limit=10000"`
FOREX=`curl -s "https://simplefx.com/utils/instruments.json"`
COMBINED="{\"crypto\":$CRYPTO,\"forex\":$FOREX}"
echo $COMBINED | curl --data-binary @- -H "Content-Type: application/json" -X PUT $DATABASE/_design/control/_update/updateRates/rates
echo "";

