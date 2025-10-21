docker build -t sr-dev:pr1868 .
docker run -d -it -v /tmp:/tmp --name sr-dev --restart unless-stopped sr-dev:pr1868