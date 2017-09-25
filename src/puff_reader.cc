// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "puffin/src/puff_reader.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "puffin/src/set_errors.h"

namespace puffin {

namespace {
// Reads a value from the buffer in big-endian mode.
inline uint16_t ReadByteArrayToUint16(const uint8_t* buffer) {
  return (*buffer << 8) | *(buffer + 1);
}
}  // namespace

bool BufferPuffReader::GetNext(PuffData* data, Error* error) {
  PuffData& pd = *data;
  size_t length = 0;
  if (state_ == State::kReadingLenDist) {
    if (puff_buf_in_[index_] & 0x80) {  // Reading length/distance.
      // Boundary check
      TEST_AND_RETURN_FALSE_SET_ERROR(index_ < puff_size_,
                                      Error::kInsufficientInput);
      if ((puff_buf_in_[index_] & 0x7F) < 127) {
        length = puff_buf_in_[index_] & 0x7F;
      } else {
        index_++;
        // Boundary check
        TEST_AND_RETURN_FALSE_SET_ERROR(index_ < puff_size_,
                                        Error::kInsufficientInput);
        length = puff_buf_in_[index_] + 127;
      }
      length += 3;
      index_++;
      // Boundary check
      TEST_AND_RETURN_FALSE_SET_ERROR(index_ + 1 < puff_size_,
                                      Error::kInsufficientInput);
      auto distance = ReadByteArrayToUint16(&puff_buf_in_[index_]);
      index_ += 2;

      // End of block
      if (length == 259) {
        pd.type = PuffData::Type::kEndOfBlock;
        pd.byte = distance;
        state_ = State::kReadingBlockMetadata;
        DVLOG(2) << "Read end of block";
        return true;
      }

      TEST_AND_RETURN_FALSE(length < 259);
      pd.type = PuffData::Type::kLenDist;
      pd.length = length;
      pd.distance = distance;
      DVLOG(2) << "Read length: " << length << " distance: " << distance;
      return true;
    } else {  // Reading literals.
      // Boundary check
      TEST_AND_RETURN_FALSE_SET_ERROR(index_ < puff_size_,
                                      Error::kInsufficientInput);
      if ((puff_buf_in_[index_] & 0x7F) < 127) {
        length = puff_buf_in_[index_] & 0x7F;
        index_++;
      } else {
        index_++;
        // Boundary check
        TEST_AND_RETURN_FALSE_SET_ERROR(index_ + 1 < puff_size_,
                                        Error::kInsufficientInput);
        length = ReadByteArrayToUint16(&puff_buf_in_[index_]) + 127;
        index_ += 2;
      }
      length++;
      DVLOG(2) << "Read literals length: " << length;
      // Boundary check
      TEST_AND_RETURN_FALSE_SET_ERROR(index_ + length <= puff_size_,
                                      Error::kInsufficientInput);
      pd.type = PuffData::Type::kLiterals;
      pd.length = length;
      pd.read_fn = [this, length](uint8_t* buffer, size_t count) mutable {
        TEST_AND_RETURN_FALSE(count <= length);
        memcpy(buffer, &puff_buf_in_[index_], count);
        index_ += count;
        length -= count;
        return true;
      };
      return true;
    }
  } else {  // Block metadata
    pd.type = PuffData::Type::kBlockMetadata;
    // Boundary check
    TEST_AND_RETURN_FALSE_SET_ERROR(index_ + 2 < puff_size_,
                                    Error::kInsufficientInput);
    length = ReadByteArrayToUint16(&puff_buf_in_[index_]) + 1;
    index_ += 2;
    DVLOG(2) << "Read block metadata length: " << length;
    // Boundary check
    TEST_AND_RETURN_FALSE_SET_ERROR(index_ + length <= puff_size_,
                                    Error::kInsufficientInput);

    memcpy(pd.block_metadata, &puff_buf_in_[index_], length);
    index_ += length;
    pd.length = length;
    state_ = State::kReadingLenDist;
  }
  return true;
}

size_t BufferPuffReader::BytesLeft() const {
  return puff_size_ - index_;
}

}  // namespace puffin
