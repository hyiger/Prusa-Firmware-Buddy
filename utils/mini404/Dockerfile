# Mini404, vintagepc's QEMU fork for Prusa Buddy boards, in its development build. The dev build has
# the prusa-core-one machines; the v0.9.10 that utils/bootstrap.py pins does not. The Linux build is
# x86-64 only and is built on Ubuntu 22.04, so build the image with --platform linux/amd64.
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update -qq \
    && apt-get install -y --no-install-recommends \
        ca-certificates curl bzip2 \
        libgl1 libglew2.2 libglu1-mesa freeglut3 libepoxy0 libgbm1 libx11-6 \
        libgtk-3-0 libcairo2 libglib2.0-0 libpixman-1-0 libjpeg-turbo8 libpng16-16 \
        libslirp0 libusb-1.0-0 \
    && rm -rf /var/lib/apt/lists/*

ARG MINI404_URL=https://github.com/vintagepc/MINI404/releases/download/dev/Mini404-dev-linux.tar.bz2
RUN curl -fsSL "${MINI404_URL}" -o /tmp/mini404.tar.bz2 \
    && sha256sum /tmp/mini404.tar.bz2 | tee /opt/mini404.sha256 \
    && mkdir -p /opt/mini404 \
    && tar -xjf /tmp/mini404.tar.bz2 -C /opt/mini404 --strip-components=1 \
    && rm /tmp/mini404.tar.bz2 \
    && ! ldd /opt/mini404/qemu-system-buddy | grep "not found"

# The Linux build has no keymaps, which QEMU's VNC server needs. The macOS build of v0.9.10 has them.
ARG KEYMAPS_URL=https://github.com/vintagepc/MINI404/releases/download/v0.9.10/Mini404-v0.9.10-macos.tar.bz2
RUN curl -fsSL "${KEYMAPS_URL}" | tar -xjf - -C /tmp --wildcards '*/pc-bios/keymaps/*' \
    && mkdir -p /opt/mini404/pc-bios \
    && mv /tmp/Mini404-*/pc-bios/keymaps /opt/mini404/pc-bios/keymaps \
    && rm -rf /tmp/Mini404-*

WORKDIR /work
ENTRYPOINT ["/opt/mini404/qemu-system-buddy", "-L", "/opt/mini404/pc-bios"]
