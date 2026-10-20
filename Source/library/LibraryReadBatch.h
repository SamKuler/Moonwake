#pragma once

#include "ArtworkStore.h"
#include "TrackMetadataReader.h"

#include <functional>

// Synchronous batch: callbacks may run concurrently; each index is visited once.
// Readers and encoding caches are worker-owned. All workers finish before return.
void readLibraryBatch (size_t count, const std::function<bool()>& cancelled,
                       const std::function<void (size_t, TrackMetadataReader&, ArtworkStore::EncodingCache&)>& read,
                       const std::function<void (size_t completed, size_t index)>& progress = {});
