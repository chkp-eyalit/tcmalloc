// Copyright 2019 The TCMalloc Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tcmalloc/static_vars.h"

#include <stddef.h>

#include <atomic>
#include <new>

#include "absl/base/attributes.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/macros.h"
#include "tcmalloc/cpu_cache.h"
#include "tcmalloc/internal/logging.h"
#include "tcmalloc/internal/mincore.h"
#include "tcmalloc/malloc_extension.h"
#include "tcmalloc/pagemap.h"
#include "tcmalloc/sampler.h"
#include "tcmalloc/thread_cache.h"
#include "tcmalloc/tracking.h"

namespace tcmalloc {

// Cacheline-align our SizeMap and CPUCache.  They both have very hot arrays as
// their first member variables, and aligning them reduces the number of cache
// lines these arrays use.
//
// IF YOU ADD TO THIS LIST, ADD TO STATIC_VAR_SIZE TOO!
absl::base_internal::SpinLock pageheap_lock(
    absl::base_internal::kLinkerInitialized);
ABSL_CONST_INIT Arena Static::arena_;
SizeMap ABSL_CACHELINE_ALIGNED Static::sizemap_;
TransferCache Static::transfer_cache_[kNumClasses];
CPUCache ABSL_CACHELINE_ALIGNED Static::cpu_cache_;
PageHeapAllocator<Span> Static::span_allocator_;
PageHeapAllocator<StackTrace> Static::stacktrace_allocator_;
PageHeapAllocator<ThreadCache> Static::threadcache_allocator_;
SpanList Static::sampled_objects_;
ABSL_CONST_INIT tcmalloc_internal::StatsCounter Static::sampled_objects_size_;
PeakHeapTracker Static::peak_heap_tracker_;
PageHeapAllocator<StackTraceTable::Bucket> Static::bucket_allocator_;
ABSL_CONST_INIT std::atomic<bool> Static::inited_{false};
bool Static::cpu_cache_active_;
Static::PageAllocatorStorage Static::page_allocator_;
PageMap Static::pagemap_;
absl::base_internal::SpinLock guarded_page_lock(
    absl::base_internal::kLinkerInitialized);
GuardedPageAllocator Static::guardedpage_allocator_;

size_t Static::metadata_bytes() {
  // This is ugly and doesn't nicely account for e.g. alignment losses
  // -- I'd like to put all the above in a struct and take that
  // struct's size.  But we can't due to linking issues.
  const size_t static_var_size =
      sizeof(pageheap_lock) + sizeof(arena_) + sizeof(sizemap_) +
      sizeof(transfer_cache_) + sizeof(cpu_cache_) + sizeof(span_allocator_) +
      sizeof(stacktrace_allocator_) + sizeof(threadcache_allocator_) +
      sizeof(sampled_objects_) + sizeof(bucket_allocator_) +
      sizeof(inited_) + sizeof(cpu_cache_active_) + sizeof(page_allocator_) +
      sizeof(pagemap_) + sizeof(sampled_objects_size_) +
      sizeof(peak_heap_tracker_) + sizeof(guarded_page_lock) +
      sizeof(guardedpage_allocator_);

  const size_t allocated = arena()->bytes_allocated() +
                           AddressRegionFactory::InternalBytesAllocated();
  return allocated + static_var_size;
}

size_t Static::pagemap_residence() {
  // Determine residence of the root node of the pagemap.
  size_t total = MInCore::residence(&pagemap_, sizeof(pagemap_));
  return total;
}

ABSL_ATTRIBUTE_COLD ABSL_ATTRIBUTE_NOINLINE void Static::SlowInitIfNecessary() {
  absl::base_internal::SpinLockHolder h(&pageheap_lock);

  // double-checked locking
  if (!inited_.load(std::memory_order_acquire)) {
    tracking::Init();
    sizemap_.Init();
    span_allocator_.Init(&arena_);
    span_allocator_.New();  // Reduce cache conflicts
    span_allocator_.New();  // Reduce cache conflicts
    stacktrace_allocator_.Init(&arena_);
    bucket_allocator_.Init(&arena_);
    peak_heap_tracker_.Init();
    // Do a bit of sanitizing: make sure central_cache is aligned properly
    CHECK_CONDITION((sizeof(transfer_cache_[0]) % 64) == 0);
    for (int i = 0; i < kNumClasses; ++i) {
      transfer_cache_[i].Init(i);
    }
    new (page_allocator_.memory) PageAllocator;
    sampled_objects_.Init();
    threadcache_allocator_.Init(&arena_);
    cpu_cache_active_ = false;
    pagemap_.MapRootWithSmallPages();
    guardedpage_allocator_.Init(/*max_alloced_pages=*/64, /*total_pages=*/128);
    inited_.store(true, std::memory_order_release);
  }
}

}  // namespace tcmalloc
