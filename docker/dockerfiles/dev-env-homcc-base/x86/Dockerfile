FROM ghcr.io/celonis/celostar/starrocks-dev-env:3.5-94085107

# Setup homcc
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.2/homcc.deb
RUN apt-get update && apt-get install -y ./homcc.deb
COPY hosts /etc/homcc/hosts
ENV HOMCC_DOCKER_CONTAINER=sr-dev

# Create .gdbinit to make breakpoints work
COPY .gdbinit /root/.gdbinit

# Install OpenJDK
RUN apt-get install -y --no-install-recommends openjdk-17-jre-headless

# Some more convenient dev tooling
RUN apt-get install -y lldb ninja-build clang-format htop

# Set up ccache
ENV CCACHE_PREFIX=homcc
