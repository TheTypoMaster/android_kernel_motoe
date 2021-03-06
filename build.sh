#!/bin/sh

rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/boot/zImage-dtb
rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/*.ko
rm -f /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/pronto/pronto_wlan.ko
rm -f /home/ryanandri/android/cm12.1/arch/arm/boot/zImage

mv /home/ryanandri/android/cm12.1/arch/arm/boot/zImage-dtb /home/ryanandri/android/clarity-cm12.1-condor-r1/boot

# get modules into one place
find -name "*.ko" -exec cp {} /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules \;
sleep 2

# Remove Unneeded
/home/ryanandri/android/linaro-4.9.4/bin/arm-cortex_a7-linux-gnueabihf-strip --strip-unneeded /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/*.ko

# move to proper place
mv /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/wlan.ko /home/ryanandri/android/clarity-cm12.1-condor-r1/system/lib/modules/pronto/pronto_wlan.ko

