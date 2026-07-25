# syntax=docker/dockerfile:1

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG NS3_UID=1000
ARG NS3_GID=1000

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        ccache \
        cmake \
        g++ \
        gcc \
        gdb \
        gdbserver \
        git \
        iproute2 \
        libeigen3-dev \
        libgsl-dev \
        libsqlite3-dev \
        libxml2-dev \
        make \
        ninja-build \
        pkg-config \
        python3 \
        tcpdump \
        valgrind \
    && rm -rf /var/lib/apt/lists/*

RUN test "${NS3_UID}" -ne 0 \
    && test "${NS3_GID}" -ne 0 \
    && existing_user="$(getent passwd "${NS3_UID}" | cut -d: -f1)" \
    && if [ -n "${existing_user}" ]; then userdel --remove "${existing_user}"; fi \
    && existing_group="$(getent group "${NS3_GID}" | cut -d: -f1)" \
    && if [ -n "${existing_group}" ]; then groupdel "${existing_group}"; fi \
    && groupadd --gid "${NS3_GID}" ns3 \
    && useradd --create-home --uid "${NS3_UID}" --gid "${NS3_GID}" --shell /bin/bash ns3

ENV CCACHE_DIR=/home/ns3/.cache/ccache
ENV NS3_CONFIGURE_ARGS="--build-profile=debug --enable-examples --enable-tests --disable-gtk --disable-python-bindings --disable-werror -G Ninja"

WORKDIR /workspace/ns-3

COPY --chown=ns3:ns3 . .
RUN chmod +x docker/entrypoint.sh \
    && mkdir -p "${CCACHE_DIR}" \
    && chown -R ns3:ns3 /home/ns3/.cache /workspace/ns-3

USER ns3

# Configure the image's checkout. Build individual targets after startup, or
# set NS3_BUILD_ALL=1 to compile every enabled target during startup.
RUN ./docker/entrypoint.sh true

ENTRYPOINT ["./docker/entrypoint.sh"]
CMD ["bash"]
