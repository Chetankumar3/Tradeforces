#!/bin/bash
# Exit immediately if a command exits with a non-zero status.
set -e

echo "=========================================="
echo "Starting Infrastructure Setup: (K3s && containerd) + (Kata && QEMU)"
echo "=========================================="

# Variables
KATA_VERSION="3.12.0"
K3S_VERSION="v1.36.1+k3s1" 
CONTAINERD_DIR="/var/lib/rancher/k3s/agent/etc/containerd"
KUBECONFIG="/etc/rancher/k3s/k3s.yaml"
export KUBECONFIG

echo "--> Checking for Hardware Virtualization (KVM)..."
if [ ! -e /dev/kvm ]; then
    echo "ERROR: /dev/kvm does not exist. Hardware virtualization is not enabled on this machine!"
    exit 1
fi
echo "KVM is enabled."


echo "--> Installing Kata Containers v${KATA_VERSION}..."
cd /tmp
wget -q "https://github.com/kata-containers/kata-containers/releases/download/${KATA_VERSION}/kata-static-${KATA_VERSION}-amd64.tar.xz"
tar -xJf "kata-static-${KATA_VERSION}-amd64.tar.xz" -C /
# Create global symlinks
sudo ln -sf /opt/kata/bin/* /usr/local/bin/
echo "--> Verifying Kata Runtime..."
sudo /usr/local/bin/kata-runtime check


echo "--> Installing K3s (${K3S_VERSION})..."
curl -sfL https://get.k3s.io | INSTALL_K3S_VERSION=${K3S_VERSION} sh -
sudo kubectl create configmap app-env --from-env-file=/myapp/.env --dry-run=client -o yaml | sudo kubectl apply -f -


echo "--> Waiting for K3s to generate default containerd config..."
while [ ! -f "${CONTAINERD_DIR}/config.toml" ]; do
    sleep 2
done
echo "--> Creating containerd config template..."
sudo cp "${CONTAINERD_DIR}/config.toml" "${CONTAINERD_DIR}/config.toml.tmpl"
# Append Kata configuration if it doesn't already exist in the file
if ! sudo grep -q "kata-qemu" "${CONTAINERD_DIR}/config.toml.tmpl"; then
sudo cat <<EOF >> "${CONTAINERD_DIR}/config.toml.tmpl"
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata-qemu]
  runtime_type = "io.containerd.kata.v2"
  privileged_without_host_devices = true
  pod_annotations = ["io.katacontainers.*"]
  
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.kata-qemu.options]
  ConfigPath = "/opt/kata/share/defaults/kata-containers/configuration-qemu.toml"
EOF
    echo "Kata configuration appended to template."
else
    echo "Kata configuration already exists in template, skipping append."
fi


echo "--> Restarting K3s to apply containerd template..."
systemctl restart k3s

# Wait for containerd to reload
sleep 5 

echo "--> Verifying Containerd Plumbing..."
if sudo crictl info | grep -q "kata-qemu"; then
    echo "Containerd successfully loaded kata-qemu runtime."
else
    echo "ERROR: Containerd plumbing failed. kata-qemu not found in crictl info."
    exit 1
fi


echo "--> Applying infrastructure to k3s cluster..."
sudo kubectl apply -f /myapp/infrastructure.yaml

echo "=========================================="
echo "Setup Complete! Cluster is ready for MicroVMs."
echo "Kata version: $(sudo kata-runtime --version | head -n 1)"
echo "K3s version: $(sudo k3s --version | head -n 1)"
echo "=========================================="