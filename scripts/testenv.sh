#!/usr/bin/env bash

IMAGE_NAME="cs214-test-env"

cd "$(dirname "$0")/.." || exit

if ! docker image inspect $IMAGE_NAME > /dev/null 2>&1; then
    echo "First time setup: Building Debian Docker image ($IMAGE_NAME)..."
    docker build -t $IMAGE_NAME -f Dockerfile .
fi

echo "mounted at /workspace"
echo "--------------------------------------------------------"

docker run -it --rm -v "$(pwd):/workspace" $IMAGE_NAME