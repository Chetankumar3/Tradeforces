#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

# ==========================================
# 1. THE VERSION ENV
# ==========================================
FIRECRACKER_VERSION="v1.3.3"
KATA_VERSION="3.2.0"
# Pinning a specific stable K3s version prevents unexpected Kubernetes API changes
K3S_VERSION="v1.28.7+k3s1" 
ARCH="x86_64" # Using x86_64 for standard GCE instances

echo "Starting Infrastructure Setup..."

# ==========================================
# 2 & 3. INSTALL KATA CONTAINERS WITH BUNDLED FIRECRACKER
# ==========================================
echo "--> Installing Kata Containers & Bundled Firecracker..."
cd /tmp
wget -q https://github.com/kata-containers/kata-containers/releases/download/3.2.0/kata-static-3.2.0-amd64.tar.xz
sudo tar -xJf kata-static-3.2.0-amd64.tar.xz -C /

# CREATE SYMLINKS (This moves Kata shims AND Firecracker v1.3.3 into the global path)
sudo ln -sf /opt/kata/bin/* /usr/local/bin/

# FIX BUNDLED PERMISSIONS (Kata extracts as root:root, so we just make it executable for everyone)
sudo chmod a+x /usr/local/bin/firecracker

# ==========================================
# 4. INSTALL K3S (AND CONTAINERD)
# ==========================================
echo "--> Installing K3s ($K3S_VERSION)..."
# We pass the version env to the K3s installer script. 
# It will automatically install containerd and configure the systemd service.
curl -sfL https://get.k3s.io | INSTALL_K3S_VERSION=${K3S_VERSION} sh -

echo "=========================================="
echo "Installation Complete!"
echo "Firecracker: $(firecracker --version | head -n 1)"
echo "Kata: $(/opt/kata/bin/kata-runtime --version | head -n 1)"
echo "K3s: $(k3s --version | head -n 1)"
echo "=========================================="