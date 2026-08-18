docker build -t sr-dev:main .
docker run -d -it -v /tmp:/tmp --name sr-dev-main --restart unless-stopped sr-dev:main