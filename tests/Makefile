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

TF_IMAGE_TAG := tf-matmul-$(NVSHARE_TAG)
# Variant with smaller matrix sizes.
TF_SMALL_IMAGE_TAG := tf-matmul-small-$(NVSHARE_TAG)
PYTORCH_IMAGE_TAG := pytorch-add-$(NVSHARE_TAG)
# Variant with smaller matrix sizes.
PYTORCH_SMALL_IMAGE_TAG := pytorch-add-small-$(NVSHARE_TAG)

DOCKERFILE_DIR := ./dockerfiles

all: build push


build: build-tf build-tf-small build-pytorch build-pytorch-small
build-tf:
	docker build --pull -t $(IMAGE):$(TF_IMAGE_TAG) \
	-f $(DOCKERFILE_DIR)/Dockerfile.tsf .
build-tf-small:
	docker build --pull -t $(IMAGE):$(TF_SMALL_IMAGE_TAG) \
	-f $(DOCKERFILE_DIR)/Dockerfile.tsf.small .
build-pytorch:
	docker build --pull -t $(IMAGE):$(PYTORCH_IMAGE_TAG) \
	-f $(DOCKERFILE_DIR)/Dockerfile.pytorch .
build-pytorch-small:
	docker build --pull -t $(IMAGE):$(PYTORCH_SMALL_IMAGE_TAG) \
	-f $(DOCKERFILE_DIR)/Dockerfile.pytorch.small .


push: push-tf push-tf-small push-pytorch push-pytorch-small
push-tf:
	docker push $(IMAGE):$(TF_IMAGE_TAG)
push-tf-small:
	docker push $(IMAGE):$(TF_SMALL_IMAGE_TAG)
push-pytorch:
	docker push $(IMAGE):$(PYTORCH_IMAGE_TAG)
push-pytorch-small:
	docker push $(IMAGE):$(PYTORCH_SMALL_IMAGE_TAG)


.PHONY: all build push
.PHONY: build-tf build-tf-small build-pytorch build-pytorch-small
.PHONY: push-tf push-tf-small push-pytorch push-pytorch-small

