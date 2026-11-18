#!/bin/bash
# setup-onnxruntime.sh — stage a prebuilt ONNX Runtime for Linux/macOS.
#
# Downloads the official ONNX Runtime prebuilt for the host OS/arch and places
# headers + shared library under third_party/onnxruntime/ ready for CMake (which
# checks that dir first). Powers the CNN signal classifier AND the ASR ONNX
# features (Silero VAD + speaker labeling). Release builds run this so those
# ship enabled; pass -DREQUIRE_ASR_ONNX=ON to fail the build if it's missing.
#
# Requires: curl, tar. Usage: ./setup-onnxruntime.sh
#
# Keep ORT_VERSION in sync with scripts/setup/setup-onnxruntime.ps1 (Windows).

set -euo pipefail

# shellcheck source=scripts/setup/_verify_sha256.sh
source "$(dirname "${BASH_SOURCE[0]}")/_verify_sha256.sh"

# Pinned to the ONNX Runtime that sherpa-onnx bundles (so a build with both
# shares one runtime — no symbol-version clash). Keep in sync with
# setup-onnxruntime.ps1 and setup-sherpa-onnx.sh.
ORT_VERSION="1.27.0"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${REPO_ROOT}/third_party/onnxruntime"

if [ -f "${OUT_DIR}/include/onnxruntime_cxx_api.h" ]; then
    echo "ONNX Runtime already staged in ${OUT_DIR}"
    exit 0
fi

# Select the release asset + its pinned SHA-256 for this host.
os="$(uname -s)"
arch="$(uname -m)"
case "${os}-${arch}" in
    Linux-x86_64)        asset="linux-x64"     sha="547e40a48f1fe73e3f812d7c88a948612c23f896b91e4e2ee1e232d7b468246f" ;;
    Linux-aarch64|Linux-arm64) asset="linux-aarch64" sha="3e4d83ac06924a32a07b6d7f91ce6f852876153fc0bbdf931bf517a140bfbe48" ;;
    Darwin-arm64)        asset="osx-arm64"     sha="545e81c58152353acb0d1e8bd6ce4b62f830c0961f5b3acfedc790ffd76e477a" ;;
    Darwin-x86_64)
        echo "ERROR: ONNX Runtime ${ORT_VERSION} has no Intel-macOS prebuilt (arm64 only). Stage" >&2
        echo "sherpa-onnx (universal2, bundles the same runtime) or build ONNX Runtime from source." >&2
        exit 1 ;;
    *)
        echo "ERROR: unsupported host ${os}-${arch}. Install onnxruntime manually or extend this script." >&2
        exit 1 ;;
esac

pkg="onnxruntime-${asset}-${ORT_VERSION}"
tgz="${REPO_ROOT}/third_party/${pkg}.tgz"

# ANAN fork (18.11.2026): microsoft/onnxruntime vanished from GitHub (its
# release downloads answer 404), so the same archive is also looked for on
# mirrors, in this order. A mirror is not trusted: whatever it serves has to
# match the SHA-256 pinned above, exactly as Microsoft's own file did, or it
# is thrown away and the next source is tried.
urls=(
    "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${pkg}.tgz"
    "https://downloads.sourceforge.net/project/onnx-runtime.mirror/v${ORT_VERSION}/${pkg}.tgz"
    "https://github.com/DenisovAV/flutter_edge_ai/releases/download/mirror-onnxruntime-v${ORT_VERSION}/${pkg}.tgz"
)

mkdir -p "${REPO_ROOT}/third_party"
if [ -f "${tgz}" ] && ! verify_sha256 "${tgz}" "${sha}"; then
    rm -f "${tgz}"
fi
if [ ! -f "${tgz}" ]; then
    for url in "${urls[@]}"; do
        echo "Downloading ${pkg}.tgz from ${url} ..."
        if curl -fSL --retry 2 -o "${tgz}.part" "${url}" && verify_sha256 "${tgz}.part" "${sha}"; then
            mv "${tgz}.part" "${tgz}"
            break
        fi
        echo "  not usable from this source" >&2
        rm -f "${tgz}.part"
    done
fi
if [ ! -f "${tgz}" ]; then
    # Linux: Microsoft's own tgz is on no mirror with its pinned bytes. The
    # same runtime ships in the official NuGet package on nuget.org
    # (Microsoft.ML.OnnxRuntime, a zip): headers under build/native/include,
    # the library under runtimes/<rid>/native. Staged in the tgz's layout
    # (include/, lib/libonnxruntime.so -> .so.1 -> .so.<version>). The
    # package is pinned by its SHA-256 as well.
    case "${asset}" in
        linux-x64)     rid="linux-x64" ;;
        linux-aarch64) rid="linux-arm64" ;;
        *)             rid="" ;;
    esac
    if [ -n "${rid}" ]; then
        nupkg_sha="c725e8cefdfe4de08befb9f79b3f218c162439cdcdb97cbbb5192888d58ea669"
        nupkg="${REPO_ROOT}/third_party/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
        nurl="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
        echo "Downloading the NuGet package from ${nurl} ..."
        curl -fSL --retry 3 -o "${nupkg}" "${nurl}"
        verify_sha256 "${nupkg}" "${nupkg_sha}"
        tmp="${REPO_ROOT}/third_party/onnxruntime-tmp"
        rm -rf "${tmp}" "${OUT_DIR}"
        mkdir -p "${tmp}"
        if command -v unzip >/dev/null 2>&1; then
            unzip -q "${nupkg}" -d "${tmp}"
        else
            python3 -m zipfile -e "${nupkg}" "${tmp}"
        fi
        so="${tmp}/runtimes/${rid}/native/libonnxruntime.so"
        if [ ! -f "${tmp}/build/native/include/onnxruntime_cxx_api.h" ] || [ ! -f "${so}" ]; then
            echo "ERROR: unexpected NuGet package layout under ${tmp}" >&2
            find "${tmp}" -maxdepth 4 | head -60 >&2
            exit 1
        fi
        mkdir -p "${OUT_DIR}/lib"
        cp -a "${tmp}/build/native/include" "${OUT_DIR}/include"
        cp "${so}" "${OUT_DIR}/lib/libonnxruntime.so.${ORT_VERSION}"
        ln -s "libonnxruntime.so.${ORT_VERSION}" "${OUT_DIR}/lib/libonnxruntime.so.1"
        ln -s "libonnxruntime.so.1" "${OUT_DIR}/lib/libonnxruntime.so"
        for f in "${tmp}"/runtimes/"${rid}"/native/libonnxruntime_providers_shared.so; do
            [ -f "$f" ] && cp "$f" "${OUT_DIR}/lib/"
        done
        rm -rf "${tmp}"
        echo "ONNX Runtime ${ORT_VERSION} (${asset}, from the NuGet package) staged in ${OUT_DIR}"
        exit 0
    fi
    echo "ERROR: ${pkg}.tgz with the pinned SHA-256 was not found on any source." >&2
    exit 1
fi
verify_sha256 "${tgz}" "${sha}"

echo "Extracting ..."
tmp="${REPO_ROOT}/third_party/onnxruntime-tmp"
rm -rf "${tmp}" "${OUT_DIR}"
mkdir -p "${tmp}"
tar xzf "${tgz}" -C "${tmp}"

src="${tmp}/${pkg}"
[ -d "${src}" ] || src="$(find "${tmp}" -maxdepth 1 -type d -name 'onnxruntime-*' | head -1)"
if [ ! -f "${src}/include/onnxruntime_cxx_api.h" ]; then
    echo "ERROR: unexpected archive layout under ${tmp}" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}"
cp -a "${src}/include" "${OUT_DIR}/include"
cp -a "${src}/lib" "${OUT_DIR}/lib"
rm -rf "${tmp}"

echo "ONNX Runtime ${ORT_VERSION} (${asset}) staged in ${OUT_DIR}"
