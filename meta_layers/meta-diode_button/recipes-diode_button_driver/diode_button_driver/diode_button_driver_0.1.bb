SUMMARY = "bitbake-layers recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LICENSE="GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=d882afdbb835752c1a5b5b2080253f50"

inherit module

SRC_URI = "file://Makefile \
           file://diode_button.c \
           file://diode_button.h \
           file://COPYING \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

RPROVIDES_${PN} += "kernel-module-diode_button_driver"
