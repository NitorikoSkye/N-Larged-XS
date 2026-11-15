#!/bin/sh
# Builds the Android app (platform/android/README.md): the game's core for
# ARM64 and x86-64 (the pc platform's, static, run with --stream), the
# app's Java code, and the APK, signed. Run from the project's root:
#   platform/android/build.sh            -> build/android/CrashBandicootXS.apk
# Needs (Debian/Ubuntu): gcc/g++-aarch64-linux-gnu, binutils-aarch64-linux-gnu,
# libc6-dev-arm64-cross, openjdk-17-jdk-headless, android-sdk-platform-23,
# android-sdk-build-tools, dalvik-exchange, apksigner, zipalign.
set -e
A=aarch64-linux-gnu
OUT=build/android
SDK=/usr/lib/android-sdk
JAR=$SDK/platforms/android-23/android.jar
J=$(nproc)

echo "== the cores"
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-arm64/crashbandicootxs -j$J \
    HOST_CC=$A-gcc HOST_CXX=$A-g++ HOST_NM=$A-nm HOST_CXXFILT=$A-c++filt HOST_OBJCOPY=$A-objcopy
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-x86_64/crashbandicootxs -j$J

rm -rf $OUT/stage $OUT/classes $OUT/res
mkdir -p $OUT/stage/lib/arm64-v8a $OUT/stage/lib/x86_64 $OUT/classes $OUT/res/drawable
$A-strip -o $OUT/stage/lib/arm64-v8a/libcrashxs_core.so build/android-arm64/libcrashxs_core.so
strip -o $OUT/stage/lib/x86_64/libcrashxs_core.so build/android-x86_64/libcrashxs_core.so

echo "== the icon (Crash's HUD head, as the PC port's)"
python3 platform/android/make_icon.py games/xs/graphics/sprites/bank47_icons/f034.png $OUT/res/drawable/icon.png

echo "== the code"
javac -source 8 -target 8 -bootclasspath $JAR -classpath $JAR -d $OUT/classes -Xlint:-options \
    $(find platform/android/src -name '*.java')
dalvik-exchange --dex --output=$OUT/stage/classes.dex $OUT/classes

echo "== the APK"
aapt package -f -M platform/android/AndroidManifest.xml -S $OUT/res -I $JAR -0 arsc -F $OUT/unsigned.apk
(cd $OUT/stage && aapt add ../unsigned.apk classes.dex lib/arm64-v8a/libcrashxs_core.so lib/x86_64/libcrashxs_core.so >/dev/null)
zipalign -p -f 4 $OUT/unsigned.apk $OUT/aligned.apk
KS=platform/android/crashxs.keystore
if [ ! -f $KS ]; then
    keytool -genkeypair -keystore $KS -storepass crashxs -keypass crashxs -alias crashxs -keyalg RSA \
        -keysize 2048 -validity 36500 -dname "CN=Crash Bandicoot XS PC port" >/dev/null 2>&1
fi
apksigner sign --ks $KS --ks-pass pass:crashxs --key-pass pass:crashxs --ks-key-alias crashxs \
    --min-sdk-version 21 --out $OUT/CrashBandicootXS.apk $OUT/aligned.apk
apksigner verify $OUT/CrashBandicootXS.apk
echo "== $OUT/CrashBandicootXS.apk"
