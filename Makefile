# Copyright (c) 2023 Georgios Alexopoulos
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# We abuse the image/tag semantics.
# We use a single image name: "nvshare" and incorporate the component name
# into the tag.

# You can change IMAGE to point to your own Repository.
IMAGE := nvshare
NVSHARE_COMMIT := $(shell git rev-parse HEAD)
NVSHARE_TAG := $(shell echo $(NVSHARE_COMMIT) | cut -c 1-8)

LIBNVSHARE_TAG := libnvshare-$(NVSHARE_TAG)
SCHEDULER_TAG := nvshare-scheduler-$(NVSHARE_TAG)
DEVICE_PLUGIN_TAG := nvshare-device-plugin-$(NVSHARE_TAG)

all: build push

build: build-libnvshare build-scheduler build-device-plugin

build-libnvshare:
	docker build --pull -f Dockerfile.libnvshare -t $(IMAGE):$(LIBNVSHARE_TAG) .

build-scheduler:
	docker build --pull -f Dockerfile.scheduler -t $(IMAGE):$(SCHEDULER_TAG) .

build-device-plugin:
	docker build --pull -f Dockerfile.device_plugin -t $(IMAGE):$(DEVICE_PLUGIN_TAG) .

push: push-libnvshare push-scheduler push-device-plugin

push-libnvshare:
	docker push "$(IMAGE):$(LIBNVSHARE_TAG)"

push-scheduler:
	docker push "$(IMAGE):$(SCHEDULER_TAG)"

push-device-plugin:
	docker push "$(IMAGE):$(DEVICE_PLUGIN_TAG)"

.PHONY: all
.PHONY: build build-libnvshare build-scheduler build-device-plugin
.PHONY: push push-libnvshare push-scheduler push-device-plugin

