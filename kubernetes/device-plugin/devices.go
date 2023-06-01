/*
 * Copyright (c) 2023 Georgios Alexopoulos
 */

package main

import (
	"log"
	"strconv"

	pluginapi "k8s.io/kubelet/pkg/apis/deviceplugin/v1beta1"
)

func generateDeviceID(uuid string, ordinal int) string {
	var ordinalStr string
	var devID string
	ordinalStr = strconv.FormatInt(int64(ordinal), 10)
	devID = uuid + "__" + ordinalStr
	return devID
}

func getDevices() []*pluginapi.Device {
	var devID string
	var devs []*pluginapi.Device
	log.Printf("Reporting the following DeviceIDs to kubelet:\n")

	for j := int(0); j < NvshareVirtualDevices; j++ {
		devID = generateDeviceID(UUID, j+1)
		log.Printf("[%d] Device ID:%s\n", j+1, devID)
		devs = append(devs, &pluginapi.Device{
			ID:     devID,
			Health: pluginapi.Healthy,
		})
	}

	return devs
}

