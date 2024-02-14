#!/bin/bash

# Check if the image tag is provided as an argument
if [ $# -ne 1 ]; then
    echo "Usage: $0 <image_tag>"
    exit 1
fi

# Assign the image tag from the input argument
image_tag=$1

# Start the container
container_id=$(docker run -d --name my_container ghcr.io/celonis/celostar/starrocks-allin1:${image_tag})

# Copy the file from the container to the local disk
docker cp my_container:/data/deploy/starrocks/be/lib/starrocks_be.debuginfo ./starrocks_be.debuginfo.${image_tag}

# Stop and remove the container
docker stop my_container
docker rm my_container
docker rmi ghcr.io/celonis/celostar/starrocks-allin1:${image_tag}
