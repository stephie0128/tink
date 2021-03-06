// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////

#include "tink/subtle/ecdsa_sign_boringssl.h"

#include <vector>

#include "absl/strings/str_cat.h"
#include "tink/subtle/common_enums.h"
#include "tink/subtle/subtle_util_boringssl.h"
#include "tink/util/errors.h"
#include "openssl/bn.h"
#include "openssl/ec.h"
#include "openssl/ecdsa.h"
#include "openssl/evp.h"

namespace crypto {
namespace tink {
namespace subtle {

// static
util::StatusOr<std::unique_ptr<EcdsaSignBoringSsl>>
EcdsaSignBoringSsl::New(const SubtleUtilBoringSSL::EcKey& ec_key,
                        HashType hash_type) {
  // Check hash.
  auto hash_status = SubtleUtilBoringSSL::ValidateSignatureHash(hash_type);
  if (!hash_status.ok()) {
    return hash_status;
  }
  auto hash_result = SubtleUtilBoringSSL::EvpHash(hash_type);
  if (!hash_result.ok()) return hash_result.status();
  const EVP_MD* hash = hash_result.ValueOrDie();

  // Check curve.
  auto group_result(SubtleUtilBoringSSL::GetEcGroup(ec_key.curve));
  if (!group_result.ok()) return group_result.status();
  bssl::UniquePtr<EC_GROUP> group(group_result.ValueOrDie());
  bssl::UniquePtr<EC_KEY> key(EC_KEY_new());
  EC_KEY_set_group(key.get(), group.get());

  // Check key.
  auto ec_point_result =
      SubtleUtilBoringSSL::GetEcPoint(ec_key.curve, ec_key.pub_x, ec_key.pub_y);
  if (!ec_point_result.ok()) return ec_point_result.status();

  bssl::UniquePtr<EC_POINT> pub_key(ec_point_result.ValueOrDie());
  if (!EC_KEY_set_public_key(key.get(), pub_key.get())) {
    return util::Status(util::error::INVALID_ARGUMENT,
                        absl::StrCat("Invalid public key: ",
                                     SubtleUtilBoringSSL::GetErrors()));
  }

  bssl::UniquePtr<BIGNUM> priv_key(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(ec_key.priv.data()),
                ec_key.priv.size(), nullptr));
  if (!EC_KEY_set_private_key(key.get(), priv_key.get())) {
    return util::Status(util::error::INVALID_ARGUMENT,
                        absl::StrCat("Invalid private key: ",
                                     SubtleUtilBoringSSL::GetErrors()));
  }

  // Sign.
  std::unique_ptr<EcdsaSignBoringSsl> sign(
      new EcdsaSignBoringSsl(key.release(), hash));
  return std::move(sign);
}

EcdsaSignBoringSsl::EcdsaSignBoringSsl(EC_KEY* key, const EVP_MD* hash)
    : key_(key), hash_(hash) {}

util::StatusOr<std::string> EcdsaSignBoringSsl::Sign(
    absl::string_view data) const {
  // Compute the digest.
  unsigned int digest_size;
  uint8_t digest[EVP_MAX_MD_SIZE];
  if (1 != EVP_Digest(data.data(), data.size(), digest, &digest_size, hash_,
                  nullptr)) {
    return util::Status(util::error::INTERNAL, "Could not compute digest.");
  }

  // Compute the signature.
  std::vector<uint8_t> buffer(ECDSA_size(key_.get()));
  unsigned int sig_length;
  if (1 != ECDSA_sign(0 /* unused */, digest, digest_size, buffer.data(),
                  &sig_length, key_.get())) {
    return util::Status(util::error::INTERNAL, "Signing failed.");
  }

  return std::string(reinterpret_cast<char*>(buffer.data()), sig_length);
}

}  // namespace subtle
}  // namespace tink
}  // namespace crypto
