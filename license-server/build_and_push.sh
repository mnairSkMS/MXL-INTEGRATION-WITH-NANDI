#!/bin/bash

# Exit on error
set -e

# Read version from version.txt
VERSION=$(cat version.txt | tr -d ' \r')

# Check if version is empty
if [ -z "$VERSION" ]; then
    echo "Error: version.txt is empty"
    exit 1
fi

# Docker image name
IMAGE_NAME="ghcr.io/skandha-media-services/utpl/license-server"

# Build the image
echo "Building Docker image..."
docker build -t ${IMAGE_NAME}:latest \
    -t ${IMAGE_NAME}:${VERSION} \
    --build-arg VERSION=${VERSION} \
    --build-arg BUILD_DATE=$(date +%Y-%m-%d) \
    --build-arg BUILD_COMMIT=$(git rev-parse --short HEAD) \
    .

# Check if we're logged in to ghcr.io
if ! docker info | grep -q "ghcr.io"; then
    echo "Please login to ghcr.io first using:"
    echo "echo \$GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin"
    exit 1
fi

# Push both tags
echo "Pushing Docker images..."
docker push ${IMAGE_NAME}:${VERSION}

echo "Successfully built and pushed:"
echo "  - ${IMAGE_NAME}:${VERSION}" 