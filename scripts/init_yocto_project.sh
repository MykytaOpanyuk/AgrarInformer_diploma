#!/bin/bash

function set_en_and_build()
{
        mkdir -p $1/bbb/build/conf
        # Set env for bitbake command
        cd $1/bbb/build
        source ../../poky-zeus/oe-init-build-env .
        source $2/bbb_config.sh $1
        # clean status of kernel from tmp/cache
        bitbake -c cleansstate console-image
        # build
        bitbake console-image
}

function install_packages()
{
        echo "current dir:" $1

        # check that the filename was supplied (keeping it simple for the example)
        set -o nounset

        package_file=$1/package_file.txt

        # initialize the package variable
        packs=''

        # read the lines of the package file
        while IFS= read -r line; do
                packs+=" $line"
        done < $package_file

        # apt install all of the packages
        sudo apt-get install -y $packs
        # create some links for it in /usr/bin
        sudo ln -sf /usr/bin/python2.7 /usr/bin/python2
        #change the default Ubuntu shell from dash to bash (select "No")
        sudo dpkg-reconfigure dash
}

function install_yocto()
{
        cd $1
        # Clone the repositories
        git clone -b zeus git://git.yoctoproject.org/poky.git poky-zeus
        cd poky-zeus
        git clone -b zeus git://git.openembedded.org/meta-openembedded
        git clone -b zeus https://github.com/meta-qt5/meta-qt5.git
        git clone -b zeus git://git.yoctoproject.org/meta-security.git
        cd .. && mkdir bbb && cd bbb
        git clone -b zeus git://github.com/jumpnow/meta-bbb
}

if [ -z "$1" ]
then
        echo "Dir path for yocto project is not set."
        echo "Using: ./init_yocto_project.sh <dir_path>"
        sleep 5
        exit 1
else
        echo "yocto project dir path: " $1
        current_dir=$PWD
        install_packages $current_dir
        install_yocto $1
        set_en_and_build $1 $current_dir

        sleep 1
fi


