#!/bin/bash

if [ $# -ne 1 ] ; then
    echo >&2 "Usage: $0 <destination-dir>"
    exit 1
fi
testdir=$1

if [ "$EUID" -ne 0 ] ; then
    echo >&2 "This script needs root permission to read device info"
    exit 1
fi

# /dev/cpu/*/cpuid
if [ -e /dev/cpu/0/cpuid ] ; then
    for d in /dev/cpu/* ; do
        mkdir -p "$1/$d"
        dd if="$d/cpuid" of="$1/$d/cpuid" bs=16 count=1
    done
fi

# /proc/cpuinfo
mkdir -p "$1/proc"
cp /proc/cpuinfo "$1/proc/cpuinfo"

# /proc/net/dev
mkdir -p "$1/proc/net"
cp /proc/net/dev "$1/proc/net/dev"

# /proc/sys/kernel, /proc/sys/abi
mkdir -p "$1/proc/sys"
cp -dR /proc/sys/kernel /proc/sys/abi "$1/proc/sys/"

# /proc/device-tree
if [ -e /proc/device-tree ] ; then
    mkdir -p "$1/proc"
    cp -dR /proc/device-tree "$1/proc/device-tree"
fi

# /sys/devices
mkdir -p "$1/sys"
cp -dR /sys/devices "$1/sys/devices"

# /sys/bus/pci
mkdir -p "$1/sys/bus"
cp -dR /sys/bus/pci "$1/sys/bus/pci"

# /sys/kernel/debug/usb/devices
mkdir -p "$1/sys/kernel/debug/usb"
cp /sys/kernel/debug/usb/devices "$1/sys/kernel/debug/usb/devices"

# sysconf
mkdir -p "$1/sysconf"
./dump_sysconf.py "$1"
