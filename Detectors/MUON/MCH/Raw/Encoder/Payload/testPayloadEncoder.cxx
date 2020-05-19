// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

///
/// @author  Laurent Aphecetche
///
/// In those tests we are mainly concerned about testinng
/// whether the payloads are actually properly simulated.
///
#define BOOST_TEST_MODULE Test MCHRaw Encoder
#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>
#include "MCHRawEncoderPayload/PayloadEncoder.h"
#include "MCHRawCommon/DataFormats.h"
#include "MCHRawEncoderPayload/DataBlock.h"
#include <fmt/printf.h>
#include <boost/mpl/list.hpp>
#include "CruBufferCreator.h"
#include "EncoderImplHelper.h"

using namespace o2::mch::raw;

template <typename FORMAT>
std::unique_ptr<PayloadEncoder> defaultEncoder()
{
  return createPayloadEncoder<FORMAT, SampleMode, true>();
}

typedef boost::mpl::list<BareFormat, UserLogicFormat> testTypes;

BOOST_AUTO_TEST_SUITE(o2_mch_raw)

BOOST_AUTO_TEST_SUITE(encoder)

BOOST_AUTO_TEST_CASE_TEMPLATE(StartHBFrameBunchCrossingMustBe12Bits, T, testTypes)
{
  auto encoder = defaultEncoder<T>();
  BOOST_CHECK_THROW(encoder->startHeartbeatFrame(0, 1 << 12), std::invalid_argument);
  BOOST_CHECK_NO_THROW(encoder->startHeartbeatFrame(0, 0xFFF));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(EmptyEncoderHasEmptyBufferIfPhaseIsZero, T, testTypes)
{
  srand(time(nullptr));
  auto encoder = defaultEncoder<T>();
  encoder->startHeartbeatFrame(12345, 123);
  std::vector<std::byte> buffer;
  encoder->moveToBuffer(buffer);
  BOOST_CHECK_EQUAL(buffer.size(), 0);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(EmptyEncodeIsNotNecessarilyEmptyDependingOnPhase, T, testTypes)
{
  srand(time(nullptr));
  auto encoder = createPayloadEncoder<T, SampleMode, false>();
  encoder->startHeartbeatFrame(12345, 123);
  std::vector<std::byte> buffer;
  encoder->moveToBuffer(buffer);
  BOOST_CHECK_GE(buffer.size(), 0);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(MultipleOrbitsWithNoDataIsAnEmptyBufferIfPhaseIsZero, T, testTypes)
{
  srand(time(nullptr));
  auto encoder = defaultEncoder<T>();
  encoder->startHeartbeatFrame(12345, 123);
  encoder->startHeartbeatFrame(12345, 125);
  encoder->startHeartbeatFrame(12345, 312);
  std::vector<std::byte> buffer;
  encoder->moveToBuffer(buffer);
  BOOST_CHECK_EQUAL(buffer.size(), 0);
}

int estimateUserLogicSize(int nofDS, int maxNofChPerDS)
{
  // counting first in number of 10 bits

  size_t sync = 5;
  size_t sampaHeaders = nofDS * maxNofChPerDS * 5;
  size_t sampaData = nofDS * maxNofChPerDS * 4;

  size_t n10 = sync + sampaHeaders + sampaData;
  while (n10 % 5) {
    n10++;
  }

  return (n10 / 5) * (64 / 8) + sizeof(DataBlockHeader);
}

int estimateBareSize(int nofDS, int maxNofChPerGBT)
{
  size_t headerSize = 2; // equivalent to 2 64-bits words
  size_t nbits = nofDS * 50 + maxNofChPerGBT * 90;
  size_t n128bitwords = nbits / 2;
  size_t n64bitwords = n128bitwords * 2;
  return 8 * (n64bitwords + headerSize); // size in bytes
}

template <typename FORMAT>
int estimateSize();

template <>
int estimateSize<BareFormat>()
{
  return estimateBareSize(1, 3) +
         estimateBareSize(1, 4) +
         estimateBareSize(1, 6);
}

template <>
int estimateSize<UserLogicFormat>()
{
  return estimateUserLogicSize(1, 3) +
         estimateUserLogicSize(1, 4) +
         estimateUserLogicSize(1, 6) +
         2 * 8; // 8 bytes per FEE (to ensure the payload size is a multiple of 128 bits = 1 GBT word)
}

BOOST_AUTO_TEST_CASE_TEMPLATE(CheckNumberOfPayloadHeaders, T, testTypes)
{
  auto buffer = test::CruBufferCreator<T, ChargeSumMode>::makeBuffer();
  int nheaders = o2::mch::raw::countHeaders(buffer);
  BOOST_CHECK_EQUAL(nheaders, 3);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(CheckSize, T, testTypes)
{
  auto buffer = test::CruBufferCreator<T, ChargeSumMode>::makeBuffer();
  size_t expectedSize = estimateSize<T>();
  BOOST_CHECK_EQUAL(buffer.size(), expectedSize);
}

std::string asBinary(uint64_t value)
{
  std::string s;
  uint64_t one = 1;
  int space{0};
  for (auto i = 63; i >= 0; i--) {
    if (value & (one << i)) {
      s += "1";
    } else {
      s += "0";
    }
    if (i % 4 == 0 && i) {
      s += " ";
    }
  }
  return s;
}

std::string binaryRule(bool top, std::vector<int> stops)
{
  std::string s;
  for (auto i = 63; i >= 0; i--) {
    if (std::find(stops.begin(), stops.end(), i) != stops.end()) {
      s += fmt::format("{}", (top ? i / 10 : i % 10));
    } else {
      s += " ";
    }
    if (i % 4 == 0) {
      s += " ";
    }
  }
  return s;
}

void dump(uint64_t value, std::vector<int> stops = {63, 47, 31, 15, 0})
{
  std::cout << asBinary(value) << "\n";
  std::cout << binaryRule(true, stops) << "\n";
  std::cout << binaryRule(false, stops) << "\n";
}

BOOST_AUTO_TEST_CASE(Prefix)
{
  std::vector<uint10_t> b10;
  impl::append(b10, sampaSyncWord);
  std::vector<uint64_t> b64;
  impl::b10to64(b10, b64, 24);
  auto w = b64[0];
  BOOST_CHECK_EQUAL(asBinary(w), "0000 0000 0110 0001 0101 0101 0101 0101 0100 0000 1111 0000 0000 0001 0001 0011");
}

BOOST_AUTO_TEST_CASE(Binary)
{
  dump(static_cast<uint64_t>(7) << 50, {50, 51, 52, 0});
}

BOOST_AUTO_TEST_SUITE_END()
BOOST_AUTO_TEST_SUITE_END()