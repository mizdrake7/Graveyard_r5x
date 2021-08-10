SECONDS=0 # builtin bash timer
ZIPNAME="Trashed_$(date '+%Y%m%d-%H%M').zip"
TC_DIR="$HOME/r5x/proton-clang"
DEFCONFIG="vendor/RMX1911_defconfig"

export PATH="$TC_DIR/bin:$PATH"

if ! [ -d "$TC_DIR" ]; then
echo "Proton clang not found! Cloning to $TC_DIR..."
if ! git clone -q --depth=1 --single-branch https://github.com/kdrag0n/proton-clang $TC_DIR; then
echo "Cloning failed! Aborting..."

fi
fi

export KBUILD_BUILD_USER=henrysg
export KBUILD_BUILD_HOST=workspace

if [[ $1 = "-r" || $1 = "--regen" ]]; then
make O=out ARCH=arm64 $DEFCONFIG savedefconfig
cp out/defconfig arch/arm64/configs/$DEFCONFIG
exit
fi

if [[ $1 = "-c" || $1 = "--clean" ]]; then
rm -rf out
fi

mkdir -p out
make O=out ARCH=arm64 $DEFCONFIG

echo -e "\nStarting compilation...\n"
make -j$(nproc --all) CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- CC=clang O=out ARCH=arm64 2>&1 | tee log.txt
if [ -f "out/arch/arm64/boot/Image.gz-dtb" ] && [ -f "out/arch/arm64/boot/dtbo.img" ]; then
echo -e "\nKernel compiled succesfully! Zipping up...\n"

fi
cp out/arch/arm64/boot/Image.gz-dtb /home/henrysg/r5x/any
cp out/arch/arm64/boot/dtbo.img /home/henrysg/r5x/any
cd /home/henrysg/r5x/any
rm -f *.zip
zip -r9 "../$ZIPNAME" * -x '*.git*' README.md *placeholder
echo -e "\n REMOVING Image.gz-dtb and dtbo.img in Anykernel folder\n"
rm -rf /home/henrysg/r5x/any/Image.gz-dtb && rm -rf /home/henrysg/r5x/any/dtbo.img
echo -e "\n REMOVING Image.gz-dtb and dtbo.img in out folder\n"
cd /home/henrysg/r5x/OSS/out/arch/arm64/boot
rm -rf Image.gz-dtb && rm -rf dtbo.img
echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s) !"
echo "Zip: $ZIPNAME"
