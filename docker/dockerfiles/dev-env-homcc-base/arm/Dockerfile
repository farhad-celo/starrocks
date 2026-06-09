FROM ghcr.io/celonis/celostar/starrocks-dev-env:3.5-cc-86b916f9-arm

# Setup homcc
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.2/homcc_arm.deb
RUN apt update && apt install -y ./homcc_arm.deb
COPY hosts /etc/homcc/hosts
ENV HOMCC_DOCKER_CONTAINER=sr-dev

# Install dependencies for StarRocks E2E tests
RUN apt-get update && apt-get install -y pip mysql-client

# Some more convenient dev tooling
RUN apt install -y lldb ninja-build clang-format htop

# Set up ccache
ENV CCACHE_PREFIX=homcc
