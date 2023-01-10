ARG BRANCH=branch-2.5

FROM starrocks/dev-env:${BRANCH}  as dev-env-with-maven-repo

# Set the artifact threads to 128 to maximize the download speed
ENV MAVEN_OPTS='-Dmaven.artifact.threads=128'
ARG STARROCKS_VERSION=2.5.0-rc02
WORKDIR /root
RUN wget https://github.com/StarRocks/starrocks/archive/refs/tags/${STARROCKS_VERSION}.tar.gz && \
        tar zxf ${STARROCKS_VERSION}.tar.gz && rm ${STARROCKS_VERSION}.tar.gz && \
        mv starrocks-${STARROCKS_VERSION} starrocks
# Copy a customized pom.xml that put kunpeng as the last option for downloading repos
COPY pom.xml starrocks/fe/pom.xml

# Build to get all dependence
RUN cd starrocks && ./build.sh --fe
RUN cd starrocks/fs_brokers/apache_hdfs_broker && ./build.sh
RUN cd starrocks/java-extensions && mvn package -DskipTests

# Remove the entire directory to make the image layer smaller
RUN rm -rf starrocks


FROM starrocks/dev-env:${BRANCH}  as celostar-dev-env

# upgrade git to latest version
RUN \
  yum -y remove git && \
  yum -y remove git-* && \
  yum -y install https://packages.endpointdev.com/rhel/7/os/x86_64/endpoint-repo.x86_64.rpm && \
  yum -y install git

# install other tools
RUN yum -y install emacs && \
    yum -y install tree && \
    yum -y install mysql


RUN \
  yum -y install openssh-clients openssh-server && \
  yum -y clean all && \
  touch /run/utmp && \
  chmod u+s /usr/bin/ping && \
  echo "root:root" | chpasswd

# retain environment variable in SSH session
RUN env | grep _ >> /etc/environment

# generate host keys if not present
RUN ssh-keygen -A

EXPOSE 22

# do not detach (-D), log to stderr (-e), passthrough other arguments
CMD exec /usr/sbin/sshd -D -e "$@"

WORKDIR /root
# copy mvn dependencies from dev-env-with-maven-repo
COPY --from=dev-env-with-maven-repo /root/.m2 /root/.m2
