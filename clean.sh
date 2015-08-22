#!/bin/sh

rm -f /home/ryanandri/android/cm12.1/arch/arm/mach-msm/smd_rpc_sym.c
make clean && make mrproper
rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/boot/zImage-dtb
rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/*.ko
rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/pronto/pronto_wlan.ko

