#!/usr/bin/env sh

# Inventory a locally installed QAIRT SDK without assuming QNN library names.
# Candidate roles are printed with their real installed paths and still require
# confirmation against the matching SDK's official sample build files.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SDK_ROOT=${QAIRT_SDK_ROOT:-}
SDK_ROOT_SOURCE=QAIRT_SDK_ROOT

if [ "${1:-}" = "--sdk-root" ]; then
    if [ "$#" -lt 2 ]; then
        echo "error=--sdk-root requires a path"
        exit 64
    fi
    SDK_ROOT=$2
    SDK_ROOT_SOURCE=argument
elif [ "$#" -gt 0 ]; then
    SDK_ROOT=$1
    SDK_ROOT_SOURCE=argument
fi

read_gradle_property() {
    for property_file in \
        "$REPOSITORY_ROOT/local.properties" \
        "$REPOSITORY_ROOT/gradle.properties" \
        "${HOME:-}/.gradle/gradle.properties"
    do
        [ -f "$property_file" ] || continue
        value=$(sed -n 's/^[[:space:]]*qairt\.sdkRoot[[:space:]]*=[[:space:]]*//p' \
            "$property_file" | tail -n 1)
        if [ -n "$value" ]; then
            printf '%s\n' "$value" | sed 's/\\:/:/g; s/\\\\/\\/g'
            return 0
        fi
    done
    return 1
}

if [ -z "$SDK_ROOT" ]; then
    property_root=$(read_gradle_property 2>/dev/null || true)
    if [ -n "$property_root" ]; then
        SDK_ROOT=$property_root
        SDK_ROOT_SOURCE=gradle_property
    fi
fi

if [ -z "$SDK_ROOT" ]; then
    for common_root in \
        /opt/qualcomm \
        /opt/qcom \
        "${HOME:-}/Qualcomm" \
        "${HOME:-}/qairt" \
        "${HOME:-}/.qairt"
    do
        [ -d "$common_root" ] || continue
        header=$(find "$common_root" -type f -name QnnInterface.h -print 2>/dev/null | head -n 1)
        if [ -n "$header" ]; then
            SDK_ROOT=$common_root
            SDK_ROOT_SOURCE=common_path_scan
            break
        fi
    done
fi

echo "check=QAIRT_SDK_INVENTORY"
echo "requested_sdk_root=${SDK_ROOT:-NONE}"

if [ -z "$SDK_ROOT" ] || [ ! -d "$SDK_ROOT" ]; then
    echo "sdk_root_exists=false"
    echo "qnn_interface_header_exists=false"
    echo "qnn_types_header_exists=false"
    echo "qnn_implementation_ready=false"
    echo "status=BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED"
    exit 2
fi

SDK_ROOT=$(CDPATH= cd -- "$SDK_ROOT" && pwd)
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/phonelm-qairt.XXXXXX") || exit 1
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

find "$SDK_ROOT" -type f -print 2>/dev/null > "$TMP_DIR/all-files"
grep '/QnnInterface\.h$' "$TMP_DIR/all-files" > "$TMP_DIR/interface" || true
grep '/QnnTypes\.h$' "$TMP_DIR/all-files" > "$TMP_DIR/types" || true
grep '\.so$' "$TMP_DIR/all-files" > "$TMP_DIR/shared-objects" || true

: > "$TMP_DIR/aarch64"
while IFS= read -r library; do
    [ -n "$library" ] || continue
    description=$(file -b "$library" 2>/dev/null || true)
    case "$description" in
        *aarch64*|*AArch64*) printf '%s\n' "$library" >> "$TMP_DIR/aarch64" ;;
    esac
done < "$TMP_DIR/shared-objects"
grep -Ei '(android.*(aarch64|arm64)|(aarch64|arm64).*android)' \
    "$TMP_DIR/aarch64" > "$TMP_DIR/android-arm64" || true
grep -Ei '((qnn|qairt).*cpu|cpu.*(qnn|qairt))' \
    "$TMP_DIR/android-arm64" > "$TMP_DIR/cpu-backend" || true
grep -Ei '((qnn|qairt).*htp|htp.*(qnn|qairt))' \
    "$TMP_DIR/android-arm64" > "$TMP_DIR/htp-backend" || true
grep -Ei '(htp.*(prepare|prep)|(prepare|prep).*htp)' \
    "$TMP_DIR/shared-objects" > "$TMP_DIR/htp-prepare" || true
grep -Ei '(dsp|hexagon|htp).*(skel)|skel.*(dsp|hexagon|htp)' \
    "$TMP_DIR/shared-objects" > "$TMP_DIR/dsp-skel" || true
grep -Ei '(dsp|hexagon|htp).*(stub)|stub.*(dsp|hexagon|htp)' \
    "$TMP_DIR/shared-objects" > "$TMP_DIR/dsp-stub" || true
grep -E '/qnn-net-run([^/]*)?$' "$TMP_DIR/all-files" > "$TMP_DIR/net-run" || true
grep -E '/qnn-platform-validator([^/]*)?$' "$TMP_DIR/all-files" > "$TMP_DIR/validator" || true
grep -Ei '/(sample|samples|example|examples)/' "$TMP_DIR/all-files" > "$TMP_DIR/sample-files" || true

: > "$TMP_DIR/sample-evidence"
: > "$TMP_DIR/cpu-sample"
: > "$TMP_DIR/htp-sample"
while IFS= read -r sample_file; do
    [ -f "$sample_file" ] || continue
    if grep -Iq 'QnnInterface\.h' "$sample_file" 2>/dev/null; then
        printf '%s\n' "$sample_file" >> "$TMP_DIR/sample-evidence"
        if printf '%s\n' "$sample_file" | grep -Eiq 'cpu' ||
           grep -Eiq '(^|[^A-Za-z])CPU([^A-Za-z]|$)|QnnCpu' "$sample_file" 2>/dev/null; then
            printf '%s\n' "$sample_file" >> "$TMP_DIR/cpu-sample"
        fi
        if printf '%s\n' "$sample_file" | grep -Eiq 'htp' ||
           grep -Eiq '(^|[^A-Za-z])HTP([^A-Za-z]|$)|QnnHtp' "$sample_file" 2>/dev/null; then
            printf '%s\n' "$sample_file" >> "$TMP_DIR/htp-sample"
        fi
    fi
done < "$TMP_DIR/sample-files"

join_file() {
    if [ -s "$1" ]; then
        sort -u "$1" | paste -sd ';' -
    else
        printf 'NONE'
    fi
}

macro_value() {
    suffix=$1
    grep -RhE --include='Qnn*.h' --include='Qairt*.h' \
        "^[[:space:]]*#[[:space:]]*define[[:space:]]+[A-Za-z0-9_]*(QNN|QAIRT)[A-Za-z0-9_]*VERSION_${suffix}[[:space:]]+\\(?[0-9]+" \
        "$SDK_ROOT" 2>/dev/null |
        sed -E 's/.*[[:space:]]+\(?([0-9]+).*/\1/' | head -n 1
}

api_major=$(macro_value MAJOR || true)
api_minor=$(macro_value MINOR || true)
api_patch=$(macro_value PATCH || true)
if [ -n "$api_major" ] && [ -n "$api_minor" ]; then
    QNN_API_VERSION="$api_major.$api_minor"
    [ -n "$api_patch" ] && QNN_API_VERSION="$QNN_API_VERSION.$api_patch"
else
    QNN_API_VERSION=UNDETERMINED
fi

root_version=$(basename "$SDK_ROOT" | sed -nE 's/.*([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p')
QAIRT_SDK_VERSION=${root_version:-UNDETERMINED}

echo "sdk_root_exists=true"
echo "sdk_root=$SDK_ROOT"
echo "sdk_root_source=$SDK_ROOT_SOURCE"
if [ -s "$TMP_DIR/interface" ]; then echo "qnn_interface_header_exists=true"; else echo "qnn_interface_header_exists=false"; fi
echo "qnn_interface_headers=$(join_file "$TMP_DIR/interface")"
if [ -s "$TMP_DIR/interface" ]; then
    sed 's#/[^/]*$##' "$TMP_DIR/interface" | sort -u > "$TMP_DIR/include-dirs"
else
    : > "$TMP_DIR/include-dirs"
fi
if [ -s "$TMP_DIR/types" ]; then echo "qnn_types_header_exists=true"; else echo "qnn_types_header_exists=false"; fi
echo "qnn_types_headers=$(join_file "$TMP_DIR/types")"
echo "include_directories=$(join_file "$TMP_DIR/include-dirs")"
echo "qairt_sdk_version=$QAIRT_SDK_VERSION"
echo "qnn_api_version=$QNN_API_VERSION"
echo "android_arm64_libraries=$(join_file "$TMP_DIR/android-arm64")"
if [ -s "$TMP_DIR/android-arm64" ]; then
    sed 's#/[^/]*$##' "$TMP_DIR/android-arm64" | sort -u > "$TMP_DIR/android-arm64-dirs"
else
    : > "$TMP_DIR/android-arm64-dirs"
fi
echo "android_arm64_library_directories=$(join_file "$TMP_DIR/android-arm64-dirs")"
echo "cpu_backend_library_candidates=$(join_file "$TMP_DIR/cpu-backend")"
echo "htp_backend_library_candidates=$(join_file "$TMP_DIR/htp-backend")"
if [ -s "$TMP_DIR/htp-backend" ]; then
    sed 's#/[^/]*$##' "$TMP_DIR/htp-backend" | sort -u > "$TMP_DIR/htp-runtime-dirs"
else
    : > "$TMP_DIR/htp-runtime-dirs"
fi
echo "htp_runtime_library_directories=$(join_file "$TMP_DIR/htp-runtime-dirs")"
echo "htp_prepare_or_equivalent_candidates=$(join_file "$TMP_DIR/htp-prepare")"
echo "dsp_skel_candidates=$(join_file "$TMP_DIR/dsp-skel")"
echo "dsp_stub_candidates=$(join_file "$TMP_DIR/dsp-stub")"
cat "$TMP_DIR/dsp-skel" "$TMP_DIR/dsp-stub" | sed 's#/[^/]*$##' | sort -u > "$TMP_DIR/dsp-dirs"
echo "dsp_library_directories=$(join_file "$TMP_DIR/dsp-dirs")"
echo "qnn_net_run=$(join_file "$TMP_DIR/net-run")"
echo "qnn_platform_validator=$(join_file "$TMP_DIR/validator")"
echo "official_sample_candidates=$(join_file "$TMP_DIR/sample-evidence")"
if [ -s "$TMP_DIR/sample-evidence" ]; then
    sed 's#/[^/]*$##' "$TMP_DIR/sample-evidence" | sort -u > "$TMP_DIR/sample-dirs"
else
    : > "$TMP_DIR/sample-dirs"
fi
echo "official_sample_directories=$(join_file "$TMP_DIR/sample-dirs")"
echo "cpu_sample_candidates=$(join_file "$TMP_DIR/cpu-sample")"
echo "htp_sample_candidates=$(join_file "$TMP_DIR/htp-sample")"
echo "classification_note=Candidate roles are inferred from installed paths and names and must be confirmed against that SDK's official build files; no library basename is hard-coded."
echo "qnn_implementation_ready=false"

if [ -s "$TMP_DIR/interface" ] && [ -s "$TMP_DIR/types" ] && \
   [ "$QAIRT_SDK_VERSION" != UNDETERMINED ] && \
   [ "$QNN_API_VERSION" != UNDETERMINED ] && [ -s "$TMP_DIR/android-arm64" ] && \
   [ -s "$TMP_DIR/cpu-backend" ] && [ -s "$TMP_DIR/htp-backend" ] && \
   [ -s "$TMP_DIR/htp-prepare" ] && \
   [ -s "$TMP_DIR/dsp-skel" ] && [ -s "$TMP_DIR/dsp-stub" ] && \
   [ -s "$TMP_DIR/net-run" ] && [ -s "$TMP_DIR/validator" ] && \
   [ -s "$TMP_DIR/sample-evidence" ] && [ -s "$TMP_DIR/cpu-sample" ] && \
   [ -s "$TMP_DIR/htp-sample" ]; then
    echo "status=QAIRT_SDK_INVENTORY_COMPLETE_ADAPTER_NOT_IMPLEMENTED"
    exit 0
fi

echo "status=QAIRT_SDK_FOUND_INVENTORY_INCOMPLETE"
exit 3
