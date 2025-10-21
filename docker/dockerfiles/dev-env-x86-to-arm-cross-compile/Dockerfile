FROM ghcr.io/celonis/celostar/starrocks-dev-env:pr1868-3.3-ee1f4e26-celo-amd

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    gcc-11-aarch64-linux-gnu \
    g++-11-aarch64-linux-gnu \
    gcc-11-multilib \
    g++-11-multilib

RUN ln -s /usr/bin/aarch64-linux-gnu-g++-11 /usr/bin/aarch64-linux-gnu-g++ && \
    ln -s /usr/bin/aarch64-linux-gnu-gcc-11 /usr/bin/aarch64-linux-gnu-gcc