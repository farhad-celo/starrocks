FROM starrocks/dev-env-ubuntu:latest

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    gcc-12-aarch64-linux-gnu \
    g++-12-aarch64-linux-gnu \
    gcc-12-multilib \
    g++-12-multilib

RUN ln -s /usr/bin/aarch64-linux-gnu-g++-12 /usr/bin/aarch64-linux-gnu-g++ && \
    ln -s /usr/bin/aarch64-linux-gnu-gcc-12 /usr/bin/aarch64-linux-gnu-gcc