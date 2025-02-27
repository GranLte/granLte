// Copyright 2023 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_GEMATRIA_GEMATRIA_DATASETS_FIND_ACCESSED_ADDRS_H_
#define THIRD_PARTY_GEMATRIA_GEMATRIA_DATASETS_FIND_ACCESSED_ADDRS_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace gematria {

struct AccessedAddrs {
  uintptr_t code_location;
  size_t block_size;
  std::vector<uintptr_t> accessed_blocks;
};

// Given a basic block of code, attempt to determine what addresses that code
// accesses. This is done by executing the code in a new process, so the code
// must match the architecture on which this function is executed.
absl::StatusOr<AccessedAddrs> FindAccessedAddrs(
    absl::Span<const uint8_t> basic_block);

}  // namespace gematria

#endif  // THIRD_PARTY_GEMATRIA_GEMATRIA_DATASETS_FIND_ACCESSED_ADDRS_H_
