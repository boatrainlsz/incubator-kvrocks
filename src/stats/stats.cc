/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "stats.h"

#include <chrono>

#include "fmt/format.h"
#include "util.h"

Stats::Stats() {
  for (int i = 0; i < STATS_METRIC_COUNT; i++) {
    struct inst_metric im;
    im.last_sample_time = 0;
    im.last_sample_count = 0;
    im.idx = 0;
    for (uint64_t &sample : im.samples) {
      sample = 0;
    }
    inst_metrics.push_back(im);
  }
}

#if defined(__APPLE__)
#include <mach/mach_init.h>
#include <mach/task.h>

int64_t Stats::GetMemoryRSS() {
  task_t task = MACH_PORT_NULL;
  struct task_basic_info t_info;
  mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
  if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS) return 0;
  task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
  return static_cast<int64_t>(t_info.resident_size);
}
#else
#include <fcntl.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "fd_util.h"

int64_t Stats::GetMemoryRSS() {
  char buf[4096];
  auto fd = UniqueFD(open(fmt::format("/proc/{}/stat", getpid()).c_str(), O_RDONLY));
  if (!fd) return 0;
  if (read(*fd, buf, sizeof(buf)) <= 0) {
    return 0;
  }
  fd.Close();

  char *start = buf;
  int count = 23;  // RSS is the 24th field in /proc/<pid>/stat
  while (start && count--) {
    start = strchr(start, ' ');
    if (start) start++;
  }
  if (!start) return 0;
  char *stop = strchr(start, ' ');
  if (!stop) return 0;
  *stop = '\0';
  int rss = std::atoi(start);
  return static_cast<int64_t>(rss * sysconf(_SC_PAGESIZE));
}
#endif

void Stats::IncrCalls(const std::string &command_name) {
  total_calls.fetch_add(1, std::memory_order_relaxed);
  commands_stats[command_name].calls.fetch_add(1, std::memory_order_relaxed);
}

void Stats::IncrLatency(uint64_t latency, const std::string &command_name) {
  commands_stats[command_name].latency.fetch_add(latency, std::memory_order_relaxed);
}

void Stats::TrackInstantaneousMetric(int metric, uint64_t current_reading) {
  uint64_t t = Util::GetTimeStampMS() - inst_metrics[metric].last_sample_time;
  uint64_t ops = current_reading - inst_metrics[metric].last_sample_count;
  uint64_t ops_sec = t > 0 ? (ops * 1000 / t) : 0;
  inst_metrics[metric].samples[inst_metrics[metric].idx] = ops_sec;
  inst_metrics[metric].idx++;
  inst_metrics[metric].idx %= STATS_METRIC_SAMPLES;
  inst_metrics[metric].last_sample_time = Util::GetTimeStampMS();
  inst_metrics[metric].last_sample_count = current_reading;
}

uint64_t Stats::GetInstantaneousMetric(int metric) {
  uint64_t sum = 0;
  for (uint64_t sample : inst_metrics[metric].samples) sum += sample;
  return sum / STATS_METRIC_SAMPLES;
}
