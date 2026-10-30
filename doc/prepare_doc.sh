#!/bin/bash

shopt -s extglob

# Constants
BASE_DIR="dist"
VERSION=$(cat ../version.txt)
DATE=$(date +%Y%m%d)
LICENSE_DOC_DIR="$BASE_DIR/${DATE}-license-documentation-${VERSION}/"
TRANSCODER_DOC_DIR="$BASE_DIR/${DATE}-transcoder-documentation-${VERSION}/"
TRANSCODER_API_DIR="$TRANSCODER_DOC_DIR/api"
TRANSCODER_CONFIG_DIR="$TRANSCODER_DOC_DIR/config"

# Openapi source files
API_YAML_PATH="../api/api.yaml"
CONFIG_YAML_PATH="../transcoder/config/doc/openapi.yaml"

# Ensure required tools are installed
if ! command -v openapi-generator-cli &> /dev/null; then
    echo "Error: openapi-generator-cli is not installed. Please install (pipx install openapi-generator-cli) it and try again."
    echo "Preliminary environment setup:"
    echo "   #root$ apt install openjdk-21-jre zip"
    echo "   #user$ pip install pipx"
    echo "   #user$ pipx install openapi-generator-cli"
    exit 1
fi

# Create directories
mkdir -p "$LICENSE_DOC_DIR"
mkdir -p "$TRANSCODER_API_DIR"
mkdir -p "$TRANSCODER_CONFIG_DIR"

# Generate API documentation
openapi-generator-cli generate --skip-validate-spec -i "$API_YAML_PATH" -g html -o "$TRANSCODER_API_DIR"
mv "$TRANSCODER_API_DIR/index.html" "$TRANSCODER_API_DIR/api-documentation.html"
cp "$API_YAML_PATH" "$TRANSCODER_API_DIR/api-documentation.yaml"
rm -r "$TRANSCODER_API_DIR/".openapi-generator*

# Generate Config documentation
openapi-generator-cli generate --skip-validate-spec -i "$CONFIG_YAML_PATH" -g html -o "$TRANSCODER_CONFIG_DIR"
mv "$TRANSCODER_CONFIG_DIR/index.html" "$TRANSCODER_CONFIG_DIR/config-documentation.html"
cp "$CONFIG_YAML_PATH" "$TRANSCODER_CONFIG_DIR/config-documentation.yaml"
rm -r "$TRANSCODER_CONFIG_DIR/".openapi-generator*

# Copy additional files
cp -r license/* "$LICENSE_DOC_DIR"
cp src/* "$TRANSCODER_DOC_DIR"
cp -r misc "$TRANSCODER_DOC_DIR"
cp -r playlist "$TRANSCODER_DOC_DIR"

# Create zip archives
cd "$BASE_DIR"
rm -f license-documentation.zip transcoder-documentation.zip
zip -r license-documentation.zip "${DATE}-license-documentation-${VERSION}"
zip -r transcoder-documentation.zip "${DATE}-transcoder-documentation-${VERSION}"

# Clean up
rm -r "${DATE}-license-documentation-${VERSION}"
rm -r "${DATE}-transcoder-documentation-${VERSION}"

cd ..
