#!/bin/bash

# Compile script for Graveyard Kernel
# Copyright (C) 2023-2024 Christopher K.

# <---SETUP ENVIRONMENT-->

# Initializing Directories
mkdir -p $HOME/tc/weebx_clang

# Initializing variables
SECONDS=0 # builtin bash timer
ZIPNAME="Graveyard-v15-r5x-$(date '+%Y%m%d-%H%M').zip"
TC_DIR="$HOME/tc/weebx_clang"
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

# Telegram variables
send_telegram_message(){
     curl -s -X POST "https://api.telegram.org/bot$token/sendMessage" \
     -d chat_id="$chat_id" \
     -d "text=$1"
}

# <---SETUP CLANG COMPILER/LINKER--->

if ! [ -d "$TC_DIR" ]; then
  echo "WeebX clang not found!"
  # Determine glibc version
  glibc_version=$(ldd --version | awk '/ldd/{print $NF}')
  # Compare glibc version
  if [[ "$glibc_version" > "2.35" ]]; then
    # glibc version is newer than 2.35
    trb_clang_branch="17"
  else
    # glibc version is older than 2.36
    trb_clang_branch="16"
  fi
  # Clone WeebX Clang repository
  echo "Cloning WeebX Clang $trb_clang_branch to $TC_DIR..."
  if ! curl -L https://github.com/XSans0/WeebX-Clang/releases/download/WeebX-Clang-18.0.0-release/WeebX-Clang-18.0.0.tar.gz | tar -xzvf - -C "$TC_DIR" && echo "Extraction Done"; then
    echo "Cloning failed! Aborting..."
    exit 1
  fi
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
# Notify about compilation start
send_telegram_message "Starting compilation..."
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
  echo "Zip: $ZIPNAME"
  # Get the size of the ZIP file in megabytes
  ZIP_SIZE=$(stat -c%s "$ZIPNAME")
  ZIP_SIZE_MB=$(awk "BEGIN {print $ZIP_SIZE/1048576}")
  echo "Zip Size: $ZIP_SIZE_MB MB"

# <---UPLOAD--->

 # Upload the ZIP file to Telegram
  compilation_successfull="Completed in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s) !"
  curl -F chat_id="$chat_id" -F document=@"$ZIPNAME" -F caption="$compilation_successfull" "https://api.telegram.org/bot$token/sendDocument"
else
  send_telegram_message "Compilation failed!"
  exit 1
fi
