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

        echo "SUMMARY = \"New-defconfig recipe\"" > agroinformer_0.1.bb
        echo "DESCRIPTION = \"Recipe created by bitbake-layers\"" >> agroinformer_0.1.bb
        echo "LICENSE=\"GPLv2\"" >> agroinformer_0.1.bb
        echo "LIC_FILES_CHKSUM = \"file://COPYING;md5=d882afdbb835752c1a5b5b2080253f50\"" >> agroinformer_0.1.bb

        echo "FILESEXTRAPATHS_prepend := \"\${THISDIR}/\${PN}:\"" >> agroinformer_0.1.bb
        echo "SRC_URI += \"file://defconfig \\" >> agroinformer_0.1.bb
        echo "             file://COPYING\"" >> agroinformer_0.1.bb
        echo "S = \"\${WORKDIR}\"" >> agroinformer_0.1.bb
}

if [ -z "$1" ]
then
        echo "Yocto dir path is not set."
        echo "Using: source set_defconfig.sh <yocto_dir_path>"
        sleep 5
        exit 1
else
        echo "yocto dir path: " $1

        current_dir=$PWD
        echo "current dir:" $current_dir

        configure_layer $1

        cp $current_dir/defconfig $1/bbb/meta-agroinformer/recipes-kernel/agroinformer/
        cp $current_dir/COPYING $1/bbb/meta-agroinformer/recipes-kernel/agroinformer/
	echo "IMAGE_INSTALL_append = \"ppp\"" >> $1/bbb/build/conf/local.conf
        source $current_dir/rebuild_kernel.sh $1
        sleep 1
fi

