FROM ghcr.io/celonis/celostar/starrocks-dev-env:pr2035c-3.3-4f90ca75-celo

# Setup homcc
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.2/homcc.deb
RUN apt update && apt install -y ./homcc.deb
COPY hosts /etc/homcc/hosts
ENV HOMCC_DOCKER_CONTAINER=sr-dev

# Install OpenJDK
RUN apt install -y openjdk-17-jdk

# Some more convenient dev tooling
RUN apt install -y lldb ninja-build clang-format htop

# Set up ccache
ENV CCACHE_PREFIX=homcc
