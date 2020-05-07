#!/bin/bash
if [ -z "$1" ]
then
        echo "Yocto dir path is not set."
        echo "Using: ./set_defconfig.sh <yocto_dir_path>"
        sleep 5
        exit 1
else
        echo "yocto dir path: " $1
        # Set env for bitbake command
        cd $1/bbb/build
        source ../../poky-zeus/oe-init-build-env .
        bitbake -c devshell virtual/kernel
        sleep 1
fi
