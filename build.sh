#!/bin/bash

# Compile script for Graveyard Kernel
# Copyright (C) 2023-2024 Christopher K. Irvine (MAdMiZ)

# <---SETUP ENVIRONMENT-->

# Initializing variables
SECONDS=0 # builtin bash timer
TZ=Asia/Kolkata
ZIPNAME="Graveyard-v16-r5x-$(date '+%Y%m%d-%H%M').zip"
TC_DIR="$HOME/tc/zyc_clang"
AK3_DIR="$HOME/android/AnyKernel3"
DEFCONFIG="vendor/RMX1911_defconfig"
export TZ=Asia/Kolkata
export KBUILD_BUILD_USER=MAdMiZ
export KBUILD_BUILD_HOST=BlackArch
export PATH="$TC_DIR/bin:$PATH"

if test -z "$(git rev-parse --show-cdup 2>/dev/null)" &&
   head=$(git rev-parse --verify HEAD 2>/dev/null); then
	ZIPNAME="${ZIPNAME::-4}-$(echo $head | cut -c1-8).zip"
fi

# Patch with latest KernelSU
  echo "Applying latest KernelSU patch"
if ! [ -d "$KERNEL_DIR"/KernelSU ]; then
  curl -LSs "https://raw.githubusercontent.com/tiann/KernelSU/main/kernel/setup.sh" | bash -
else
  echo -e "KernelSU patch failed, stopping build now..."
  exit 1
fi

# <---SETUP CLANG COMPILER/LINKER--->

if [ ! -d "$TC_DIR" ]; then
  mkdir -p "$TC_DIR" || { echo "Failed to create directory $TC_DIR! Aborting..."; exit 1; }
  echo "ZYC Clang 19 not found!"
  # Fetch latest ZYC Clang release
  LATEST_RELEASE_INFO=$(curl -s https://api.github.com/repos/ZyCromerZ/Clang/releases/latest)
  TARBALL_URL=$(echo "$LATEST_RELEASE_INFO" | jq -r '.assets[0].browser_download_url')
  if [ -z "$TARBALL_URL" ]; then
    echo "Failed to fetch tarball URL! Aborting..."
    exit 1
  fi
  echo "Downloading ZYC Clang 19..."
  if ! wget -O zyc_clang.tar.gz "$TARBALL_URL"; then
    echo "Downloading failed! Aborting..."
    exit 1
  fi
  echo "Extracting ZYC Clang 19 to $TC_DIR..."
  if ! tar -xzf zyc_clang.tar.gz -C "$TC_DIR"; then
    echo "Extraction failed! Aborting..."
    exit 1
  fi
  rm zyc_clang.tar.gz
fi

# <---KERNELSU PATCH--->

# Patch with latest KernelSU
curl -LSs "https://raw.githubusercontent.com/tiann/KernelSU/main/kernel/setup.sh" | bash -

# <----START COMPILATION--->

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
make -j$(nproc --all) O=out ARCH=arm64 CC=clang LD=ld.lld AR=llvm-ar AS=llvm-as NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- Image.gz-dtb dtbo.img 2>&1 | tee error.log

if [ -f "out/arch/arm64/boot/Image.gz-dtb" ] && [ -f "out/arch/arm64/boot/dtbo.img" ]; then
  echo -e "\nKernel compiled successfully! Zipping up...\n"
  if [ -d "$AK3_DIR" ]; then
    cp -r "$AK3_DIR" AnyKernel3
  elif ! git clone -q https://github.com/mizdrake7/AnyKernel3; then
    echo -e "\nAnyKernel3 repo not found locally, and cloning failed! Aborting..."
    exit 1
  fi
  cp out/arch/arm64/boot/Image.gz-dtb AnyKernel3
  cp out/arch/arm64/boot/dtbo.img AnyKernel3
  rm -f *zip
  cd AnyKernel3
  git checkout master &> /dev/null
  zip -r9 "../$ZIPNAME" * -x '*.git*' README.md *placeholder
  cd ..
  rm -rf AnyKernel3
  rm -rf out/arch/arm64/boot
  echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s)!"
  echo "Zip: $ZIPNAME"
  # Get the size of the ZIP file in megabytes
  ZIP_SIZE=$(stat -c%s "$ZIPNAME")
  ZIP_SIZE_MB=$(awk "BEGIN {print $ZIP_SIZE/1048576}")
  echo "Zip Size: $ZIP_SIZE_MB MB"

# <---UPLOAD--->

 # Upload the ZIP file
  read -p "Enter 1 to upload the ZIP file to Telegram, or press any key to upload to Oshi..at: " CHOICE
  if ((CHOICE == 1)); then
    read -p "Enter the bot token: " BOT_TOKEN
    echo -e "\nBot Token has been set successfully!"
    echo -e "\nUploading the ZIP file to Telegram..."
    curl --progress-bar -F chat_id="-1001304524669" -F document=@"$ZIPNAME" "https://api.telegram.org/bot$BOT_TOKEN/sendDocument"
    echo -e "\nDone!"
  else
    echo -e "\nUploading the ZIP file to Oshi.at..."
    curl --progress-bar --upload-file "$ZIPNAME" "https://oshi.at/$ZIPNAME"
    echo -e "\nDone!"
  fi
else
  echo -e "\nCompilation failed!"
  exit 1
fi
