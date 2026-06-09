FROM ghcr.io/celonis/celostar/starrocks-dev-env:3.5-cc-86b916f9-amd

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    gcc-11-aarch64-linux-gnu \
    g++-11-aarch64-linux-gnu \
    gcc-11-multilib \
    g++-11-multilib

RUN ln -s /usr/bin/aarch64-linux-gnu-g++-11 /usr/bin/aarch64-linux-gnu-g++ && \
    ln -s /usr/bin/aarch64-linux-gnu-gcc-11 /usr/bin/aarch64-linux-gnu-gcc
