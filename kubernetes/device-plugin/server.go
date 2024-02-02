/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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
	"path"
	"path/filepath"
	"time"
	"fmt"
	"log"
	"net"
	"os"
	"strings"

	"golang.org/x/net/context"
	"google.golang.org/grpc"
	pluginapi "k8s.io/kubelet/pkg/apis/deviceplugin/v1beta1"
)

const (
	resourceName = "nvshare.com/gpu"
	serverSock   = pluginapi.DevicePluginPath + "nvshare-device-plugin.sock"
)



type NvshareDevicePlugin struct {
	devs   []*pluginapi.Device
	socket string

	stop   chan interface{}
	health chan *pluginapi.Device

	server *grpc.Server
}

func NewNvshareDevicePlugin() *NvshareDevicePlugin {
	socketId:= os.Getenv("NVSHARE_SOCK_ID")
	if len(socketId) == 0 {
		socketId = "0"
	}

	serverSockNew:= strings.Split(serverSock, ".sock")[0]+socketId+".sock"
	return &NvshareDevicePlugin{
		devs:   getDevices(),
		socket: serverSockNew,

		stop:   make(chan interface{}),
		health: make(chan *pluginapi.Device),
	}
}

func (m *NvshareDevicePlugin) initialize() {
	m.server = grpc.NewServer([]grpc.ServerOption{}...)
	m.health = make(chan *pluginapi.Device)
	m.stop = make(chan interface{})
}

func (m *NvshareDevicePlugin) cleanup() {
	close(m.stop)
	m.server = nil
	m.health = nil
	m.stop = nil
}

/*
 * Starts the gRPC server, registers the device plugin with the Kubelet.
 */
func (m *NvshareDevicePlugin) Start() error {
	m.initialize()

	socketId:= os.Getenv("NVSHARE_SOCK_ID")
	if len(socketId) == 0 {
		socketId = "0"
	}
	resourceNameNew:= resourceName+socketId

	err := m.Serve()
	if err != nil {
		log.Printf("Could not start device plugin for '%s': %s", resourceNameNew, err)
		m.cleanup()
		return err
	}
	log.Printf("Starting to serve '%s' on %s", resourceNameNew, m.socket)

	err = m.Register()
	if err != nil {
		log.Printf("Could not register device plugin: %s", err)
		m.Stop()
		return err
	}
	log.Printf("Registered device plugin for '%s' with Kubelet", resourceNameNew)

	return nil
}

/* Stop the gRPC server and clean up the UNIX socket file */
func (m *NvshareDevicePlugin) Stop() error {

	socketId:= os.Getenv("NVSHARE_SOCK_ID")
	if len(socketId) == 0 {
		socketId = "0"
	}
	resourceNameNew:= resourceName+socketId

	if (m == nil) || (m.server == nil) {
		return nil
	}
	log.Printf("Stopping to serve '%s' on %s\n", resourceNameNew, m.socket)
	m.server.Stop()
	err := os.Remove(m.socket)
	if (err != nil) && (!os.IsNotExist(err)) {
		return err
	}
	m.cleanup()
	return nil
}

/* Starts the gRPC server which serves incoming requests from kubelet */
func (m *NvshareDevicePlugin) Serve() error {

	socketId:= os.Getenv("NVSHARE_SOCK_ID")
	if len(socketId) == 0 {
		socketId = "0"
	}
	resourceNameNew:= resourceName+socketId

	os.Remove(m.socket)
	sock, err := net.Listen("unix", m.socket)
	if err != nil {
		return err
	}

	pluginapi.RegisterDevicePluginServer(m.server, m)

	go func() {
		lastCrashTime := time.Now()
		restartCount := 0
		for {
			log.Printf("Starting gRPC server for '%s'", resourceNameNew)
			err := m.server.Serve(sock)
			if err == nil {
				break
			}

			log.Printf("GRPC server for '%s' crashed with error: %v",
			resourceNameNew, err)

			if restartCount > 5 {
				log.Fatalf("GRPC server for '%s' has repeatedly crashed recently. Quitting", resourceNameNew)
			}
			timeSinceLastCrash := time.Since(lastCrashTime).Seconds()
			lastCrashTime = time.Now()
			if timeSinceLastCrash > 3600 {
				restartCount = 1
			} else {
				restartCount++
			}
		}
	}()

	conn, err := m.dial(m.socket, 5*time.Second)
	if err != nil {
		return err
	}
	conn.Close()

	return nil
}

/* Registers the device plugin for resourceName with kubelet */
func (m *NvshareDevicePlugin) Register() error {

	socketId:= os.Getenv("NVSHARE_SOCK_ID")
	if len(socketId) == 0 {
		socketId = "0"
	}
	resourceNameNew := resourceName+socketId


	conn, err := m.dial(pluginapi.KubeletSocket, 5*time.Second)
	if err != nil {
		return err
	}
	defer conn.Close()

	client := pluginapi.NewRegistrationClient(conn)
	reqt := &pluginapi.RegisterRequest{
		Version:      pluginapi.Version,
		Endpoint:     path.Base(m.socket),
		ResourceName: resourceNameNew,
		Options: &pluginapi.DevicePluginOptions{
			GetPreferredAllocationAvailable: false,
		},
	}

	_, err = client.Register(context.Background(), reqt)
	if err != nil {
		return err
	}
	return nil
}


func (m *NvshareDevicePlugin) GetDevicePluginOptions(context.Context, *pluginapi.Empty) (*pluginapi.DevicePluginOptions, error) {
	options := &pluginapi.DevicePluginOptions{
		PreStartRequired:                false,
		GetPreferredAllocationAvailable: false,
	}
	return options, nil
}

/*
 * Reports available devices to kubelet and (theoretically) updates that list
 * according to their health status.
 *
 * We don't monitor health for Nvshare devices at the moment, we consider them
 * all to be healthy.
 *
 * If the underlying GPU goes unhealthy, NVIDIA's device
 * plugin will detect it and fail the (Nvshare device plugin) Pod.
 *
 * For device health handling see also the official device plugin proposal:
 * https://github.com/kubernetes/community/blob/c4466d9fbfa6645410083e37560810a9aa000267/contributors/design-proposals/resource-management/device-plugin.md#healthcheck-and-failure-recovery
 */
func (m *NvshareDevicePlugin) ListAndWatch(e *pluginapi.Empty, s pluginapi.DevicePlugin_ListAndWatchServer) error {
	s.Send(&pluginapi.ListAndWatchResponse{Devices: m.devs})
	log.Printf("Sent ListAndWatchResponse with DeviceIDs")
	for {
		select {
		case <-m.stop:
			return nil
		}
	}
}

/*
 * Kubelet calls this method when it wants to run containers in a Pod that
 * has requested an Nvshare GPU.
 */
func (m *NvshareDevicePlugin) Allocate(ctx context.Context, reqs *pluginapi.AllocateRequest) (*pluginapi.AllocateResponse, error) {
	log.SetOutput(os.Stderr)
 
	  socketId:= os.Getenv("NVSHARE_SOCK_ID")
	  if len(socketId) == 0 {
		  socketId = "0"
	  }

	  resourceNameNew := resourceName+socketId
 
	responses := pluginapi.AllocateResponse{}
	for _, req := range reqs.ContainerRequests {
		for _, id := range req.DevicesIDs {
			log.Printf("Received Allocate request for %s", id)
			if !m.deviceExists(id) {
				return nil, fmt.Errorf("invalid allocation request for '%s' - unknown device: %s", resourceNameNew, id)
			}
		}

		response := pluginapi.ContainerAllocateResponse{}

		var envsMap map[string]string
		envsMap = make(map[string]string)
		envsMap["LD_PRELOAD"] = LibNvshareContainerPath
		if nvidiaRuntimeUseMounts == false {
			envsMap[NvidiaDevicesEnvVar] = UUID
		} else {
			envsMap[NvidiaDevicesEnvVar] = NvidiaExposeMountDir
		}

		response.Envs = envsMap

		/* Add libnvshare to the Mounts section of the ContainerResponse */
		var mounts []*pluginapi.Mount
		/* Mount libnvshare */
		mount := &pluginapi.Mount{
			HostPath:      LibNvshareHostPath,
			ContainerPath: LibNvshareContainerPath,
			ReadOnly:      true,
		}
		mounts = append(mounts, mount)
  
		  SocketHostPathNew:= strings.Split(SocketHostPath, ".sock")[0]+socketId+".sock"
		  SocketContainerPathNew:= strings.Split(SocketContainerPath, ".sock")[0]+socketId+".sock"
  
		/* Mount scheduler socket */
		mount = &pluginapi.Mount{
			HostPath:      SocketHostPathNew,
			ContainerPath: SocketContainerPathNew,
			ReadOnly:      true,
		}
		mounts = append(mounts, mount)
		/*
		 * If the method for requesting GPUs from the underlying NVIDIA
		 * container runtime is through Volume Mounts, set symbolic /dev/null
		 * mount for GPU exposure
		 */
		if nvidiaRuntimeUseMounts == true {
			mount = &pluginapi.Mount{
				HostPath:      NvidiaExposeMountHostPath,
				ContainerPath: filepath.Join(NvidiaExposeMountDir, UUID),
			}
			mounts = append(mounts, mount)
		}

		response.Mounts = mounts
		responses.ContainerResponses = append(responses.ContainerResponses, &response)
	}

	return &responses, nil
}

/* GetPreferredAllocation is unimplemented for Nvshare device plugin */
func (m *NvshareDevicePlugin) GetPreferredAllocation(ctx context.Context, r *pluginapi.PreferredAllocationRequest) (*pluginapi.PreferredAllocationResponse, error) {
	response := &pluginapi.PreferredAllocationResponse{}
	return response, nil
}

/* PreStartContainer is unimplemented for Nvshare device plugin */
func (m *NvshareDevicePlugin) PreStartContainer(context.Context, *pluginapi.PreStartContainerRequest) (*pluginapi.PreStartContainerResponse, error) {
	response := &pluginapi.PreStartContainerResponse{}
	return response, nil
}

/* Establish a gRPC communication with an entity over a UNIX socket */
func (m *NvshareDevicePlugin) dial(unixSocketPath string, timeout time.Duration) (*grpc.ClientConn, error) {
	c, err := grpc.Dial(unixSocketPath, grpc.WithInsecure(), grpc.WithBlock(),
		grpc.WithTimeout(timeout),
		grpc.WithDialer(func(addr string, timeout time.Duration) (net.Conn, error) {
			return net.DialTimeout("unix", addr, timeout)
		}),
	)

	if err != nil {
		return nil, err
	}

	return c, nil
}

func (m *NvshareDevicePlugin) deviceExists(id string) bool {
	for _, d := range m.devs {
		if d.ID == id {
			return true
		}
	}
	return false
}

