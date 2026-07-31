# syntax=docker/dockerfile:1

FROM ghcr.io/coder/code-server:latest

ARG DEBIAN_FRONTEND=noninteractive
ARG USER_UID=1000
ARG USER_GID=1000

USER root

# code-server + toàn bộ môi trường build/debug ns-3.
RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        bash-completion \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        curl \
        g++ \
        gcc \
        gdb \
        gdbserver \
        git \
        iproute2 \
        iputils-ping \
        libeigen3-dev \
        libgsl-dev \
        libsqlite3-dev \
        libxml2-dev \
        make \
        ninja-build \
        openssh-client \
        pkg-config \
        python3 \
        ripgrep \
        tcpdump \
        valgrind \
    && rm -rf /var/lib/apt/lists/*

# Đồng bộ UID/GID user coder với user trên host.
RUN set -eux; \
    current_uid="$(id -u coder)"; \
    current_gid="$(id -g coder)"; \
    \
    if [ "${current_gid}" != "${USER_GID}" ]; then \
        existing_group="$(getent group "${USER_GID}" | cut -d: -f1 || true)"; \
        if [ -n "${existing_group}" ] && [ "${existing_group}" != "coder" ]; then \
            groupdel "${existing_group}"; \
        fi; \
        groupmod --gid "${USER_GID}" coder; \
    fi; \
    \
    if [ "${current_uid}" != "${USER_UID}" ]; then \
        existing_user="$(getent passwd "${USER_UID}" | cut -d: -f1 || true)"; \
        if [ -n "${existing_user}" ] && [ "${existing_user}" != "coder" ]; then \
            userdel --remove "${existing_user}"; \
        fi; \
        usermod \
            --uid "${USER_UID}" \
            --gid "${USER_GID}" \
            coder; \
    fi; \
    \
    chown -R coder:coder /home/coder

RUN mkdir -p \
        /workspace/ns-3 \
        /home/coder/.cache/ccache \
        /home/coder/.config/code-server \
        /home/coder/.local/share/code-server \
    && chown -R coder:coder \
        /workspace \
        /home/coder

COPY --chown=coder:coder \
    docker/entrypoint.sh \
    /usr/local/bin/ns3-code-server-entrypoint

RUN chmod 0755 /usr/local/bin/ns3-code-server-entrypoint

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

ENV CCACHE_DIR=/home/coder/.cache/ccache
ENV LD_LIBRARY_PATH=/workspace/ns-3/build/lib

ENV NS3_CONFIGURE_ARGS="--build-profile=debug --enable-examples --enable-tests --disable-gtk --disable-python-bindings --disable-werror -G Ninja"

USER coder

WORKDIR /workspace/ns-3

ENTRYPOINT ["/usr/local/bin/ns3-code-server-entrypoint"]

CMD [
    "code-server",
    "--bind-addr", "0.0.0.0:8080",
    "--auth", "password",
    "--disable-telemetry",
    "/workspace/ns-3"
]