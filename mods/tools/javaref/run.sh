#!/bin/bash
# usage: run.sh MainClass args...
export JAVA_TOOL_OPTIONS=
cd /tmp/jh
CP=$(cat cp.txt)
exec java -XX:+UnlockDiagnosticVMOptions -XX:-BytecodeVerificationRemote -XX:-BytecodeVerificationLocal -Xss16m -Xms3g -Xmn2g -XX:+UseSerialGC -cp .:$CP "$@" 2>&1 | grep -v "^Picked"
