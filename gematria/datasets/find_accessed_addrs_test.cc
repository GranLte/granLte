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

#include "gematria/datasets/find_accessed_addrs.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/random/seed_sequences.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "gematria/llvm/asm_parser.h"
#include "gematria/llvm/llvm_architecture_support.h"
#include "gematria/testing/matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"

namespace gematria {
namespace {

using testing::ElementsAre;
using testing::Field;
using testing::IsEmpty;

uintptr_t AlignDown(uintptr_t x, size_t align) { return x - (x % align); }

class FindAccessedAddrsTest : public testing::Test {
 private:
  inline static std::unique_ptr<LlvmArchitectureSupport> llvm_arch_support_;

 protected:
  static void SetUpTestSuite() {
    llvm_arch_support_ = LlvmArchitectureSupport::X86_64();
  }

  std::string Assemble(std::string_view textual_assembly) {
    auto mc_insts_or = gematria::ParseAsmCodeFromString(
        llvm_arch_support_->target_machine(), textual_assembly,
        llvm::InlineAsm::AsmDialect::AD_Intel);
    CHECK_OK(mc_insts_or);
    const auto& mc_insts = *mc_insts_or;

    llvm::MCContext mc_context(
        llvm_arch_support_->target_machine().getTargetTriple(),
        &llvm_arch_support_->mc_asm_info(),
        &llvm_arch_support_->mc_register_info(),
        &llvm_arch_support_->mc_subtarget_info());
    const auto code_emitter =
        absl::WrapUnique(llvm_arch_support_->target().createMCCodeEmitter(
            llvm_arch_support_->mc_instr_info(), mc_context));

    llvm::SmallString<128> code;
    llvm::SmallVector<llvm::MCFixup> fixups;
    for (const auto& mc_inst : mc_insts) {
      code_emitter->encodeInstruction(mc_inst, code, fixups,
                                      llvm_arch_support_->mc_subtarget_info());
    }

    return std::string(code);
  }

  absl::StatusOr<AccessedAddrs> FindAccessedAddrsAsm(
      std::string_view textual_assembly) {
    auto code = Assemble(textual_assembly);
    auto span = absl::MakeConstSpan(
        reinterpret_cast<const uint8_t*>(code.data()), code.size());
    return FindAccessedAddrs(span);
  }
};

TEST_F(FindAccessedAddrsTest, BasicMov) {
  EXPECT_THAT(
      FindAccessedAddrsAsm("mov [0], eax"),
      IsOkAndHolds(Field(&AccessedAddrs::accessed_blocks, ElementsAre(0))));
}

TEST_F(FindAccessedAddrsTest, DISABLED_SingleAddressRandomTests) {
  absl::BitGen rng;

  // See https://www.kernel.org/doc/html/latest/x86/x86_64/mm.html for
  // a full description of the address space on Linux x86_64, which this test
  // is specific to.
  constexpr int64_t kMaxUserModeAddress = 0x0000'7fff'ffff'ffffL;

  // TODO(orodley): FindAccessedAddrs is currently quite slow, so we can't run
  // too many iterations without the test taking annoyingly long. Raise it later
  // when/if we've improved performance.
  for (int n = 0; n < 100; ++n) {
    // We only want to generate addresses within the user-mode virtual address
    // space.
    //
    // We use LogUniform so that we get a broad sampling of the address space.
    // If we used Uniform over such a large range we'd practically never get
    // addresses right towards the bottom of the range.
    auto addr = absl::LogUniform(rng, 0L, kMaxUserModeAddress);

    // mov can only take up to 32-bit immediate addresses, so we need to use a
    // temporary register.
    auto code = absl::StrFormat(R"asm(
      mov rax, %ld
      mov ebx, [rax]
    )asm",
                                addr);
    const absl::StatusOr<AccessedAddrs> result = FindAccessedAddrsAsm(code);
    ASSERT_OK(result.status());

    EXPECT_THAT(result->accessed_blocks,
                ElementsAre(AlignDown(addr, result->block_size)));
    EXPECT_GT(result->code_location, 0);
    EXPECT_LT(result->code_location, kMaxUserModeAddress);
  }
}

TEST_F(FindAccessedAddrsTest, NoMemoryAccesses) {
  EXPECT_THAT(FindAccessedAddrsAsm("mov eax, ebx"),
              IsOkAndHolds(Field(&AccessedAddrs::accessed_blocks, IsEmpty())));
}

TEST_F(FindAccessedAddrsTest, MultipleAccesses) {
  EXPECT_THAT(FindAccessedAddrsAsm(R"asm(
    mov [0x10000], eax
    mov [0x20000], eax
  )asm"),
              IsOkAndHolds(Field(&AccessedAddrs::accessed_blocks,
                                 ElementsAre(0x10000, 0x20000))));
}

TEST_F(FindAccessedAddrsTest, AccessFromRegister) {
  EXPECT_THAT(FindAccessedAddrsAsm(R"asm(
    mov [eax], eax
    mov [r11+r12], eax
  )asm"),
              IsOkAndHolds(Field(&AccessedAddrs::accessed_blocks,
                                 ElementsAre(0x10000, 0x20000))));
}

}  // namespace
}  // namespace gematria
