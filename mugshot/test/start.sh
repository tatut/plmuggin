#!/bin/sh
cd $(dirname $0)/..
make
./mugshot test/config 2>1 >> test.log &
