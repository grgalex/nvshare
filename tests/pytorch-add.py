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

import torch
import subprocess
import sys
import time

try:
    from tqdm import tqdm
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", 'tqdm'])
finally:
    from tqdm import tqdm

start_time = time.time()
n = 28000
device = torch.cuda.current_device()
x = torch.ones([n, n], dtype=torch.float32).to(device)
y = torch.ones([n, n], dtype=torch.float32).to(device)
for i in tqdm(range(4000)):
    z = torch.add(x, y)
torch.cuda.synchronize() #Ensure computations are finished
print("PASS")
print("--- %s seconds ---" % (time.time() - start_time))

