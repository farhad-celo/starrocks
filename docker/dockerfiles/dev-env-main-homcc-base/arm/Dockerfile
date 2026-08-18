FROM starrocks/dev-env-ubuntu:latest

# Setup homcc
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.4/python3-homcc-common_1.0.4_all.deb
RUN wget https://github.com/celonis/homcc/releases/download/v1.0.4/homcc_1.0.4_all.deb
RUN apt update && apt install -y ./python3-homcc-common_1.0.4_all.deb ./homcc_1.0.4_all.deb
COPY hosts /etc/homcc/hosts
ENV HOMCC_DOCKER_CONTAINER=sr-dev-main

# Install dependencies for StarRocks E2E tests
RUN apt-get update && apt-get install -y pip mysql-client

# Some more convenient dev tooling
RUN apt install -y lldb ninja-build clang-format htop distcc

# Set up ccache
ENV CCACHE_PREFIX=homcc
