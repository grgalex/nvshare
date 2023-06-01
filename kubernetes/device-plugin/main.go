/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (c) 2023, Georgios Alexopoulos
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package main

import (
	"strconv"
	"syscall"
	"log"
	"os"

	"github.com/fsnotify/fsnotify"
	pluginapi "k8s.io/kubelet/pkg/apis/deviceplugin/v1beta1"
)

const (
	LibNvshareHostPath               = "/var/run/nvshare/libnvshare.so"
	LibNvshareContainerPath          = "/usr/lib/nvshare/libnvshare.so"
	SocketHostPath                   = "/var/run/nvshare/scheduler.sock"
	SocketContainerPath              = "/var/run/nvshare/scheduler.sock"
	NvshareVirtualDevicesEnvVar      = "NVSHARE_VIRTUAL_DEVICES"
	NvidiaDevicesEnvVar              = "NVIDIA_VISIBLE_DEVICES"
	NvidiaExposeMountDir             = "/var/run/nvidia-container-devices"
	NvidiaExposeMountHostPath        = "/dev/null"
)

var UUID string
var NvshareVirtualDevices int
var nvidiaRuntimeUseMounts bool

func main() {
	var exists bool
	var NumVirtualDevicesEnv string
	var err error
	var devicePlugin *NvshareDevicePlugin


	log.SetOutput(os.Stderr)

	/*
	 * Read the underlying GPU UUID from the NVIDIA_VISIBLE_DEVICES environment
	 * variable. Nvshare device plugin's Pod requests 1 `nvidia.com/gpu` in order
	 * to isolate it from the rest of the cluster and manage it, exposing it
	 * as multiple `nvshare.com/gpu` devices.
	 *
	 * Pods (soon to be Nvshare clients) that request an Nvshare GPU device still
	 * need to have access to the real GPU. As such, we must set the same env
	 * variable `NVIDIA_VISIBLE_DEVICES` in the containers of the Pods that
	 * request Nvshare GPUs to the same UUID as NVIDIA's device plugin set it for
	 * us here.
	 *
	 * The container runtime reads the value of this env variable and exposes
	 * the GPU device into a container.
	 */
	nvidiaRuntimeUseMounts = false
	UUID, exists = os.LookupEnv(NvidiaDevicesEnvVar)
	if exists == false {
		log.Printf("%s is not set, exiting", NvidiaDevicesEnvVar)
		os.Exit(1)
	}

	/*
	 * Find out how many virtual GPUs we must advertize
	 */
	NumVirtualDevicesEnv, exists = os.LookupEnv(NvshareVirtualDevicesEnvVar)
	if exists == false {
		log.Printf("%s is not set, exiting", NvshareVirtualDevicesEnvVar)
		os.Exit(1)
	}
	NvshareVirtualDevices, err = strconv.Atoi(NumVirtualDevicesEnv)
	if err != nil {
		log.Printf("Failed to parse nvshare devices per GPU")
		log.Fatal(err)
	}
	if NvshareVirtualDevices <= 0 {
		log.Printf("Parsed nvshare virtual devices per GPU is not a positive integer, exiting")
		os.Exit(1)
	}

	/*
	 * Device expose mode is through Volume Mounts, NVIDIA_VISIBLE_DEVICES
	 * has a symbolic value of "/var/run/nvidia-container-devices" and
	 * UUIDs are passed through volume mounts in that directory
	 */
	if UUID == NvidiaExposeMountDir {
		log.Printf("Device Exposure method of NVIDIA device plugin is Volume Mounts, following the same strategy for Nvshare device plugin")
		f, err := os.Open(NvidiaExposeMountDir)
		if err != nil {
			log.Printf("Failed to open %s", NvidiaExposeMountDir)
			log.Fatal(err)
		}
		// Read all filenames in the directory
		nvFiles, err := f.Readdirnames(0)
		if (len(nvFiles) != 1) || (err != nil) {
			log.Printf("Error when reading UUID from %s directory:%s", NvidiaExposeMountDir, err)
			if err != nil {
				log.Fatal(err)
			} else {
				os.Exit(1)
			}
		}
		UUID = nvFiles[0]
		nvidiaRuntimeUseMounts = true
	}

	log.Printf("Read UUID = %s", UUID)

	log.Println("Starting FS watcher.")
	watcher, err := newFSWatcher(pluginapi.DevicePluginPath)
	if err != nil {
		log.Fatal("Failed to create FS watcher:", err)
	}
	defer watcher.Close()

	log.Println("Starting OS watcher.")
	sigs := newOSWatcher(syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT)

restart:
	/* If we are restarting, stop any running plugin before recreating it */
	devicePlugin.Stop()

	devicePlugin = NewNvshareDevicePlugin()

	pluginStartError := make(chan struct{})

	/*
	 * Start the gRPC server for the device plugin and connect it with
	 * the kubelet.
	 */
	err = devicePlugin.Start()
	if err != nil {
		log.Println("devicePlugin.Start() FAILED. Could not contact Kubelet, retrying. Did you enable the device plugin feature gate?")
		close(pluginStartError)
		goto events
	}

events:
	for {
		select {
		case <-pluginStartError:
			goto restart

		case event := <-watcher.Events:
			if (event.Name == pluginapi.KubeletSocket) && (event.Op&fsnotify.Create == fsnotify.Create) {
				log.Printf("inotify: %s created, restarting", pluginapi.KubeletSocket)
				goto restart
			}

		case err := <-watcher.Errors:
			log.Printf("inotify: %s", err)

		case s := <-sigs:
			switch s {
			case syscall.SIGHUP:
				log.Println("Received SIGHUP, restarting.")
				goto restart
			default:
				log.Printf("Received signal \"%v\", shutting down.", s)
				devicePlugin.Stop()
				break events
			}
		}
	}
	return
}

