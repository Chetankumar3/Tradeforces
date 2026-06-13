# Multi-stage build for VM Creator Service
FROM python:3.11-slim AS builder

WORKDIR /build

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements
COPY microservices/vm_creator/requirements.txt .

# Create virtual environment
RUN python -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# Install dependencies
RUN pip install --no-cache-dir --upgrade pip setuptools wheel
RUN pip install --no-cache-dir -r requirements.txt


# Final runtime stage
FROM python:3.11-slim

WORKDIR /app

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy virtual environment from builder
COPY --from=builder /opt/venv /opt/venv

# Set environment
ENV PATH="/opt/venv/bin:$PATH" \
    PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1

RUN useradd -m -u 1000 appuser

# Copy shared core AND set ownership in the exact same step
COPY --chown=appuser:appuser shared_core /app/shared_core

# Copy vm_creator service AND set ownership in the exact same step
COPY --chown=appuser:appuser microservices/vm_creator /app/microservices/vm_creator

# DELETE the RUN chown -R appuser:appuser /app line entirely

# Switch to non-root user
USER appuser

# Start the worker directly
CMD ["python", "-m", "microservices.vm_creator.main"]
# python -m microservices.vm_creator.main
