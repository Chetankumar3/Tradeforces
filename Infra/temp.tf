terraform {
  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.0"
    }
  }
}

provider "google" {
  project = "project-cdd074dc-6291-4d7f-a2a"
  region  = "us-central1"
  zone    = "us-central1-a"
}

# ==========================================
# Networking (VPC & Subnet)
# ==========================================

resource "google_compute_network" "app_vpc" {
  name                    = "app-network"
  auto_create_subnetworks = false
}

resource "google_compute_subnetwork" "app_subnet" {
  name          = "app-subnet-central1"
  ip_cidr_range = "10.0.1.0/24"
  region        = "us-central1"
  network       = google_compute_network.app_vpc.id
}

# ==========================================
# Private IP Reservation
# ==========================================

resource "google_compute_address" "app_internal_ip" {
  name         = "app-gce-internal-ip"
  subnetwork   = google_compute_subnetwork.app_subnet.id
  address_type = "INTERNAL"
  region       = "us-central1"
  # You can optionally hardcode an IP by uncommenting the line below:
  # address      = "10.0.1.10" 
}

# ==========================================
# Firewall Rules
# ==========================================

resource "google_compute_firewall" "allow_ssh" {
  name    = "app-allow-ssh"
  network = google_compute_network.app_vpc.name

  allow {
    protocol = "tcp"
    ports    = ["22"]
  }

  # Allow SSH from anywhere (for testing/hackathon purposes)
  source_ranges = ["0.0.0.0/0"]
  
  # Only apply this rule to instances with this tag
  target_tags = ["app-node"]
}

resource "google_compute_firewall" "allow_internal" {
  name    = "app-allow-internal"
  network = google_compute_network.app_vpc.name

  # Allow all internal TCP, UDP, and ICMP traffic within the subnet
  allow {
    protocol = "tcp"
    ports    = ["0-65535"]
  }
  allow {
    protocol = "udp"
    ports    = ["0-65535"]
  }
  allow {
    protocol = "icmp"
  }

  source_ranges = [google_compute_subnetwork.app_subnet.ip_cidr_range]
}

# ==========================================
# Compute Engine Instance
# ==========================================

resource "google_compute_instance" "app_sandbox" {
  name         = "app-gce-01"
  machine_type = "n2-custom-6-6144" # 6 vCPUs, 6144 MB (6GB) RAM
  zone         = "us-central1-a"

  # Network tag maps to the firewall rules above
  tags = ["app-node"]

  # Allows Firecracker to use KVM
  advanced_machine_features {
    enable_nested_virtualization = true
  }

  boot_disk {
    initialize_params {
      image = "ubuntu-nested-virt"
      size  = 30
      type  = "pd-balanced"
    }
  }

  network_interface {
    network    = google_compute_network.app_vpc.id
    subnetwork = google_compute_subnetwork.app_subnet.id
    
    # Attach the reserved private internal IP
    network_ip = google_compute_address.app_internal_ip.address
    
    # This block assigns an ephemeral public IP so you can SSH into it from the internet
    access_config {
      // Ephemeral public IP
    }
  }

  # Ensure the instance stops/starts properly if GCP does maintenance
  # Required when nested virtualization is enabled
  scheduling {
    on_host_maintenance = "TERMINATE"
  }
}