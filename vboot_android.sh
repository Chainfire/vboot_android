# put everything in aosp/external/vboot_android and call this script from aosp/

. build/envsetup.sh

PREBUILT=external/vboot_android/prebuilt/
rm -rf $PREBUILT

lunch aosp_arm-eng
make -j12 futility-android
mkdir -p $PREBUILT/arm
cp out/target/product/generic/system/bin/futility-android $PREBUILT/arm/futility

lunch aosp_arm64-eng
make -j12 futility-android
mkdir -p $PREBUILT/arm64
cp out/target/product/generic_arm64/system/bin/futility-android $PREBUILT/arm64/futility

lunch aosp_x86-eng
make -j12 futility-android
mkdir -p $PREBUILT/x86
cp out/target/product/generic_x86/system/bin/futility-android $PREBUILT/x86/futility

lunch aosp_x86_64-eng
make -j12 futility-android
mkdir -p $PREBUILT/x64
cp out/target/product/generic_x86_64/system/bin/futility-android $PREBUILT/x64/futility

lunch aosp_mips-eng
make -j12 futility-android
mkdir -p $PREBUILT/mips
cp out/target/product/generic_mips/system/bin/futility-android $PREBUILT/mips/futility

lunch aosp_mips64-eng
make -j12 futility-android
mkdir -p $PREBUILT/mips64
cp out/target/product/generic_mips64/system/bin/futility-android $PREBUILT/mips64/futility

