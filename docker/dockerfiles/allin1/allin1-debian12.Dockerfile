# This docker file builds the StarRocks allin1 image on secure Debian 12 base
# Uses multi-stage build with package-builder pattern for secure base compatibility
#
# Please run this command from the git repo root directory to build:
#
#   - Use artifact image to package runtime container:
#     > DOCKER_BUILDKIT=1 docker build --build-arg ARTIFACT_SOURCE=image --build-arg ARTIFACTIMAGE=starrocks/artifacts-ubuntu:latest -f docker/dockerfiles/allin1/allin1-debian12.Dockerfile -t allin1-debian12:latest .
#   - Use locally build artifacts to package runtime container:
#     > DOCKER_BUILDKIT=1 docker build --build-arg ARTIFACT_SOURCE=local --build-arg LOCAL_REPO_PATH=. -f docker/dockerfiles/allin1/allin1-debian12.Dockerfile -t allin1-debian12:latest .

# Global ARGs - must be declared before any FROM to be used in FROM instructions
ARG ARTIFACT_SOURCE=image
ARG WITH_DEBUG_INFO=false
ARG ARTIFACTIMAGE=starrocks/artifacts-ubuntu:latest

# Stage 1: Package builder - download packages for secure base (which lacks apt-get)
FROM debian:12-slim AS package-builder
WORKDIR /packages
RUN apt-get update && apt-get download \
    supervisor lsb-base \
    nginx mariadb-client netcat-openbsd \
    binutils openjdk-17-jdk \
    python3 python3-minimal python3.11 python3.11-minimal \
    libpython3-stdlib libpython3.11-stdlib libpython3.11-minimal \
    python3-pkg-resources \
    curl vim tree net-tools \
    less tzdata locales && \
    rm -f *systemd* *dbus*

# Stage 2: Artifact sources
FROM ${ARTIFACTIMAGE} AS artifacts-from-image

FROM busybox:latest AS artifacts-from-local
ARG LOCAL_REPO_PATH
COPY ${LOCAL_REPO_PATH}/output/fe /release/fe_artifacts/fe
COPY ${LOCAL_REPO_PATH}/output/be /release/be_artifacts/be

FROM artifacts-from-${ARTIFACT_SOURCE} AS artifacts
ARG WITH_DEBUG_INFO
RUN if [ "$WITH_DEBUG_INFO" = "false" ]; then rm -f /release/be_artifacts/be/lib/starrocks_be.debuginfo; fi

# Stage 3: Final runtime image using secure Debian 12 base
FROM ghcr.io/celonis/cloud-secure-base-images/celodeb-starrocks-21:21-debian12-wolfssl5.7.6

ARG DEPLOYDIR=/data/deploy
ENV SR_HOME=${DEPLOYDIR}/starrocks

# Copy packages from builder stage
COPY --from=package-builder /packages/*.deb /tmp/packages/

# Install packages using dpkg (secure base lacks apt-get)
# Keep broad install tolerant for secure-base package conflicts, then enforce python/supervisor bootstrap.
RUN dpkg -i /tmp/packages/*.deb || true && \
    dpkg --configure -a || true && \
    dpkg -i \
    /tmp/packages/libpython3.11-minimal_*.deb \
    /tmp/packages/python3.11-minimal_*.deb \
    /tmp/packages/python3-minimal_*.deb \
    /tmp/packages/libpython3.11-stdlib_*.deb \
    /tmp/packages/libpython3-stdlib_*.deb \
    /tmp/packages/python3.11_*.deb \
    /tmp/packages/python3_*.deb \
    /tmp/packages/python3-pkg-resources_*.deb \
    /tmp/packages/lsb-base_*.deb \
    /tmp/packages/supervisor_*.deb || true && \
    dpkg --configure -a || true && \
    python3 --version && \
    supervisord --version && \
    rm -rf /tmp/packages

# Configure timezone and locale
RUN ln -fs /usr/share/zoneinfo/UTC /etc/localtime && \
    dpkg-reconfigure -f noninteractive tzdata && \
    echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && locale-gen en_US.UTF-8

# Set up Java environment - secure base has Java 21, create compatibility paths
RUN ARCH=$(uname -m) && \
    mkdir -p /lib/jvm && \
    if [ -d "/usr/lib/jvm/default" ]; then \
        ln -sf /usr/lib/jvm/default /lib/jvm/java-17-openjdk; \
    elif [ "$ARCH" = "aarch64" ] && [ -d "/usr/lib/jvm/java-17-openjdk-arm64" ]; then \
        ln -sf /usr/lib/jvm/java-17-openjdk-arm64 /lib/jvm/java-17-openjdk; \
    elif [ "$ARCH" = "x86_64" ] && [ -d "/usr/lib/jvm/java-17-openjdk-amd64" ]; then \
        ln -sf /usr/lib/jvm/java-17-openjdk-amd64 /lib/jvm/java-17-openjdk; \
    else \
        ln -sf /usr/lib/jvm/default /lib/jvm/java-17-openjdk || true; \
    fi
ENV JAVA_HOME=/lib/jvm/java-17-openjdk

WORKDIR $DEPLOYDIR

# Copy all artifacts to the runtime container image
COPY --from=artifacts /release/be_artifacts/ $DEPLOYDIR/starrocks
COPY --from=artifacts /release/fe_artifacts/ $DEPLOYDIR/starrocks

# Copy setup script and config files
COPY docker/dockerfiles/allin1/*.sh docker/dockerfiles/allin1/*.conf docker/dockerfiles/allin1/*.txt $DEPLOYDIR
COPY docker/dockerfiles/allin1/services/ $SR_HOME

RUN cat be.conf >> $DEPLOYDIR/starrocks/be/conf/be.conf && \
    cat fe.conf >> $DEPLOYDIR/starrocks/fe/conf/fe.conf && \
    rm -f be.conf fe.conf && \
    mkdir -p $DEPLOYDIR/starrocks/fe/meta $DEPLOYDIR/starrocks/be/storage && touch /.dockerenv

CMD ./entrypoint.sh
