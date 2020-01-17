#!/bin/sh
# Script for unloading the sir kernel module
# Using template from "Linux Device Drivers 3rd Ed." by J. Corbert, A. Rubini, G. Kroah-Hartman
module="sir"
device="sir"

# invoke rmmod with all arguments we got
/sbin/rmmod $module $* || exit 1

# Remove stale nodes

rm -f /dev/${device} /dev/${device}0 
