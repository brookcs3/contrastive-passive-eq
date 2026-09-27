#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
#
# Run scripts/build.sh inside a throwaway Debian 12 container, once per architecture. The release binaries are built this way.
# usage: scripts/docker-build.sh [aarch64|x86_64 ...]   (default: this machine's architecture; another one runs under emulation)
# The container mounts this repository at /src, so the outputs land in build/ exactly as with scripts/build.sh. IMAGE=<image> picks
# another base image (default debian:12).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-debian:12}"
[ $# -gt 0 ] || set -- "$(uname -m)"
for arch in "$@"; do
  case "$arch" in
    aarch64|arm64) platform=linux/arm64 ;;
    x86_64|amd64)  platform=linux/amd64 ;;
    *) echo "unknown architecture: $arch" >&2; exit 2 ;;
  esac
  docker run --rm --platform "$platform" -v "$ROOT":/src -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" "$IMAGE" bash -c '
    set -euo pipefail
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends build-essential pkg-config git ca-certificates >/dev/null
    rc=0; bash /src/scripts/build.sh || rc=$?
    chown -R "$HOST_UID:$HOST_GID" /src/build /src/third_party 2>/dev/null || true
    exit $rc'
done
