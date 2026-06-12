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
  region  = "us-east1"
  zone    = "us-east1-a"
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
  region        = "us-east1"
  network       = google_compute_network.app_vpc.id
}

# ==========================================
# Private IP Reservation
# ==========================================
# Required for Private IP Google Services (Postgres, Pub/Sub)
resource "google_compute_global_address" "private_ip_alloc" {
  name          = "private-ip-alloc"
  purpose       = "VPC_PEERING"
  address_type  = "INTERNAL"
  prefix_length = 16
  network       = google_compute_network.app_vpc.id
}

resource "google_service_networking_connection" "private_vpc_connection" {
  network                 = google_compute_network.app_vpc.id
  service                 = "servicenetworking.googleapis.com"
  reserved_peering_ranges = [google_compute_global_address.private_ip_alloc.name]
  update_on_creation_fail = true

  deletion_policy = "ABANDON"
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
    ports    = ["80", "8080", "5000"]
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
  location      = "us-east1"
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

resource "google_storage_bucket" "app_gcs" {
  name          = "tradeforces-configs"
  location      = "us-east1"
  force_destroy = true 
  uniform_bucket_level_access = true
}

resource "google_storage_bucket_object" "env_file" {
  name   = ".env"
  bucket = google_storage_bucket.app_gcs.name
  
  content = templatefile("${path.module}/templates/env.tpl", {
    gcs_bucket_name = google_storage_bucket.submissions_bucket.name
    queue1_name = google_pubsub_topic.queue1_submissions.name
    queue1_subscription_name = google_pubsub_subscription.queue1_pull_sub.name
    queue2_name = google_pubsub_topic.queue2_microvms.name
    queue2_subscription_name = google_pubsub_subscription.queue2_pull_sub.name
    db_private_ip = google_sql_database_instance.app_db.ip_address.0.ip_address
    db_name = google_sql_database.app_db_database.name
    db_user = google_sql_user.db_user.name
    db_password = google_sql_user.db_user.password
  })
}

resource "google_storage_bucket_object" "static_files" {
  for_each = toset([
    "machine-setup.sh",
    "k3s.yaml",
    "gar-key.json"
  ])

  name   = each.value
  bucket = google_storage_bucket.app_gcs.name
  source = "${path.module}/templates/${each.value}" 
}

resource "google_compute_instance" "app_sandbox" {
  name         = "app-gce-01"
  machine_type = "n2-custom-6-24576"
  zone         = "us-east1-c"

  # Network tag maps to the firewall rules above
  tags = ["app-node"]

  # Allows Hardware virtualization
  advanced_machine_features {
    enable_nested_virtualization = true
  }

  reservation_affinity {
    type = "SPECIFIC_RESERVATION"
    specific_reservation {
      key    = "compute.googleapis.com/reservation-name"
      values = ["app-sandbox-reservation"] 
    }
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
    access_config {
      // Ephemeral public IP
    }
  }

  metadata_startup_script = replace(<<-EOF
    #!/bin/bash

    exec > /var/log/startup-script.log 2>&1

    mkdir -p /root/myapp
    cd /root/myapp

    echo "Waiting for apt lock..."
    while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1; do
      sleep 2
    done

    apt-get update
    apt-get install -y apt-transport-https ca-certificates curl gnupg lsb-release nano unzip

    gcloud storage cp -r gs://tradeforces-configs/* .
    
    chmod +x machine-setup.sh
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


resource "google_sql_database_instance" "app_db" {
  name             = "app-postgres-6291"
  database_version = "POSTGRES_15"
  region           = "us-east1"

  settings {
    tier              = "db-custom-2-4096" 
    availability_type = "ZONAL"
    disk_autoresize = false
    disk_size = 15
    
    ip_configuration {
      ipv4_enabled    = true
      private_network = google_compute_network.app_vpc.id
    }

    backup_configuration {
      enabled = true
    }

    insights_config {
      query_insights_enabled  = true
      query_string_length     = 1024
      record_application_tags = false
      record_client_address   = false
    }
  }

  deletion_protection = false 
  depends_on          = [google_service_networking_connection.private_vpc_connection]
}

resource "google_sql_database" "app_db_database" {
  name     = "testdb"
  instance = google_sql_database_instance.app_db.name
}

resource "google_sql_user" "db_user" {
  name     = "postgres"
  instance = google_sql_database_instance.app_db.name
  password = "Ch92.8%%"
}


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
  topic = google_pubsub_topic.queue2_microvms.name

  ack_deadline_seconds = 10

  # Retain unacknowledged messages for 2 days
  message_retention_duration = "1712800s"

  # Prevents one bad message from crashing the pipeline infinitely
  retry_policy {
    minimum_backoff = "10s"
    maximum_backoff = "600s"
  }
}