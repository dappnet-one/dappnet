#!/bin/bash
set -e
export IMAGE_NAME="dpnit/dpn:$BRANCH_NAME"
if [[ $IMAGE_NAME == "dpnit/dpn:stable" ]] ; then
  IMAGE_NAME="dpnit/dpn:latest"
fi
sudo docker build --build-arg CI_BUILD=1 --build-arg BUILD_STEP=2 -t=$IMAGE_NAME .
sudo docker login --username=$DOCKER_USER --password=$DOCKER_PASS
sudo docker push $IMAGE_NAME
# make docker cleanup after itself and delete all exited containers
sudo docker rm -v $(docker ps -a -q -f status=exited) || true
