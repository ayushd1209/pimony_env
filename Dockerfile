FROM ubuntu:22.04

# Set noninteractive to skip timezone prompts
ENV DEBIAN_FRONTEND=noninteractive

# Update and install required packages
RUN apt-get update && apt-get install -y \
  build-essential scons python3 python3-pip git \
  libprotobuf-dev protobuf-compiler libgoogle-perftools-dev \
  libboost-all-dev zlib1g-dev pkg-config \
  libglib2.0-dev libpixman-1-dev \
  qemu qemu-system-x86 \
  cmake \
  wget curl nano vim unzip ca-certificates sudo \
  tree tmux \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install pip packages
RUN pip install --no-cache-dir conan==1.59.0

# Create user 'spec-2017' with home directory and sudo privileges
RUN useradd -m -s /bin/bash spec-2017 && \
  echo "spec-2017 ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Switch to user spec-2017
USER spec-2017
WORKDIR /home