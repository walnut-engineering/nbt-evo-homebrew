#!/bin/sh
# fetch_firmware.sh - get the Xbox One Wireless Adapter's firmware from Microsoft.
#
# The adapter is a bare radio: it does nothing until the host uploads this firmware.
# The file is Microsoft's and is not part of this repository or its releases; this
# script downloads the Windows driver package from Windows Update (the same file the
# Linux drivers xow and xone use), extracts the firmware and checks its hash.
#
#   ./fetch_firmware.sh [output-dir]      (run on a PC; needs curl, cabextract, sha256sum)
#
# Then put FW_ACC_00U.bin on the head unit as /fs/sda1/evo/xbox/FW_ACC_00U.bin.
set -e
URL="http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab"
SHA="48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66"
OUT="${1:-.}"

for tool in curl cabextract sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || { echo "fetch_firmware: '$tool' is required" >&2; exit 1; }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "downloading the driver package from Windows Update..."
curl -fsSL -o "$TMP/driver.cab" "$URL"
cabextract -q -F FW_ACC_00U.bin -d "$TMP" "$TMP/driver.cab" 2>/dev/null
echo "$SHA  $TMP/FW_ACC_00U.bin" | sha256sum -c - >/dev/null || { echo "fetch_firmware: hash mismatch - not using this file" >&2; exit 1; }
mkdir -p "$OUT"
cp "$TMP/FW_ACC_00U.bin" "$OUT/FW_ACC_00U.bin"
echo "ok: $OUT/FW_ACC_00U.bin (70620 bytes, sha256 verified)"
echo "copy it to the head unit as /fs/sda1/evo/xbox/FW_ACC_00U.bin"
