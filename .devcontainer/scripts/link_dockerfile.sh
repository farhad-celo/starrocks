#!/usr/bin/env sh
set -eu

cd "$(dirname "$0")"
arch="$(uname -m)"

# we are adding a relative link to ../dev-env-homcc-base
# therefore the link target has to be read as ../../docker/<...> relative to this file.
case "$arch" in
  arm64|aarch64)
    ln -sfF  ../docker/dockerfiles/dev-env-homcc-base/arm/ ../dev-env-homcc-base
    ;;
  x86_64|amd64)
    ln -sfF  ../docker/dockerfiles/dev-env-homcc-base/x86/ ../dev-env-homcc-base
    ;;
  *)
    echo "Unsupported architecture: $arch" >&2
    exit 1
    ;;
esac
