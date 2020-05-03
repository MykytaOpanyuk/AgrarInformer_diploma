if [ -z "$1" ]
then
        echo "Yocto dir path is not set."
        echo "Using: ./set_defconfig.sh <yocto_dir_path> <sd_dev_path>"
        sleep 5
        exit 1
else
        echo "yocto dir path: " $1
fi

if [ -z "$2" ]
then
        echo "SD dev is not set."
        echo "Using: ./prepare_sd_card.sh <yocto_dir_path> <sd_dev>"
        sleep 5
        exit 1
else
        echo "SD dev path: " $2
fi

#sudo umount /dev/$21
#sudo umount /dev/$22

cd $1/bbb/meta-bbb/scripts
sudo ./mk2parts.sh $2
sudo mkdir /media/card
# export a tmp directory, which contains bbb images
export OETMP=$1/bbb/build/tmp
./copy_boot.sh $2
./copy_rootfs.sh $2 console
sleep 5
