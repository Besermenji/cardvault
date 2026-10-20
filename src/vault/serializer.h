// VaultData <-> bytes (compact little-endian TLV).
//
//   u8 version | u32 next_id | u16 entry_count | entries...
//   entry: u32 id | u8 type | u32 created | u32 updated | u16 title_len | title |
//          u8 field_count | fields...
//   field: u8 key | u16 len | bytes
#pragma once

#include "vault/format.h"
#include "vault/model.h"

namespace cv {

constexpr uint8_t kPayloadVersion = 1;

VaultError serialize(const VaultData& data, SBytes& out);
VaultError deserialize(const uint8_t* in, size_t len, VaultData& out);

}  // namespace cv
