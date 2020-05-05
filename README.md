# AgrarInformer_diploma
AgrarInformer application for monitoring information from
different devices (moisture sensor, temperature sensor,
switch and etc.) and send it to remote server(SSH, HTTP connection).

1)INFO:
All system based on yocto project (https://www.yoctoproject.org/).
The Yocto Project (YP) is an open source collaboration project
that helps developers create custom Linux-based systems regardless
of the hardware architecture.

The project provides a flexible set of tools and a space where
embedded developers worldwide can share technologies, software
stacks, configurations, and best practices that can be used to
create tailored Linux images for embedded and IOT devices, or
anywhere a customized Linux OS is needed.

2)INSTALLATION:
For installing yocto_project you need to use bash-scripts
in the right sequence:
    You need to select dir for yocto project and execute:
        1.$ source init_yocto_project.sh <yocto_dir_path>
    Then you need to rewrite path for bblayers.conf for correct
    compile process.
        2.$ source bbb_config.sh <yocto_dir_path>
    After all for PPP connection you need to install defconfig
    with enabled PPP connection:
        3.$ source set_defconfig.sh <yocto_dir_path>
    Last one - you can use prepare_sd_card.sh for flashing SD card
    with rootfs, dts and boot images:
        4.$ ./prepare_sd_card.sh <yocto_dir_path> <sd_dev>

3)META LAYERS:
We can use different meta-layers for building linux kernel with
correct configuration for current device:
- A Yocto meta-layer for BeagleBones http://www.jumpnowtek.com:
https://github.com/jumpnow/meta-bbb
- Yocto BSP layer for the Raspberry Pi boards http://www.raspberrypi.org/:
https://github.com/agherzan/meta-raspberrypi
And etc.
About drivers - you can provide drivers to kernel like an meta-* layer with
recipe for cross-compiling. You can find out some examples of how write down,
add and build meta-driver layer for yocto project in:
--------------------------------------------------------------
$ cd meta_layers
$ vim README
--------------------------------------------------------------
Anyway you can use script: set_defconfig.sh as an example
how to create, add and build bitbake layer for yocto-project.

4)ARM CROSS-COMPILIER SDK
After install and build yocto project we need to install SDK for building
kernel modules (DHT11 driver, matrix driver and etc.):
After bitbake meta-toolchain the script to install the
toolchain package is located under $yoctoproject/bbb/build:
--------------------------------------------------------------
$ ls tmp/deploy/sdk/poky-eglibc-x86_64-arm-toolchain-1.4.1.sh
   tmp/deploy/sdk/poky-eglibc-x86_64-arm-toolchain-1.4.1.sh
--------------------------------------------------------------
In order to install it:
--------------------------------------------------------------
$ source poky-eglibc-x86_64-arm-toolchain-1.4.1.sh

    [sudo] password for daiane:

    Enter target directory for SDK (default: /opt/poky/1.4.1):
    You are about to install the SDK to "/opt/poky/1.4.1". Proceed[Y/n]?y
    Extracting SDK...done
    Setting it up...done
    SDK has been successfully set up and is ready to be used.
--------------------------------------------------------------

More info: https://community.nxp.com/docs/DOC-95225
           https://community.nxp.com/docs/DOC-95122
 
5)PPP CONFIGURATION:
SIM800L manual:
http://codius.ru/articles/GSM_%D0%BC%D0%BE%D0%B4%D1%83%D0%BB%D1%8C_SIM800L_%D1%87%D0%B0%D1%81%D1%82%D1%8C_1
You can find out info of SIM800L (Pinout + datasheet) in info directory.

To connect the SIM800L to the Internet, we need to add PPP support to the Linux kernel, add the ppp package to our distribution kit and write a recipe extension file for it that will tell bitbake how and where to install the necessary files for pppd and chat in our distribution kit, being part of the ppp package.
2 ways to have correct kernel defconfig:
1)Use set_defconfig.sh script.
2)(By yourself)To configure the kernel, execute the command:
--------------------------------------------------------------
~/yocto-project/bbb/build$ bitbake virtual/kernel -c menuconfig
--------------------------------------------------------------
You should have configuration like this:
--------------------------------------------------------------
Device drivers --->
    [*] Network device support --->
        <M> PPP (point-to-point protocol) support
            <M> PPP BSD-Compress compression
            <M> PPP Deflate compression
            <*> PPP filtering
            <M> PPP MPPE compression (encryption)
            <*> PPP multilink support
            <M> PPP over Ethernet
            <M> PPP support for async serial ports
            <M> PPP support for sync tty ports
--------------------------------------------------------------
After configuring you need to save new defconfig:
--------------------------------------------------------------
~/yocto-project/bbb/build$ bitbake virtual/kernel -c savedefconfig
--------------------------------------------------------------

After that you should to modify bbb/build/conf/local.conf, add line to the end of file:
IMAGE_INSTALL_append = " ppp"
And then rebuild bbb target:
--------------------------------------------------------------
~/yocto-project/bbb/build$ bitbake -c cleanall console-image
~/yocto-project/bbb/build$ bitbake console-image
--------------------------------------------------------------
