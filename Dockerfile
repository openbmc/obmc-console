# Dockerfile for building obmc-console with Make
FROM ubuntu:22.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    pkg-config \
    libsystemd-dev \
    libiniparser-dev \
    libgpiod-dev \
    libudev-dev \
    socat \
    git \
    && rm -rf /var/lib/apt/lists/*

# Create a working directory
WORKDIR /workspace

# Copy the source code
COPY . .

# Set up the build environment
RUN echo "Build environment ready!"

# Default command to show build instructions
CMD echo "obmc-console build container ready!" && \
    echo "" && \
    echo "To build:" && \
    echo "  ./configure" && \
    echo "  make" && \
    echo "" && \
    echo "To test:" && \
    echo "  ./test-build.sh" && \
    echo "" && \
    echo "To run interactive shell:" && \
    echo "  docker run -it <image> /bin/bash" && \
    /bin/bash
