#!/bin/sh
# Builds the Android app (platform/android/README.md): the game's core for
# ARM64 and x86-64 (the pc platform's, static, run with --stream), each
# with its game data blanked out and the package that makes it back from
# the player's ROM, the app's Java code, and the APK, signed. The ROM is
# needed to make the packages; the APK holds no part of it. From the
# project's root:
#   platform/android/build.sh ROM      -> build/android/NLargedXS-<version>-dev.apk
#   NLXS_KEYSTORE=release.p12 NLXS_KEYSTORE_PASS_FILE=release.password \
#   platform/android/build.sh ROM      -> build/android/NLargedXS-<version>.apk (a release)
# Each ABI gets two cores: musl's (libcrashxs_core.so, the one the app
# starts) and glibc's (libcrashxs_gnu.so, the one it tries if the first
# stops before its first picture).
# Needs (Debian/Ubuntu): gcc/g++-aarch64-linux-gnu, binutils-aarch64-linux-gnu,
# libc6-dev-arm64-cross, openjdk-17-jdk-headless, android-sdk-platform-23,
# android-sdk-build-tools, dalvik-exchange, apksigner, zipalign, librsvg2-bin; and
# zig's musl (pip install ziglang).
set -e
ROM=${1:?usage: platform/android/build.sh ROM}
A=aarch64-linux-gnu
OUT=build/android
SDK=/usr/lib/android-sdk
JAR=$SDK/platforms/android-23/android.jar
J=$(nproc)
VERSION=$(sed -n 's/^#define HOST_PORT_VERSION "v\(.*\)"/\1/p' platform/pc/src/host_platform.h)
CODE=$(echo "$VERSION" | awk -F. '{ printf "%d", $1 * 100 + $2 }')
MC=platform/android/musl-cc

echo "== the cores (v$VERSION)"
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-arm64/crashbandicootxs -j$J \
    HOST_CC=$A-gcc HOST_CXX=$A-g++ HOST_NM=$A-nm HOST_CXXFILT=$A-c++filt HOST_OBJCOPY=$A-objcopy
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-x86_64/crashbandicootxs -j$J
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-musl-arm64/crashbandicootxs -j$J \
    HOST_CC="$MC aarch64 gcc" HOST_CXX="$MC aarch64 g++" HOST_NM=$A-nm HOST_CXXFILT=$A-c++filt HOST_OBJCOPY=$A-objcopy
make PLATFORM=pc HOST_ANDROID=1 HOST_SDL=0 OBJ_DIR=build/android-musl-x64/crashbandicootxs -j$J \
    HOST_CC="$MC x86_64 gcc" HOST_CXX="$MC x86_64 g++"

rm -rf $OUT/stage $OUT/classes $OUT/res $OUT/assets
mkdir -p $OUT/stage/lib/arm64-v8a $OUT/stage/lib/x86_64 $OUT/classes $OUT/res/drawable \
    $OUT/assets/arm64-v8a $OUT/assets/x86_64 $OUT/assets/icons

echo "== the game data out of the cores, and their packages"
# core build dir, ABI, name in the APK, nm, strip
pack() {
    python3 platform/android/tools/make_core_package.py --rom "$ROM" --core build/$1/libcrashxs_core.so \
        --map build/$1/libcrashxs_core.so.map --nm $4 --strip $5 --obj-dir build/$1/crashbandicootxs \
        --blanked $OUT/stage/lib/$2/$3.so --package $OUT/assets/$2/$3.pkg
}
# one at a time: each holds an index of the whole ROM
pack android-musl-arm64 arm64-v8a libcrashxs_core $A-nm $A-strip
pack android-arm64 arm64-v8a libcrashxs_gnu $A-nm $A-strip
pack android-musl-x64 x86_64 libcrashxs_core nm strip
pack android-x86_64 x86_64 libcrashxs_gnu nm strip

echo "== the touch buttons' icons (icons/*.svg, white)"
mkdir -p $OUT/assets/icons
for f in platform/android/icons/*.svg; do
    rsvg-convert -w 160 -h 160 "$f" -o $OUT/assets/icons/$(basename "$f" .svg).png
done

echo "== the icon (Crash's HUD head, as the PC port's)"
python3 platform/android/make_icon.py games/xs/graphics/sprites/bank47_icons/f034.png $OUT/res/drawable/icon.png

echo "== the code"
javac -source 8 -target 8 -bootclasspath $JAR -classpath $JAR -d $OUT/classes -Xlint:-options \
    $(find platform/android/src -name '*.java')
dalvik-exchange --dex --output=$OUT/stage/classes.dex $OUT/classes

echo "== the APK"
sed -e "s/android:versionCode=\"[0-9]*\"/android:versionCode=\"$CODE\"/" \
    -e "s/android:versionName=\"[^\"]*\"/android:versionName=\"$VERSION\"/" \
    platform/android/AndroidManifest.xml > $OUT/AndroidManifest.xml
aapt package -f -M $OUT/AndroidManifest.xml -S $OUT/res -A $OUT/assets -I $JAR -0 arsc -0 pkg \
    -F $OUT/unsigned.apk
(cd $OUT/stage && aapt add ../unsigned.apk classes.dex lib/*/*.so >/dev/null)
zipalign -p -f 4 $OUT/unsigned.apk $OUT/aligned.apk
# The signature. A release is signed with the release key, which is
# never in the repository (anyone with it could sign an "update" of the
# app): NLXS_KEYSTORE names it, NLXS_KEYSTORE_PASS_FILE the file with its
# password (NLXS_KEY_ALIAS: nlxs). Without them the APK is signed with a
# development key made once in build/ and named -dev: fine to test, but
# it won't update a release (nor a release it).
if [ -n "$NLXS_KEYSTORE" ]; then
    KS=$NLXS_KEYSTORE
    PASS=$(cat "${NLXS_KEYSTORE_PASS_FILE:?NLXS_KEYSTORE_PASS_FILE: the release key's password file}")
    ALIAS=${NLXS_KEY_ALIAS:-nlxs}
    APK=$OUT/NLargedXS-v$VERSION.apk
else
    KS=$OUT/dev.keystore
    PASS=nlxs-dev
    ALIAS=dev
    if [ ! -f $KS ]; then
        keytool -genkeypair -storetype PKCS12 -keystore $KS -storepass $PASS -keypass $PASS -alias $ALIAS \
            -keyalg RSA -keysize 2048 -validity 36500 -dname "CN=N. Larged XS development build" >/dev/null 2>&1
    fi
    APK=$OUT/NLargedXS-v$VERSION-dev.apk
    echo "== no NLXS_KEYSTORE: signed with the development key ($APK), not for release"
fi
apksigner sign --ks "$KS" --ks-pass "pass:$PASS" --key-pass "pass:$PASS" --ks-key-alias "$ALIAS" \
    --min-sdk-version 21 --out $APK $OUT/aligned.apk
apksigner verify $APK
echo "== $APK"
