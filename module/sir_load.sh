#!/bin/sh
# Script for loading the sir kernel module
# Using template from "Linux Device Drivers 3rd Ed." by J. Corbert, A. Rubini, G. Kroah-Hartman

module="sir"
device="sir"
mode="444"

# Group: since distributions do it differently, look for wheel or use staff
if grep -q '^staff:' /etc/group; then
    group="staff"
else
    group="wheel"
fi

# invoke insmod with all arguments we got
# and use a pathname, as insmod doesn't look in . by default
/sbin/insmod ./$module.ko $* || exit 1

# retrieve major number
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)

# Remove stale nodes and replace them, then give gid and perms
# Usually the script is shorter, it's scull that has several devices in it.

rm -f /dev/${device}0
mknod /dev/${device}0 c $major 0
ln -sf ${device}0 /dev/${device}
chgrp $group /dev/${device}0 
chmod $mode  /dev/${device}0
