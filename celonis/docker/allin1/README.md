# Docker file for building all in one starrocks runtime container image.
This Docker file builds starrocks FE and BE binaries and deploy it to a locally running docker container.

## Build docker image with a new starrocks release

### Build image
```
> DOCKER_BUILDKIT=1 docker build -f Dockerfile -t <tag> . 

E.g.
> DOCKER_BUILDKIT=1 docker build -f Dockerfile -t starrocks-allin1:2.4.0-rc03 . 
```

### Start container
The container can be started in these STARTMODE:
- `manual`: the default mode. will only start be, but not fe. One needs to start fe manually and add be to fe. Container will exit if be is stopped.
- `auto`: will start fe, be and configure be on fe. This option is suitable for single node cluster. Container will exit if either fe or be is stopped.
- `debug`: no service will be started. Container will not exit.
```
> docker run --env STARTMODE=[auto|manual|debug] --name <container_name> <image_name>:<tag> 

E.g
> docker run --env STARTMODE=auto --name starrocks-allin1-2.4.0-rc03 starrocks-allin1:2.4.0-rc03 
```
## Multistage Docker build graph
![](Dockerfile.png "Docker Build Graph")
