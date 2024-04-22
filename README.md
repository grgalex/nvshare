## `nvshare`: Practical GPU Sharing Without Memory Size Constraints

`nvshare` is a GPU sharing mechanism that allows multiple processes (or containers running on Kubernetes) to securely run on the same physical GPU concurrently, each having the whole GPU memory available.

You can watch a quick explanation plus a **demonstration** at https://www.youtube.com/watch?v=9n-5sc5AICY.

To achieve this, it transparently enables GPU page faults using the system RAM as swap space. To avoid thrashing, it uses `nvshare-scheduler`, which manages the GPU and gives exclusive GPU access to a single process for a given time quantum (TQ), which has a default duration of 30 seconds.

This functionality solely depends on the Unified Memory API provided by the NVIDIA kernel driver. It is highly unlikely that an update to NVIDIA's kernel drivers would interfere with the viability of this project as it would require disabling Unified Memory.

The de-facto way (Nvidia's device plugin) of handling GPUs on Kubernetes is to assign them to containers in a 1-1 manner. This is especially inefficient for applications that only use a GPU in bursts throughout their execution, such as long-running interactive development jobs like Jupyter notebooks.

I've written a [Medium article](https://grgalex.medium.com/gpu-virtualization-in-k8s-challenges-and-state-of-the-art-a1cafbcdd12b) on the challenges of GPU sharing on Kubernetes, it's worth a read.

### Indicative Use Cases

- Run 2+ processes/containers with infrequent GPU bursts on the same GPU (e.g., interactive apps, ML inference)
- Run 2+ non-interactive workloads (e.g., ML training) on the same GPU to minimize their total completion time and reduce queueing

## Table of Contents
- [Features](#features)
- [Key Idea](#key_idea)
- [Supported GPUs](#supported_gpus)
- [Overview](#overview)
  - [`nvshare` components](#components)
  - [Some Details on `nvshare-scheduler`](#details_scheduler)
  - [Memory Oversubscription For a Single Process](#single_oversub)
  - [The Scheduler's Time Quantum (TQ)](#scheduler_tq)
- [Further Reading](#further_reading)
- [Deploy on a Local System](#deploy_local)
  - [Installation (Local)](#installation_local)
  - [Usage (Local)](#usage_local)
  - [Test (Local)](#test_local)
- [Deploy on Kubernetes](#deploy_k8s)
  - [Installation (Kubernetes)](#installation_k8s)
  - [Usage (Kubernetes)](#usage_k8s)
    - [Use an `nvshare.com/gpu` Device](#usage_k8s_device)
    - [(Optional) Configure scheduler using `nvsharectl`](#usage_k8s_conf)
  - [Test (Kubernetes)](#test_k8s)
  - [Uninstall (Kubernetes)](#uninstall_k8s)
- [Build For Local Use](#build_local)
- [Build Docker Images](#build_docker)
- [Future Improvements](#future_improves)
- [Feedback](#feedbk)
- [Cite This Work](#cite)

<a name="features"/>

## Features

- Single GPU sharing among multiple processes/containers
- Memory and fault isolation is guaranteed because co-located processes use different CUDA contexts, unlike other approaches such as NVIDIA MPS.
- Completely transparent to applications, no code changes needed
- Each process/container has whole GPU memory available
   - Uses Unified Memory to swap GPU memory to system RAM
   - Scheduler optionally serializes overlapping GPU work to avoid thrashing (assigns exclusive access to one app for TQ seconds at a time)
   - Apps release GPU if done with work before TQ elapses
- Device plugin for Kubernetes

<a name="key_ideas"/>

## Key Idea

1. With `cudaMalloc()`, the sum of memory allocations from CUDA apps must be smaller than physical GPU memory size (`Σ(mem_allocs) <= GPU_mem_size`).
2. Hooking and replacing all `cudaMalloc()` calls in an application with `cudaMallocManaged()`, i.e., transparently forcing the use of CUDA's Unified Memory API does not affect correctness and only leads to a ~1% slowdown.
3. If we apply (2), constraint (1) no longer holds for an application written using `cudaMalloc()`.
4. When we oversubscribe GPU memory (`Σ(mem_allocs) > GPU_mem_size`), we must take care to avoid thrashing when the working sets of co-located apps (i.e., the data they are *actively* using) don't fit in GPU mem (`Σ(wss) > GPU_mem_size`). We use `nvshare-scheduler` to serialize work on the GPU to avoid thrashing. If we don't serialize the work, the frequent (every few ms) context switches of NVIDIA's black-box scheduler between the co-located apps will cause thrashing.
5. If we know that `Σ(wss) <= GPU_mem_size`, we can disable `nvshare-scheduler`'s anti-thrashing mode.

<a name="supported_gpus"/>

## Supported GPUs

`nvshare` relies on Unified Memory's dynamic page fault handling mechanism introduced in the Pascal microarchitecture.

It supports **any Pascal (2016) or newer Nvidia GPU**.

It has only been tested on Linux systems.

<a name="overview"/>

## Overview

<a name="components"/>

### `nvshare` components
- `nvshare-scheduler`, which is responsible for managing a single Nvidia GPU. It schedules the GPU "lock" among co-located clients that want to submit work on the GPU. It assigns exclusive access to the GPU to clients in an FCFS manner, for TQ seconds at a time.
- `libnvshare.so`, which we inject into CUDA applications through `LD_PRELOAD` and which:
   * Interposes (hooks) the application's calls to the CUDA API, converting normal memory allocation calls to their Unified Memory counterparts
   * Implements the client side of `nvshare`, which communicates with the `nvshare-scheduler` instance to gain exclusive access to the GPU each time the application wants to do computations on the GPU.
- `nvsharectl`, which is a command-line tool used to configure the status of an `nvshare-scheduler` instance.

<a name="details_scheduler"/>

### Some Details on `nvshare-scheduler`

> **IMPORTANT**: `nvshare` currently supports only one GPU per node, as `nvshare-scheduler` is hardcoded to use the Nvidia GPU with ID 0.

`nvshare-scheduler`'s job is to prevent thrashing. It assigns exclusive usage of the whole GPU and its physical memory to a single application at a time, handling requests from applications in an FCFS manner. Each app uses the GPU for at most TQ seconds. If the app is idle, it releases the GPU early. When it wants to compute something on the GPU at a later point, it again requests GPU access from the scheduler. When the scheduler gives it access to the GPU, the app gradually fetches its data to the GPU via page faults.

If the combined GPU memory usage of the co-located applications fits in the available GPU memory, they can seamlessly run in parallel.

However, when the combined memory usage exceeds the total GPU memory, `nvshare-scheduler` must serialize GPU work from different processes in order to avoid thrashing.

The anti-thrashing mode of nvshare-scheduler is enabled by default. You can configure this using `nvsharectl`. We currently have no way of automatically detecting thrashing, therefore we must toggle the scheduler on/off manually.

<a name="single_oversub"/>

### Memory Oversubscription For a Single Process

`nvshare` allows each co-located process to use the whole physical GPU memory. By default, it doesn't allow a single process to allocate more memory than the GPU can hold, as this can lead to internal thrashing for the process, regardless of the existence of other processes on the same GPU.

If you get a `CUDA_ERROR_OUT_OF_MEMORY` it means that your application tried to allocate more memory than the total capacity of the GPU.

You can set the `NVSHARE_ENABLE_SINGLE_OVERSUB=1` environment variable to enable a single process to use more memory than is physically available on the GPU. This can lead to degraded performance.

<a name="scheduler_tq"/>

### The Scheduler's Time Quantum (TQ)

> The TQ has effect only when the scheduler's anti-thrashing mode is enabled.

A larger time quantum sacrifices interactivity (responsiveness) in favor of throughput (utilization).

The scheduler's TQ dictates the amount of time the scheduler assigns the GPU to a client for. A larger time quantum sacrifices interactivity (latency) in favor of throughput (utilization) and vice-versa.

You shouldn't set the time quantum to a very small value (< 10), as the time spent fetching the pages of the app that just acquired the GPU lock takes a few seconds, so it won't have enough time to do actual computations.

To minimize the overall completion time of a set of sequential (batch) jobs, you can set the TQ to very large value.

**Without** `nvshare`, you would run out of memory and have to run one job after another.

**With** `nvshare`:

- Only the GPU portions of the jobs will run serialized on the GPU, the CPU parts will run in parallel
- Each application will hold the GPU only while it runs code on it (due to the early release mechanism)

<a name="further_reading"/>

## Further Reading
`nvshare` is based on my diploma thesis titled "Dynamic memory management for the efficient utilization of graphics processing units in interactive machine learning development", published in July 2021 and available at http://dx.doi.org/10.26240/heal.ntua.21988.

#### Thesis:
The title and first part are in Greek, but the second part is the full thesis in English. You can also find it at [`grgalex-thesis.pdf`](grgalex-thesis.pdf) in the root of this repo.

#### Presentation:
View the presentation on `nvshare`:
- [Google Slides](https://docs.google.com/presentation/d/16aIY0WLZutHHMZ9LJ0kCc78A5Inm5rh0P1FqNYzzCeg)
- [In `ODP` format](grgalex-nvshare-presentation.odp)
- [In `PDF` format](grgalex-nvshare-presentation.pdf)

<a name="deploy_local"/>

## Deploy on a Local System

<a name="installation_local"/>

### Installation (Local)

#### For compatibility reasons, it is better if you [build `nvshare` from source](#build_local) for your system before installing.

1. (Optional) Download the latest release tarball from the `Releases` tab or through the command-line:

      ```bash
      wget https://github.com/grgalex/nvshare/releases/download/v0.1.1/nvshare-v0.1.1.tar.gz -O nvshare.tar.gz
      ```

2. Extract the tarball:

      ```bash
      tar -xzvf nvshare.tar.gz
      ```

3. Install `libnvshare.so` and update the dynamic linker's cache:

      ```bash
      sudo mv libnvshare.so /usr/local/lib/libnvshare.so && \
      sudo ldconfig /usr/local/lib
      ```

4. Install `nvshare-scheduler`:

      > `nvshare` uses UNIX sockets for communication and stores them under `/var/run/nvshare`, so it must run as **root**.

      ```bash
      sudo mv nvshare-scheduler /usr/local/sbin/nvshare-scheduler
      ```

5. Install `nvsharectl`:

      ```bash
      sudo mv nvsharectl /usr/local/bin/nvsharectl
      ```

6. Remove the tarball:

      ```bash
      rm nvshare.tar.gz
      ```

<a name="usage_local"/>

### Usage (Local)

1. Start the `nvshare-scheduler`:

      > It must run as `root`, so we must use `sudo`.

      The `nvshare-scheduler` executable will:
      - Create the `/var/run/nvshare` directory
      - Create the `/var/run/nvshare/scheduler.sock` UNIX socket
      - Listen for requests from `nvshare` clients.

      **Option A**: Start `nvshare-scheduler` with **normal logging**:

      ```bash
      sudo bash -c 'nvshare-scheduler'
      ```


      **Option B**: Start `nvshare-scheduler` with **debug logging**:

      ```bash
      sudo bash -c 'NVSHARE_DEBUG=1 nvshare-scheduler'
      ```

      **[TROUBLESHOOTING]**: If you get the following error:
      
      ```
      nvshare-scheduler: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.34' not found (required by nvshare-scheduler)
      ```

      Then you must [build `nvshare` from source](#build_local) for your system and re-install.

2. Launch your application with `LD_PRELOAD`:

      > We inject our custom `nvshare` logic into CUDA applications using `LD_PRELOAD`. `libnvshare` automatically detects if it's running in a CUDA application and only then communicates with `nvshare-scheduler`.

      **Option A**: Export the `LD_PRELOAD` variable:

      ```bash
      export LD_PRELOAD=libnvshare.so
      ```

      You can then launch your CUDA application as you normally would.

      **Option B**: Set the `LD_PRELOAD` environment variable for a single program:

      Prepend the `LD_PRELOAD` directive and launch your program as you normally would.

      ```bash
      LD_PRELOAD=libnvshare.so <YOUR_PROGRAM> <YOUR_ARGUMENTS>
      ```

      **Option C**: Add an entry for `libnvshare.so` in `/etc/ld.so.preload`:

      > In some cases, for example when using a Jupyter Notebook Server, it may be hard to set environment variables for Notebooks that it spawns after it is stated. You can opt to use the `ld.so.preload` file in those cases.

      ```bash
      sudo bash -c 'echo -ne "\n/usr/local/lib/libnvshare.so" >> /etc/ld.so.preload'
      ```

3. (Optional) Use `nvsharectl` to configure `nvshare-scheduler`:

      By default, `nvshare-scheduler` is on. This means that during TQ seconds, only one process runs computation on the GPU.

      ```bash
      usage: nvsharectl [options]

      A command line utility to configure the nvshare scheduler.

      -T, --set-tq=n               Set the time quantum of the scheduler to TQ seconds. Only accepts positive integers.
      -S, --anti-thrash=s          Set the desired status of the scheduler. Only accepts values "on" or "off".
      -h, --help                   Shows this help message
      ```

4. You can enable debug logs for any `nvshare`-enabled application by setting the `NVSHARE_DEBUG=1` environment variable.

<a name="test_local"/>

### Test (Local)

> If you don't want to use `docker`, you can run the tests manually by cloning the repo, going to the `tests/` directory and running the Python programs by hand, using `LD_PRELOAD=libnvshare.so`.
> The default tests below use about 10 GB GPU memory each. Use these if your GPU has at least 10 GB memory.

1. Install `docker` (https://docs.docker.com/engine/install/)
2. Start the `nvshare-scheduler`, following the instructions in the [`Usage (Local)`](#usage_local) section.
3. In a Terminal window, continuously watch the GPU status:

      ```bash
      watch nvidia-smi
      ```

4. Select your test workload from the available Docker images:

      - Variants that use 10 GB GPU memory:
         - `docker.io/grgalex/nvshare:tf-matmul-v0.1-f654c296`
         - `docker.io/grgalex/nvshare:pytorch-add-v0.1-f654c296`
      - Variants that use 2 GB GPU memory:
         - `docker.io/grgalex/nvshare:tf-matmul-small-v0.1-f654c296`
         - `docker.io/grgalex/nvshare:pytorch-add-small-v0.1-f654c296`

      ```bash
      export WORKLOAD_IMAGE=docker.io/grgalex/nvshare:tf-matmul-v0.1-f654c296
      ```

4. In a new Terminal window, start a container that runs the test workload:

      ```bash
      docker run -it --gpus all \
      --entrypoint=/usr/bin/env \
      -v /usr/local/lib/libnvshare.so:/libnvshare.so \
      -v /var/run/nvshare:/var/run/nvshare \
      ${WORKLOAD_IMAGE?} \
      bash -c "LD_PRELOAD=/libnvshare.so python /tf-matmul.py"
      ```

5. Wait for the first container to start computing on the GPU, and then:

      - Look at the `nvshare-scheduler` logs, watch the magic happen.
      - Look at the `nvidia-smi` output, interpet the memory usage according to https://forums.developer.nvidia.com/t/unified-memory-nvidia-smi-memory-usage-interpretation/177372.

5. In another Terminal window, start another container from the same image you picked in step (4):

      ```bash
      export WORKLOAD_IMAGE=docker.io/grgalex/nvshare:tf-matmul-v0.1-f654c296
      ```

      ```bash
      docker run -it --gpus all \
      --entrypoint=/usr/bin/env \
      -v /usr/local/lib/libnvshare.so:/libnvshare.so \
      -v /var/run/nvshare:/var/run/nvshare \
      ${WORKLOAD_IMAGE?} \
      bash -c "LD_PRELOAD=/libnvshare.so python /tf-matmul.py"
      ```

6. Observe the following:
      - At a given point in time, only one of the two applications is making progress
      - Cross-check the above with the `nvshare-scheduler` logs, look for the `REQ_LOCK`, `LOCK_OK`, `DROP_LOCK` messages
      - The GPU wattage is high compared to when the GPU is idle
      - Use `nvsharectl` to turn off the anti-thrashing mode of the scheduler
         - `nvsharectl -S off`
      - Now both applications are running loose at the same time, **thrashing**!

      > Depending on your GPU memory capacity, the working sets might still fit in GPU memory and no thrashing will happen. Run more containers to cause thrashing.

      - Notice the throughput and most importantly the wattage of the GPU fall, as the computation units are idle and page faults dominate.
      - Use `nvsharectl` to turn the anti-thrashing mode back on
         - `nvsharectl -S on`
      - Thrashing soon stops and applications start making progress again. The GPU wattage also rises.

7. (Optional) Re-run, adding `NVSHARE_DEBUG=1` before `LD_PRELOAD` to see the debug logs, which among other interesting things show the early-release mechanism in action.

<a name="deploy_k8s"/>

## Deploy on Kubernetes

<a name="installation_k8s"/>

### Installation (Kubernetes)

#### Requirements:
- NVIDIA's device plugin (https://github.com/NVIDIA/k8s-device-plugin)

Deploy the `nvshare` Kubernetes components:
1. `nvshare-system` namespace
2. `nvshare-system` ResourceQuotas
3. `nvshare-device-plugin` DaemonSet
4. `nvshare-scheduler` DaemonSet

      ```bash
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/nvshare-system.yaml && \
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/nvshare-system-quotas.yaml && \
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/device-plugin.yaml && \
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/scheduler.yaml
      ```

The Device Plugin runs on every GPU-enabled node in your Kubernetes cluster (currently it will fail on non-GPU nodes but that is OK) and manages a single GPU on every node. It consumes a single `nvidia.com/gpu` device and advertizes it as multiple (by default 10) `nvshare.com/gpu` devices. This means that up to 10 containers can concurrently run on the same physical GPU.

<a name="usage_k8s"/>

### Usage (Kubernetes)

<a name="usage_k8s_device"/>

#### Use an `nvshare.com/gpu` Device in Your Container

In order to use an `nvshare` virtual GPU, you need to request an 'nvshare.com/gpu' device in the `limits` section of the `resources` of your container.

> Practically, you can replace `nvidia.com/gpu` with `nvshare.com/gpu` in your container specs.

> You can optionally enable debug logs for any `nvshare`-enabled application by setting the `NVSHARE_DEBUG: "1"` environment variable. You can do this by following the instructions at https://kubernetes.io/docs/tasks/inject-data-application/define-environment-variable-container/.

To do this, add the following lines to the container’s spec:

```yaml
resources:
  limits:
    nvshare.com/gpu: 1
```

<a name="usage_k8s_conf"/>

#### (Optional) Configure an `nvshare-scheduler` instance using `nvsharectl`
> As the scheduler is a `DaemonSet`, there is one instance of `nvshare-scheduler` per node.

1. Store the Pod name of the instance you want to change in a variable:
      > You can use `kubectl get pods -n nvshare-system` to find the name.

      ```bash
      NVSHARE_SCHEDULER_POD_NAME=<pod-name>
      ```

2. Execute into the container and use `nvsharectl` to reconfigure the scheduler:

      ```bash
      kubectl exec -ti ${NVSHARE_SCHEDULER_POD_NAME?} -n nvshare-system -- nvsharectl ...
      ```

<a name="test_k8s"/>

### Test (Kubernetes)

1. Deploy the test workloads:

      > The default tests below use about 10 GB GPU memory each. Use these if your GPU has at least 10 GB memory. Alternatively, you can pick any in the `tests/manifests` directory. The `*-small` variants use less GPU memory. You can either clone the repo or copy the link to the raw file and pass it to `kubectl`.

      ```bash
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/tests/kubernetes/manifests/nvshare-tf-pod-1.yaml && \
      kubectl apply -f https://raw.githubusercontent.com/grgalex/nvshare/main/tests/kubernetes/manifests/nvshare-tf-pod-2.yaml
      ```

2. In a terminal window, watch the logs of the first Pod:

      ```bash
      kubectl logs nvshare-tf-matmul-1 -f
      ```

3. In another window, watch the logs of the second Pod:

      ```bash
      kubectl logs nvshare-tf-matmul-2 -f
      ```

4. (Optional) Find the node that the Pods are running on, watch the `nvshare-scheduler` logs from that node

5. Delete the test workloads:

      ```bash
      kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/tests/kubernetes/manifests/nvshare-tf-pod-1.yaml && \
      kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/tests/kubernetes/manifests/nvshare-tf-pod-2.yaml
      ```

<a name="uninstall_k8s"/>

### Uninstall (Kubernetes)

Delete all `nvshare` components from your cluster:

```bash
kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/scheduler.yaml
kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/device-plugin.yaml && \
kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/nvshare-system-quotas.yaml && \
kubectl delete -f https://raw.githubusercontent.com/grgalex/nvshare/main/kubernetes/manifests/nvshare-system.yaml && \
```

<a name="build_local"/>

## Build For Local Use

> These instructions assume building on a Debian-based system.

> You can use the artifacts on any machine that has `glibc` and supports the ELF binary format.

1. Install requirements:

      ```bash
      sudo apt update && \
      sudo apt install gcc make libc6-dev
      ```

2. Clone this repository:

      ```bash
      git clone https://github.com/grgalex/nvshare.git
      ```

3. Enter the source code directory and build `nvshare`:

      ```bash
      cd nvshare/src/ && make
      ```

4. Use the built `nvshare-XXXX.tar.gz` to [deploy `nvshare` locally](#deploy_local), starting from Step (2), using the new tarball name.

5. Delete the build artifacts:

      ```bash
      make clean
      ```

<a name="build_docker"/>

## Build Docker Images

1. Install `docker` (https://docs.docker.com/engine/install/)

2. Clone this repository:

      ```bash
      git clone https://github.com/grgalex/nvshare.git
      ```

3. Enter the source code directory:

      ```bash
      cd nvshare/
      ```

4. (Optional) Edit the `Makefile`, change the Image Repository.

5. Build the core Docker images:

      ```bash
      make build
      ```

6. (Optional) Push the core Docker images, and update the Kubernetes manifests under `kubernetes/manifests` to use the new images.

      ```bash
      make push
      ```

7. Build the test workload Docker images:

      ```bash
      cd tests/ && make build
      ```

8. (Optional) Push the test workload Docker images, and update the Kubernetes manifests under `tests/kubernetes/manifests` to use the new images.

      ```bash
      make push
      ```

<a name="future_improves"/>

## Future Improvements
- `nvshare` currently supports only one GPU per node, as the `nvshare-scheduler` is hardcoded to use the Nvidia GPU with ID 0. Support multiple GPUs per node/machine.
- Automatically detect thrashing, optimally toggle the `nvshare-scheduler` on/off.
- Intra-node GPU migration.
- Inter-node GPU migration.

<a name="feedbk"/>

## Feedback
- Open a Github issue on this repository for any questions/bugs/suggestions.
- If your organization is using `nvshare`, you can drop me a message/mail and I can add you to `USERS.md`.

<a name="cite"/>

## Cite this work

If you found this work useful, you can cite it in the following way:

```
Georgios Alexopoulos and Dimitris Mitropoulos. 2024. nvshare: Practical
GPU Sharing without Memory Size Constraints. In 2024 IEEE/ACM 46th
International Conference on Software Engineering: Companion Proceedings
(ICSE-Companion ’24), April 14–20, 2024,Lisbon, Portugal. ACM, New York,
NY, USA, 5 pages. https://doi.org/10.1145/3639478.3640034
```

