#pragma once

#include <string>

#include "modules/common/int_types.h"

namespace celonis::accelerator::legacy_embedded_format::base64 {

/**
 * base64_encode - Base64 encode
 * @src: Data to be encoded
 * @len: Length of the data to be encoded
 * @out: Pointer to allocated output data array (non-null terminated)
 *
 * Caller is responsible for freeing the out buffer. Buffer is NOT null terminated.
 */
void base64_encode(const unsigned char* src, size_t len, char* out);

/**
 * @brief more modern (i.e., non-C-style) interface for the above functions
 * @param src the data to be encoded
 * @param len the length of the data to be encoded
 * @return base64 encoded string of the given input data
 */
std::string base64_encode(const unsigned char* src, size_t len);

}  // namespace celonis::accelerator::legacy_embedded_format::base64
