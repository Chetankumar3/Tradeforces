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

resource "google_compute_firewall" "allow_public_ingress" {
  name    = "app-allow-public-ingress"
  network = google_compute_network.app_vpc.name

  allow {
    protocol = "tcp"
    ports    = ["80", "8080"]
  }

  # Allow from anywhere
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


resource "google_storage_bucket" "submissions_bucket" {
  name          = "iicpc-submissions"
  location      = "us-central1"
  force_destroy = true

  uniform_bucket_level_access = true
  public_access_prevention    = "enforced" # Ensures no one can make it public

  # CORS Policy for frontend
  cors {
    origin          = ["http://localhost:3000", "https://your-frontend.com"]
    method          = ["PUT", "OPTIONS"]
    response_header = ["Content-Type", "Authorization"]
    max_age_seconds = 3600
  }
}

resource "google_storage_bucket" "tfapp_gcs" {
  name          = "tradeforces-configs"
  location      = "us-central1"
  force_destroy = true 
  uniform_bucket_level_access = true
}

resource "google_storage_bucket_object" "static_files" {
  for_each = toset([
    "machine-setup.sh",
    "infrastructure.yaml"
  ])

  name   = each.value
  bucket = google_storage_bucket.tfapp_gcs.name
  source = "${path.module}/templates/${each.value}" 
}

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

  metadata_startup_script = replace(<<-EOF
    #!/bin/bash
      
    # Log output so you can debug via 'cat /var/log/startup-script.log'
    exec > /var/log/startup-script.log 2>&1

    mkdir -p /myapp
    cd /myapp

    echo "Waiting for apt lock..."
    while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1; do
      sleep 2
    done

    sudo apt-get update
    sudo apt-get install -y apt-transport-https ca-certificates curl gnupg lsb-release nano unzip

    gcloud storage cp -r gs://tradeforces-configs/* .
    sudo chmod +x machine-setup.sh
    ./machine-setup.sh
  EOF
  , "\r", "")

  service_account {
    email  = "artifact-registry-puller-898@project-cdd074dc-6291-4d7f-a2a.iam.gserviceaccount.com"
    scopes = ["cloud-platform"]
  }

  depends_on = [
    google_storage_bucket_object.static_files
  ]

  # Ensure the instance stops/starts properly if GCP does maintenance
  # Required when nested virtualization is enabled
  scheduling {
    on_host_maintenance = "TERMINATE"
  }
}

/*
resource "google_pubsub_topic" "queue1_submissions" {
  name = "queue1-6291"

  message_retention_duration = "86400s"
}

resource "google_pubsub_subscription" "queue1_pull_sub" {
  name  = "queue1-6291-subscription"
  topic = google_pubsub_topic.queue1_submissions.name

  ack_deadline_seconds = 10

  # Retain unacknowledged messages for 2 days
  message_retention_duration = "1712800s"

  # Prevents one bad message from crashing the pipeline infinitely
  retry_policy {
    minimum_backoff = "10s"
    maximum_backoff = "600s"
  }
}

resource "google_pubsub_topic" "queue2_microvms" {
  name = "queue2-6291"

  message_retention_duration = "86400s"
}

resource "google_pubsub_subscription" "queue2_pull_sub" {
  name  = "queue2-6291-subscription"
  topic = google_pubsub_topic.queue1_submissions.name

  ack_deadline_seconds = 10

  # Retain unacknowledged messages for 2 days
  message_retention_duration = "1712800s"

  # Prevents one bad message from crashing the pipeline infinitely
  retry_policy {
    minimum_backoff = "10s"
    maximum_backoff = "600s"
  }
} */