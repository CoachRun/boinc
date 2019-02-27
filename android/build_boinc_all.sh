#!/bin/sh
set -e

#
# See: http://boinc.berkeley.edu/trac/wiki/AndroidBuildClient#
#

# Script to compile everything BOINC needs for Android

./build_boinc_arm.sh
./build_boinc_arm64.sh
./build_boinc_x86.sh
./build_boinc_x86_64.sh
./build_boinc_mips.sh
./build_boinc_mips64.sh
