# celonis

Contains Celonis build extension and other artifacts that you'd normally expect
to find at the top-level. Keeping in a subdirectory avoids potential merge
collisions with the upstream.

* **build.sh** builds everything including the docker image. This is invoked by
  workflows/celonis_main-build.yml whenever there's a push to main in order to
  build a new image.
* **run-in-dev-image.sh** used by build.sh but is also a useful debugging
  utility by itself as it allows you to run an arbitrary command in the docker
  dev-image.
* **Dockerfile** used by build.sh to build the image.

## How to sync from upstream

1. Sync via the normal means. TODO: whoever does this next, please fill this in.
2. Update [STARROCKS_DEV_IMAGE](https://github.com/celonis/celostar-starrocks/blob/main/celonis/run-in-dev-image.sh#L9)
3. Following a push to main, it'll kick off a CI workflow to build a
   new docker
   image [here](https://github.com/orgs/celonis/packages/container/package/docker-registry%2Fcelostar-starrocks)
   .
4. Once that completes successfully, update the default value
   of [celostar-starrocks-image](https://github.com/celonis/celostar/blob/main/pom.xml#L14)
   to reference the new hash.