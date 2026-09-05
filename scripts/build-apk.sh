#!/usr/bin/env bash
# Сборка KengaOS Mobile APK — WebView-оболочка поверх Android (без gradle).
# Этап «оболочка»: ставится как обычное приложение / домашний экран,
# данные телефона не трогает. Требует: Android SDK (build-tools, android.jar),
# JDK 17+, python (zip), установленную переменную ANDROID_SDK или дефолтный путь.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK:-/c/Program Files (x86)/Android/android-sdk}"
BT="$SDK/build-tools/36.0.0"
AJ="$SDK/platforms/android-35/android.jar"
WORK="$ROOT/build/apk"

cd "$ROOT"
npx vite build

rm -rf "$WORK/assets" "$WORK/classes"; mkdir -p "$WORK/assets" "$WORK/classes"
cp -r dist/* "$WORK/assets/"
cp "$AJ" "$WORK/android.jar"

javac --release 11 -cp "$WORK/android.jar" -d "$WORK/classes" android/MainActivity.java
BTWIN=$(cygpath -w "$BT")
( cd "$BTWIN" && cmd //c "d8.bat --release --lib $(cygpath -w "$WORK/android.jar") --output $(cygpath -w "$WORK") $(cygpath -w "$WORK"/classes/ru/kengaos/shell/*.class)" )

cd "$WORK"
"$BT/aapt2.exe" link -o base.apk -I android.jar --manifest "$ROOT/android/AndroidManifest.xml" -A assets --min-sdk-version 23 --target-sdk-version 35
python - <<'EOF'
import zipfile, shutil
shutil.copy("base.apk","base2.apk")
with zipfile.ZipFile("base2.apk","a",zipfile.ZIP_DEFLATED) as z:
    z.write("classes.dex","classes.dex")
EOF
"$BT/zipalign.exe" -f 4 base2.apk aligned.apk
KT="${JAVA_HOME:+$JAVA_HOME/bin/}keytool"; KT="${KT:-keytool}"
[ -f debug.keystore ] || "$KT" -genkeypair -keystore debug.keystore -storepass kengaos -keypass kengaos -alias kenga -dname "CN=KengaOS Debug,O=KengaOS,C=RU" -keyalg RSA -keysize 2048 -validity 10000 2>/dev/null
java -jar "$BT/lib/apksigner.jar" sign --ks debug.keystore --ks-pass pass:kengaos --key-pass pass:kengaos --out kengaos-mobile.apk aligned.apk
echo "OK: build/apk/kengaos-mobile.apk"
echo "Установка: adb install -r build/apk/kengaos-mobile.apk  (MIUI: включить «Установка через USB», подтверждать окно на телефоне)"
