#!/bin/bash

function ponyfyer() {
    local search="\${HOME}"
    local replace=$1
    # Note the double quotes
    sed -i "s/${search}/${replace}/g" conf/bblayers.conf
}

if [ -z "$1" ]
then
        echo "Dir path for yocto project is not set."
        echo "Using: ./bbb_config.sh <dir_path>"
        sleep 5
        exit 1
else
        echo "yocto project dir path: " $1
        mkdir -p $1/bbb/build/conf
        # Set env for bitbake command
        cd $1/bbb/build
        source ../../poky-zeus/oe-init-build-env .
        cp ../../meta-bbb/conf/local.conf.sample conf/local.conf
        cp ../../meta-bbb/conf/bblayers.conf.sample conf/bblayers.conf

        ponyfyer $1

        sleep 1
fi

