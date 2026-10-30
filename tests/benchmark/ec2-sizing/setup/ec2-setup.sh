#!/usr/bin/env bash
# One-time preparation of a fresh Ubuntu 24.04 EC2 instance for the sizing benchmark.
# Run as the default 'ubuntu' user:  bash setup/ec2-setup.sh [--phoronix]
set -euo pipefail

sudo apt-get update -y
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    docker.io python3 sysstat curl unzip jq pigz zstd ca-certificates
sudo usermod -aG docker "$USER"

# Bounded container logs: the engine logs at 'log' level to the console.
sudo mkdir -p /etc/docker
echo '{"log-driver":"json-file","log-opts":{"max-size":"20m","max-file":"2"}}' | sudo tee /etc/docker/daemon.json >/dev/null
sudo systemctl enable --now docker
sudo systemctl restart docker

# UDP receive/send buffers for many concurrent MPEG-TS streams (the harness captures over loopback).
sudo tee /etc/sysctl.d/90-utplbench.conf >/dev/null <<'EOF'
net.core.rmem_max = 33554432
net.core.wmem_max = 33554432
net.core.rmem_default = 1048576
net.core.netdev_max_backlog = 30000
EOF
sudo sysctl --system >/dev/null

# Keep background package activity out of the measurements.
sudo systemctl disable --now unattended-upgrades apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true

if [ "${1:-}" = "--phoronix" ]; then
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y phoronix-test-suite php-cli php-xml
fi

echo
echo "Done. Log out and back in (or run 'newgrp docker') so docker works without sudo."
echo "Then: bash setup/load-image.sh <image.tar.gz>  &&  python3 bench/bench.py check"
