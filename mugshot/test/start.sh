#!/bin/sh
cd $(dirname $0)/..
make
./mugshot test/config > test.log 2>&1 &
