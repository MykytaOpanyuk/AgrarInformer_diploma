SUMMARY = "New-defconfig recipe"
DESCRIPTION = "Recipe created by bitbake-layers"
LICENSE="GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=d882afdbb835752c1a5b5b2080253f50"

FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://defconfig \
            file://COPYING"

S = "${WORKDIR}"
