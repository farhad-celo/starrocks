FROM ghcr.io/celonis/celostar/starrocks-dev-env:3.5-cc-86b916f9

# Setup homcc
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.4/python3-homcc-common_1.0.4_all.deb
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.4/homcc_1.0.4_all.deb
RUN apt update && apt install -y ./python3-homcc-common_1.0.4_all.deb ./homcc_1.0.4_all.deb
COPY hosts /etc/homcc/hosts
ENV HOMCC_DOCKER_CONTAINER=sr-dev

# Create .gdbinit to make breakpoints work
COPY .gdbinit /root/.gdbinit

# Install OpenJDK, pip, and some convenient dev tooling
RUN apt-get update && apt-get install -y --no-install-recommends \
    openjdk-17-jre-headless \
    pip \
    mysql-client \
    lldb \
    ninja-build \
    clang-format \
    htop

# Set up ccache
ENV CCACHE_PREFIX=homcc
