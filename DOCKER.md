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
the entrypoint configures the mounted checkout. Linux build output, the CMake
cache, and ccache data are stored in named Docker volumes. This prevents Linux
container binaries from being mixed with a native macOS or Windows build. The
Compose service also masks the host's default `cmake-build-debug/` CLion cache,
because the ns-3 wrapper otherwise discovers CMake caches recursively and may
select a host cache with the same debug profile.

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

`docker compose down` preserves the build volumes. To intentionally discard
the container build and force a clean reconfiguration:

```bash
docker compose down --volumes
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

The Docker image contains a configured source copy of the repository:

```bash
docker build -t ns3-47-dev .
docker run --rm -it ns3-47-dev
docker run --rm ns3-47-dev ./ns3 build first
```

Each standalone `docker run --rm` starts from the unchanged image, so build
artifacts from one run are not retained. Use an interactive container for a
build-and-run session, or use Compose for active development with persistent
Docker volumes.

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

If build volumes already exist, remove them before changing
`NS3_CONFIGURE_ARGS`; otherwise the existing CMake configuration is reused:

```bash
docker compose down --volumes
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

## Debugging

The development image includes GDB and Valgrind. The Compose service grants
`SYS_PTRACE` and uses an unconfined seccomp profile so a debugger inside the
container can trace ns-3 processes. These permissions are intended only for
this local development service.

Build the target with debug symbols:

```bash
docker compose exec ns3 ./ns3 build first -j 2
```

Open an interactive GDB session:

```bash
docker compose exec ns3 ./ns3 run first --no-build --gdb
```

At the GDB prompt:

```gdb
break main
run
backtrace
continue
```

Run GDB non-interactively to verify that breakpoints work:

```bash
docker compose exec ns3 \
  gdb --batch \
      -ex "break main" \
      -ex "run" \
      -ex "backtrace" \
      --args build/examples/tutorial/ns3.47-first-debug
```

Run an already-built target through Valgrind:

```bash
docker compose exec ns3 \
  ./ns3 run first --no-build --valgrind
```

Enable focused ns-3 logging without a debugger:

```bash
docker compose exec \
  -e NS_LOG="UdpEchoClientApplication=level_all|prefix_time|prefix_func" \
  ns3 ./ns3 run first --no-build
```

See `NS3_SOURCE_AND_WORKFLOW_TUTORIAL.md`, section 16, for source-level
breakpoints, packet-flow debugging, test-runner debugging, sanitizers, and the
VS Code workflow.

## Remote debugging: Ubuntu Server container and macOS UI

The recommended remote setup keeps the compiler, Linux executable, GDB, and
build artifacts together on the Ubuntu Server. VS Code runs its UI on macOS
and connects to the server with Remote - SSH, then attaches to the running
container with Dev Containers:

```text
macOS: VS Code UI
    -> SSH
Ubuntu Server: VS Code remote extension host
    -> Docker attach
ns3 container: source + build + GDB + running simulation
```

This avoids trying to debug a Linux executable with the macOS LLDB toolchain.
On macOS, install the VS Code extensions `Remote - SSH`, `Dev Containers`, and
`C/C++`. Add the Ubuntu host to `~/.ssh/config`, for example:

```sshconfig
Host ns3-server
    HostName 192.0.2.10
    User developer
    IdentityFile ~/.ssh/id_ed25519
```

Clone the repository on Ubuntu, then build and start the existing development
service there:

```bash
git clone <repository-url>
cd ns-3.47-dev
NS3_UID="$(id -u)" NS3_GID="$(id -g)" docker compose build
docker compose up -d
docker compose exec ns3 ./ns3 build first -j 2
```

In VS Code on macOS:

1. Run `Remote-SSH: Connect to Host` and select `ns3-server`.
2. Open the repository directory on Ubuntu.
3. Run `Dev Containers: Attach to Running Container`.
4. Select the `ns3` container and open `/workspace/ns-3`.
5. Open an example program such as `examples/tutorial/first.cc`, set a
   breakpoint, and start the `Debug ns-3 program in container` launch
   configuration described in the tutorial.

All file paths visible to the debugger are container paths, so breakpoint
mapping works without copying Linux binaries to macOS. The original
command-line GDB workflow above remains available.

### Remote SSH with GDB through Docker exec

The `Remote SSH: debug through Docker exec` VS Code profile is the shortest
workflow when the editor is connected to the Ubuntu Server with Remote - SSH
but is not attached to the container. It starts GDB inside the existing `ns3`
service through `docker compose exec`. The pipe ends with `sh -c` so the
debugger path and MI arguments added by the C/C++ extension are parsed as a
command instead of one executable filename. It does not use gdbserver or port
2345.

On Ubuntu, start the service and build the program:

```bash
docker compose up -d
docker compose exec ns3 ./ns3 build first -j 2
```

In VS Code on macOS:

1. Connect to Ubuntu with `Remote-SSH: Connect to Host`.
2. Open the repository on Ubuntu, for example
   `/home/ubuntu/ns-3.47-dev`.
3. Install the Microsoft C/C++ extension in the SSH remote environment.
4. Open the source file where the breakpoint should be placed.
5. Select `Remote SSH: debug through Docker exec`.
6. Press F5 and enter the executable path relative to `build/`, for example
   `examples/tutorial/ns3.47-first-debug`.

That input resolves to:

```text
/workspace/ns-3/build/examples/tutorial/ns3.47-first-debug
```

The profile maps `/workspace/ns-3` back to the repository opened over SSH, so
breakpoints appear in the host-side editor while GDB and the executable remain
inside the container. The SSH user must have permission to run Docker. Verify
that before debugging:

```bash
docker compose exec ns3 gdb --version
```

The selected editor tab does not affect the executable path. Obtain the exact
path for another target with `./ns3 run TARGET --no-build --dry-run`, remove
the `/workspace/ns-3/build/` prefix, and enter the remaining path at the VS
Code prompt.

### Optional direct gdbserver transport

The image also includes `gdbserver`, and the main Compose service publishes its
remote-debug port for IDEs that need the GDB remote protocol:

```bash
docker compose up -d --build

docker compose exec ns3 ./ns3 build first -j 2
docker compose exec ns3 \
  gdbserver 0.0.0.0:2345 \
  /workspace/ns-3/build/examples/tutorial/ns3.47-first-debug
```

Keep that terminal running. The exact executable path can be confirmed with:

```bash
docker compose exec ns3 ./ns3 run first --no-build --dry-run
```

Find the Ubuntu Server address:

```bash
hostname -I
```

Configure the debugger UI on macOS to connect directly to
`UBUNTU_SERVER_IP:2345`. No SSH tunnel is required. `compose.yaml` publishes
the port on `0.0.0.0:${NS3_GDB_PORT:-2345}`.

Because `gdbserver` has no authentication or encryption, allow only the Mac's
trusted IP through the Ubuntu or network firewall. For example, when UFW is
enabled:

```bash
sudo ufw allow from MAC_IP to any port 2345 proto tcp
```

Do not expose this port directly to the public Internet. Use a trusted LAN or
VPN for direct access.

### Connect VS Code on macOS directly to gdbserver

The repository's `.vscode/launch.json` includes the
`Remote gdbserver: ns-3 first` profile. It expects the matching executable and
ns-3 shared libraries under the ignored local `remote-debug/` directory.

After building on Ubuntu, export the symbol bundle from the container:

```bash
mkdir -p remote-debug
docker compose cp \
  ns3:/workspace/ns-3/build/examples/tutorial/ns3.47-first-debug \
  remote-debug/ns3.47-first-debug
docker compose cp \
  ns3:/workspace/ns-3/build/lib \
  remote-debug/lib
```

Transfer that directory to the repository checkout on macOS after every
rebuild. For example, run this from the Mac:

```bash
rsync -av \
  developer@UBUNTU_SERVER_IP:/path/to/ns-3.47-dev/remote-debug/ \
  ./remote-debug/
```

Install the VS Code C/C++ extension and GDB on Apple Silicon macOS:

```bash
brew install gdb
```

The supplied profile uses `/opt/homebrew/bin/gdb`. Change
`miDebuggerPath` in `.vscode/launch.json` when `which gdb` reports another
path.

Start gdbserver on Ubuntu:

```bash
docker compose exec ns3 \
  gdbserver 0.0.0.0:2345 \
  /workspace/ns-3/build/examples/tutorial/ns3.47-first-debug
```

In VS Code on the Mac:

1. Open `examples/tutorial/first.cc` and set a breakpoint in `main()`.
2. Select `Remote gdbserver: ns-3 first` in Run and Debug.
3. Press F5.
4. Enter `UBUNTU_SERVER_IP:2345` when prompted.

The profile maps `/workspace/ns-3` to the local workspace, obtains Linux
system libraries through gdbserver, and loads ns-3 shared-library symbols from
`remote-debug/lib`. Restart gdbserver before each new session.

Stop the optional transport with `Ctrl+C`. Stop the development environment
without deleting its named build volumes:

```bash
docker compose down
```
