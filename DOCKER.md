# Running ns-3.47 in containers

The container provides a headless C++ development environment with ns-3
examples and tests enabled. It runs as an unprivileged user because the ns-3
launcher intentionally refuses to run as root.

## Quick start with Docker Compose

Build and start the persistent development container:

```bash
docker compose build
docker compose up -d
docker compose exec ns3 bash
```

The Compose service bind-mounts this repository at `/workspace/ns-3`, so edits
made on the host are immediately visible in the container. On its first run,
the entrypoint configures the mounted checkout. Later runs reuse the local
`build/` directory and a named ccache volume.

Inside the container:

```bash
./ns3 build first
./ns3 run first --no-build
./ns3 build scratch-simulator
./ns3 run scratch-simulator --no-build
./test.py
```

Leave the shell with `exit`. The container continues running. Enter it again
with:

```bash
docker compose exec ns3 bash
```

Stop and remove the development container when finished:

```bash
docker compose down
```

Commands can also be run without opening a shell:

```bash
docker compose run --rm ns3 ./ns3 build first
docker compose run --rm ns3 ./ns3 run first --no-build
docker compose run --rm ns3 ./test.py -s core
```

Using `./ns3 run first` without `--no-build` is also valid, but the wrapper
invokes the default build and may compile all enabled examples and tests.
Building an explicit target first is faster for a new checkout.

## Standalone Docker usage

The Docker image contains a prebuilt copy of the repository:

```bash
docker build -t ns3-47-dev .
docker run --rm -it ns3-47-dev
docker run --rm ns3-47-dev ./ns3 build first
```

Each standalone `docker run --rm` starts from the unchanged image, so build
artifacts from one run are not retained. Use an interactive container for a
build-and-run session, or use Compose for active development and persistent
host-side artifacts.

## Configuration

The default container configuration is:

```text
--build-profile=debug
--enable-examples
--enable-tests
--disable-gtk
--disable-python-bindings
--disable-werror
-G Ninja
```

Override it when creating a new mounted build:

```bash
NS3_CONFIGURE_ARGS="--build-profile=release --enable-examples --disable-tests --disable-gtk --disable-python-bindings -G Ninja" \
  docker compose run --rm ns3
```

To configure or build manually:

```bash
NS3_SKIP_BUILD=1 docker compose run --rm ns3 bash
```

To compile every enabled module, example, and test during startup:

```bash
NS3_BUILD_ALL=1 docker compose run --rm ns3
```

On Linux hosts where the current user does not have UID/GID 1000, pass matching
IDs during the image build to keep bind-mounted files writable:

```bash
NS3_UID="$(id -u)" NS3_GID="$(id -g)" docker compose build
```

Host-network emulation through TAP, raw sockets, MPI, GUI visualization,
NetAnim, and Python bindings require additional dependencies or container
capabilities and are intentionally not enabled in the default headless image.
