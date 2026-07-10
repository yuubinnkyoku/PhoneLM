#!/usr/bin/env sh
set -eu

REPOSITORY="https://github.com/alibaba/MNN.git"
TAG="3.5.0"
COMMIT="c35f14f3ab5cb65094863b9a0e888370b027a670"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${1:-"$ROOT/third_party/MNN"}

if [ -d "$TARGET/.git" ]; then
    CURRENT=$(git -C "$TARGET" rev-parse HEAD 2>/dev/null || true)
    if [ "$CURRENT" = "$COMMIT" ]; then
        echo "MNN is already pinned to $COMMIT"
        exit 0
    fi
    echo "Existing checkout at '$TARGET' is not the pinned commit; move or remove it explicitly." >&2
    exit 1
fi

if [ -e "$TARGET" ] && [ -n "$(ls -A "$TARGET" 2>/dev/null)" ]; then
    echo "Target '$TARGET' exists and is not empty." >&2
    exit 1
fi

mkdir -p "$TARGET"
git init "$TARGET"
git -C "$TARGET" remote add origin "$REPOSITORY"
git -C "$TARGET" fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG"
git -C "$TARGET" checkout --detach "$COMMIT"

RESOLVED=$(git -C "$TARGET" rev-parse HEAD)
if [ "$RESOLVED" != "$COMMIT" ]; then
    echo "MNN pin verification failed: expected $COMMIT, got $RESOLVED" >&2
    exit 1
fi

echo "MNN $TAG is ready at $TARGET ($RESOLVED)"

