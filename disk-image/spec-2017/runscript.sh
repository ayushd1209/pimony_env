#!/bin/sh

cd /home/gem5/spec2017
source shrc
PAYLOAD=./payload.sh
m5 readfile > "$PAYLOAD"

chmod +x "$PAYLOAD"
exec /bin/bash "$PAYLOAD"
