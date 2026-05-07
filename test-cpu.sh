#!/bin/bash

set -e
QUERY="$1"
ARCH="$(uname -m)"

is_arm64() {
  [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ]
}

check_mac() {
  case "$1" in
    arm64)   is_arm64;;
    arm)     [ "$ARCH" = "armv7" ];;
    x86_64)  [ "$ARCH" = "x86_64" ];;
    msr|sse2|ssse3|sse4_1|xop|avx2|avx512f|vaes) is_arm64 && return 1; sysctl -n machdep.cpu.features | grep -i "$1" >/dev/null;;
    aes)     is_arm64 && return 0; sysctl -n machdep.cpu.features | grep -i " aes" >/dev/null;;
    *) echo "UNRECOGNISED CHECK $QUERY"; exit 1;;
  esac
}

check_linux() {
  which cpuinfo >/dev/null && CPUINFO=$(cpuinfo) || CPUINFO=$(cat /proc/cpuinfo)
  case "$1" in
    arm64)   uname -m | grep -E "^(aarch64|arm64)$" >/dev/null;;
    arm)     uname -a | grep "armv7"   >/dev/null;;
    x86_64)  uname -a | grep "x86_64"  >/dev/null;;
    msr)     grep msr      <<<$CPUINFO >/dev/null;;
    sse2)    grep sse2     <<<$CPUINFO >/dev/null;;
    ssse3)   grep ssse3    <<<$CPUINFO >/dev/null;;
    sse4_1)  grep sse4_1   <<<$CPUINFO >/dev/null;;
    xop)     grep xop      <<<$CPUINFO >/dev/null;;
    avx2)    grep avx2     <<<$CPUINFO >/dev/null;;
    avx512f) grep avx512f  <<<$CPUINFO >/dev/null;;
    vaes)    grep vaes     <<<$CPUINFO >/dev/null;;
    aes)     grep " aes"   <<<$CPUINFO >/dev/null;;
    *) echo "UNRECOGNISED CHECK $QUERY"; exit 1;;
  esac
}

case "$(uname -a)" in
  Darwin*) check_mac   "$QUERY";;
  *)       check_linux "$QUERY";;
esac
