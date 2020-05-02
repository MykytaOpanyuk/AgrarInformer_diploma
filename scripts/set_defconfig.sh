#!/bin/bash
function configure_layer()
{
        cd $1/bbb/build
        # Set env for bitbake command
        source ../../poky-zeus/oe-init-build-env .

        # Add new layer for yocto project
        bitbake-layers create-layer $1/bbb/meta-agroinformer

        cd $1/bbb/meta-agroinformer
        mv recipes-example/ recipes-kernel/
        cd recipes-kernel
        mv example/ agroinformer/
        cd agroinformer
        mv example_0.1.bb agroinformer_0.1.bb

        "FILESEXTRAPATHS_prepend := \"\${THISDIR}/\${PN}:" > agroinformer_0.1.bb
        "SRC_URI += \"file://defconfig\"" >> agroinformer_0.1.bb
}

if [ -z "$1" ]
then
        echo "Yocto dir path is not set."
        echo "Using: ./set_defconfig.sh <yocto_dir_path>"
        sleep 5
        exit 1
else
        echo "yocto dir path: " $1

        current_dir=$PWD
        echo "current dir:" $current_dir

        configure_layer $1

        cp $current_dir/defconfig $1/bbb/meta-agroinformer/recipes-kernel/agroinformer/
        source $current_dir/rebuild_kernel.sh $1
        sleep 1
fi

