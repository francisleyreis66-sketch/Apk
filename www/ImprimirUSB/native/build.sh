#!/usr/bin/env bash
# Compila o driver livre foo2xqx (projeto foo2zjs, licença GPL) para Android.
# O resultado é um executável com nome libfoo2xqx.so, colocado em jniLibs
# para o Android extrair e o app poder executá-lo.
set -euo pipefail
cd "$(dirname "$0")/.."

SRC=$(find ext -maxdepth 2 -name foo2xqx.c -printf '%h\n' | head -1)
[ -n "$SRC" ] || { echo "foo2xqx.c não encontrado em ext/"; exit 1; }
echo "Código do driver: $SRC"

# biblioteca JBIG: a que vem junto do foo2zjs ou a do jbigkit
if [ -f "$SRC/jbig.c" ]; then JB="$SRC"; else
  JB=$(find ext -path '*libjbig*' -name jbig.c -printf '%h\n' | head -1)
fi
[ -n "$JB" ] || { echo "jbig.c não encontrado"; exit 1; }
echo "JBIG: $JB"
FILES="$SRC/foo2xqx.c $JB/jbig.c $JB/jbig_ar.c"

NDK="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}"
if [ ! -d "$NDK" ]; then NDK=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1); fi
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
echo "NDK: $NDK"

build() {
  local abi=$1 cc=$2
  mkdir -p "app/src/main/jniLibs/$abi"
  "$TC/$cc" -O2 -fPIE -pie -w -I"$JB" -I"$SRC" -o "app/src/main/jniLibs/$abi/libfoo2xqx.so" $FILES
  echo "ok: $abi"
}
build arm64-v8a  aarch64-linux-android26-clang
build armeabi-v7a armv7a-linux-androideabi26-clang

# Versão para o computador, só para registrar as opções do driver (vai no artefato)
gcc -O2 -w -I"$JB" -I"$SRC" -o foo2xqx-host $FILES
{
  echo "== foo2xqx -h =="; ./foo2xqx-host -h 2>&1 || true
  echo; echo "== PPD / wrapper com P1005 =="
  grep -rn "P1005" "$SRC" --include='*.ppd*' --include='*wrapper*' --include='*.in' 2>/dev/null | head -60 || true
} > foo2xqx-ajuda.txt
cat foo2xqx-ajuda.txt | head -80
