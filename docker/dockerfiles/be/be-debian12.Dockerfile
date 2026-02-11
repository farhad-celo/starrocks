# Multi-stage Dockerfile for StarRocks BE on secure Debian 12 base image
# Uses Ubuntu artifacts with Debian 12 runtime for binary compatibility

# Global ARGs
ARG ARTIFACT_SOURCE=image
ARG ARTIFACTIMAGE=ghcr.io/celonis/celostar/starrocks-artifacts:latest

# Stage 1: Artifact source selection
FROM ${ARTIFACTIMAGE} AS artifacts-from-image

FROM busybox:latest AS artifacts-from-local
ARG LOCAL_REPO_PATH
COPY ${LOCAL_REPO_PATH}/output/be /release/be_artifacts/be

FROM artifacts-from-${ARTIFACT_SOURCE} AS artifacts
RUN rm -f /release/be_artifacts/be/lib/starrocks_be.debuginfo

# Stage 2: Final runtime image using secure Debian 12 base
FROM ghcr.io/celonis/cloud-secure-base-images/celodeb-starrocks-21:21-debian12-wolfssl5.7.6

# Configure timezone and locale
RUN ln -fs /usr/share/zoneinfo/UTC /etc/localtime && \
    dpkg-reconfigure -f noninteractive tzdata && \
    echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && locale-gen en_US.UTF-8

# Set up Java environment (secure base has Java 21, create compatibility symlink)
RUN mkdir -p /lib/jvm && \
    ln -sf /usr/lib/jvm/default /lib/jvm/default-java

# Create .dockerenv for container detection
RUN touch /.dockerenv

WORKDIR /opt/starrocks

# User management - use existing celonis-user (UID 1000) from secure base
RUN id 1000 || echo "UID 1000 not found" && \
    getent group 1000 || echo "GID 1000 not found" && \
    if ! id starrocks 2>/dev/null; then \
        if ! getent group starrocks 2>/dev/null; then \
            groupadd --gid 1000 starrocks 2>/dev/null || groupadd starrocks; \
        fi && \
        useradd --no-create-home --uid 1000 --gid starrocks --shell /bin/false starrocks 2>/dev/null || \
        useradd --no-create-home --gid starrocks --shell /bin/false starrocks; \
    fi && \
    chown -R 1000:1000 /opt/starrocks || chown -R starrocks:starrocks /opt/starrocks

# Copy StarRocks artifacts
COPY --from=artifacts --chown=1000:1000 /release/be_artifacts/ /opt/starrocks/

# Copy BE scripts
COPY --chown=1000:1000 docker/dockerfiles/be/*.sh /opt/starrocks/

# Create storage directory and CN symlink
RUN mkdir -p /opt/starrocks/be/storage && \
    ln -sfT be /opt/starrocks/cn && \
    chown -R 1000:1000 /opt/starrocks

# Environment variables
ENV JAVA_HOME=/lib/jvm/default-java
ENV LD_LIBRARY_PATH=$JAVA_HOME/lib/server:$JAVA_HOME/lib:/opt/starrocks/lib:/opt/starrocks/be/lib/onetbb
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

USER 1000

WORKDIR /opt/starrocks
