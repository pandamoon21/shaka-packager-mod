// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/filters/h265_byte_to_unit_stream_converter.h"

#include <limits>

#include "packager/base/logging.h"
#include "packager/media/base/buffer_writer.h"
#include "packager/media/base/rcheck.h"
#include "packager/media/filters/h265_parser.h"

namespace edash_packager {
namespace media {

H265ByteToUnitStreamConverter::H265ByteToUnitStreamConverter()
    : H26xByteToUnitStreamConverter(NaluReader::kH265) {}
H265ByteToUnitStreamConverter::~H265ByteToUnitStreamConverter() {}

bool H265ByteToUnitStreamConverter::GetDecoderConfigurationRecord(
    std::vector<uint8_t>* decoder_config) const {
  DCHECK(decoder_config);

  if (last_sps_.empty() || last_pps_.empty() || last_vps_.empty()) {
    // No data available to construct HEVCDecoderConfigurationRecord.
    return false;
  }

  // We need to parse the SPS to get the data to add to the record.
  int id;
  Nalu nalu;
  H265Parser parser;
  RCHECK(nalu.InitializeFromH265(last_sps_.data(), last_sps_.size()));
  RCHECK(parser.ParseSps(nalu, &id) == H265Parser::kOk);
  const H265Sps* sps = parser.GetSps(id);

  // Construct an HEVCDecoderConfigurationRecord containing a single SPS, PPS,
  // and VPS NALU. Please refer to ISO/IEC 14496-15 for format specifics.
  BufferWriter buffer(last_sps_.size() + last_pps_.size() + last_vps_.size() +
                      100);
  buffer.AppendInt(static_cast<uint8_t>(1) /* version */);
  // (1) general_profile_space, general_tier_flag, general_profile_idc
  // (4) general_profile_compatibility_flags
  // (6) general_constraint_indicator_flags
  // (1) general_level_idc
  // Skip Nalu header (2) and the first byte of the SPS to get the
  // profile_tier_level.
  buffer.AppendArray(&last_sps_[2+1], 12);
  // min_spacial_segmentation_idc = 0 (Unknown)
  // TODO(modmaker): Parse vui_parameters and update this.
  buffer.AppendInt(static_cast<uint16_t>(0xf000));
  buffer.AppendInt(static_cast<uint8_t>(0xfc) /* parallelismType = 0 */);
  buffer.AppendInt(static_cast<uint8_t>(0xfc | sps->chroma_format_idc));
  buffer.AppendInt(static_cast<uint8_t>(0xf8 | sps->bit_depth_luma_minus8));
  buffer.AppendInt(static_cast<uint8_t>(0xf8 | sps->bit_depth_chroma_minus8));
  buffer.AppendInt(static_cast<uint16_t>(0) /* avgFrameRate */);
  // Following flags are 0:
  //   constantFrameRate
  //   numTemporalLayers
  //   temporalIdNested
  buffer.AppendInt(static_cast<uint8_t>(kUnitStreamNaluLengthSize - 1));
  buffer.AppendInt(static_cast<uint8_t>(3) /* numOfArrays */);

  // SPS
  const uint8_t kArrayCompleteness = 0x80;
  buffer.AppendInt(static_cast<uint8_t>(kArrayCompleteness | Nalu::H265_SPS));
  buffer.AppendInt(static_cast<uint16_t>(1) /* numNalus */);
  buffer.AppendInt(static_cast<uint16_t>(last_sps_.size()));
  buffer.AppendVector(last_sps_);

  // PPS
  buffer.AppendInt(static_cast<uint8_t>(kArrayCompleteness | Nalu::H265_PPS));
  buffer.AppendInt(static_cast<uint16_t>(1) /* numNalus */);
  buffer.AppendInt(static_cast<uint16_t>(last_pps_.size()));
  buffer.AppendVector(last_pps_);

  // VPS
  buffer.AppendInt(static_cast<uint8_t>(kArrayCompleteness | Nalu::H265_VPS));
  buffer.AppendInt(static_cast<uint16_t>(1) /* numNalus */);
  buffer.AppendInt(static_cast<uint16_t>(last_vps_.size()));
  buffer.AppendVector(last_vps_);

  buffer.SwapBuffer(decoder_config);
  return true;
}

bool H265ByteToUnitStreamConverter::ProcessNalu(const Nalu& nalu) {
  DCHECK(nalu.data());

  // Skip the start code, but keep the 2-byte NALU header.
  const uint8_t* nalu_ptr = nalu.data();
  const uint64_t nalu_size = nalu.payload_size() + nalu.header_size();

  switch (nalu.type()) {
    case Nalu::H265_SPS:
      // Grab SPS NALU.
      last_sps_.assign(nalu_ptr, nalu_ptr + nalu_size);
      return true;
    case Nalu::H265_PPS:
      // Grab PPS NALU.
      last_pps_.assign(nalu_ptr, nalu_ptr + nalu_size);
      return true;
    case Nalu::H265_VPS:
      // Grab VPS NALU.
      last_vps_.assign(nalu_ptr, nalu_ptr + nalu_size);
      return true;
    case Nalu::H265_AUD:
      // Ignore AUD NALU.
      return true;
    default:
      // Have the base class handle other NALU types.
      return false;
  }
}

}  // namespace media
}  // namespace edash_packager