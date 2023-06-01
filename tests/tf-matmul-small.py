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
#
# Heavily inspired by: https://stackoverflow.com/a/41810634

import os
import sys
import tensorflow as tf
import time
from tensorflow.python.framework.ops import disable_eager_execution

import subprocess
import sys

try:
    from tqdm import tqdm
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", 'tqdm'])
finally:
    from tqdm import tqdm

disable_eager_execution()

start_time = time.time()
n = 10000
dtype = tf.float32
with tf.device("/gpu:0"):
    matrix1 = tf.Variable(tf.ones((n, n), dtype=dtype))
    matrix2 = tf.Variable(tf.ones((n, n), dtype=dtype))
    product = tf.matmul(matrix1, matrix2)

# avoid optimizing away redundant nodes
config = tf.compat.v1.ConfigProto(graph_options=tf.compat.v1.GraphOptions(optimizer_options=tf.compat.v1.OptimizerOptions(opt_level=tf.compat.v1.OptimizerOptions.L0)))
sess = tf.compat.v1.Session(config=config)
sess.run(tf.compat.v1.global_variables_initializer())
for i in tqdm(range(1000)):
    sess.run(product.op)
print("PASS")
print("--- %s seconds ---" % (time.time() - start_time))

