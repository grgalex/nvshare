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

NVSHARE_COMMIT := $(shell git rev-parse HEAD)
NVSHARE_TAG := $(shell echo $(NVSHARE_COMMIT) | cut -c 1-8)
CC = gcc
GENERAL_LDFLAGS = -Wl,-z,defs -Wl,-z,relro -Wl,-z,now -Wl,--no-undefined
LIBNVSHARE_LDFLAGS = -shared -Wl,-soname=libnvshare.so -Wl,--version-script=libnvshare-symbols.ld -Wl,--exclude-libs,ALL
LIBNVSHARE_LDLIBS = -ldl -lpthread
SCHEDULER_LDLIBS = -lpthread
CFLAGS = -O3 -Wall -Wextra -std=gnu99 -fPIC -D_FORTIFY_SOURCE=2

# Target rules
all: libnvshare.so nvshare-scheduler nvsharectl tarball

tarball: libnvshare.so nvshare-scheduler nvsharectl
	tar -czvf nvshare-$(NVSHARE_TAG).tar.gz \
	    --owner=0 \
	    --group=0 \
	    --no-same-owner \
	    libnvshare.so nvsharectl nvshare-scheduler

libnvshare.so: hook.o client.o common.o comm.o
	$(CC) $(GENERAL_LDFLAGS) $(LIBNVSHARE_LDFLAGS) $^ -o $@ $(LIBNVSHARE_LDLIBS)

nvshare-scheduler: scheduler.o common.o comm.o
	$(CC) $(CFLAGS) $(GENERAL_LDFLAGS) $^ -o $@ $(SCHEDULER_LDLIBS)

nvsharectl: cli.o common.o comm.o xopt.o
	$(CC) $(CFLAGS) $(INCLUDES) $(GENERAL_LDFLAGS) $^ -o $@

hook.o: hook.c 
	$(CC) $(CFLAGS) $(INCLUDES) -c $^ -o $@ 

client.o: client.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $^ -o $@

common.o: common.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $^ -o $@

comm.o: comm.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $^ -o $@

cli.o: cli.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $^ -o $@

xopt.o: xopt.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -vf *.o *.so nvsharectl nvshare-scheduler nvshare-$(NVSHARE_TAG).tar.gz

