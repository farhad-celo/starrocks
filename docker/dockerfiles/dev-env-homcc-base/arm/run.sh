tag=pr1868
build_type=Release
docker build -t sr-dev-homcc:${tag} .

sr_home=$(realpath ../../../)
container_id=$(docker run --restart unless-stopped -d -v ${sr_home}:/celostar-starrocks \
          -w ${sr_home} --env GITHUB_TOKEN=${GITHUB_TOKEN} \
          --env GITHUB_USERNAME=${GITHUB_USERNAME} \
          --env STARROCKS_GCC_HOME=/usr/ \
          --env LD_LIBRARY_PATH=/var/local/thirdparty/installed/lib \
          sr-dev-homcc:${tag} sleep infinity)
docker exec ${container_id} bash -c "cd /celostar-starrocks && BUILD_TYPE=${build_type} ./build.sh --be  -j 60"

