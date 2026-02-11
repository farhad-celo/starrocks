# Multi-stage Dockerfile for StarRocks FE on secure Debian 12 base image
# Uses Ubuntu artifacts with Debian 12 runtime for binary compatibility

# Global ARGs
ARG ARTIFACT_SOURCE=image
ARG ARTIFACTIMAGE=ghcr.io/celonis/celostar/starrocks-artifacts:latest
ARG RUN_AS_USER=root
ARG USER=starrocks

# Stage 1: Artifact source selection
FROM ${ARTIFACTIMAGE} AS artifacts-from-image

FROM busybox:latest AS artifacts-from-local
ARG LOCAL_REPO_PATH
COPY ${LOCAL_REPO_PATH}/output/fe /release/fe_artifacts/fe

FROM artifacts-from-${ARTIFACT_SOURCE} AS artifacts

# Stage 2: Final runtime image using secure Debian 12 base
FROM ghcr.io/celonis/cloud-secure-base-images/celodeb-starrocks-21:21-debian12-wolfssl5.7.6

ARG STARROCKS_ROOT=/opt/starrocks
ARG USER
ARG RUN_AS_USER
ARG GROUP=starrocks

# Configure timezone and locale
RUN ln -fs /usr/share/zoneinfo/UTC /etc/localtime && \
    dpkg-reconfigure -f noninteractive tzdata && \
    echo "en_US.UTF-8 UTF-8" > /etc/locale.gen && locale-gen en_US.UTF-8

# Set up Java environment (secure base has Java 21, create compatibility symlink)
RUN mkdir -p /lib/jvm && \
    ln -sf /usr/lib/jvm/default /lib/jvm/default-java

# Create .dockerenv for container detection
RUN touch /.dockerenv

WORKDIR $STARROCKS_ROOT

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
    chown -R 1000:1000 $STARROCKS_ROOT || chown -R starrocks:starrocks $STARROCKS_ROOT

# Copy StarRocks FE artifacts
COPY --from=artifacts --chown=1000:1000 /release/fe_artifacts/ $STARROCKS_ROOT/

# Copy FE scripts
COPY --chown=1000:1000 docker/dockerfiles/fe/*.sh $STARROCKS_ROOT/

# Create directory for FE metadata (as root before user switch)
RUN mkdir -p $STARROCKS_ROOT/fe/meta && \
    chown -R 1000:1000 $STARROCKS_ROOT

USER $USER

# Environment variables
ENV JAVA_HOME=/lib/jvm/default-java
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

USER $RUN_AS_USER
