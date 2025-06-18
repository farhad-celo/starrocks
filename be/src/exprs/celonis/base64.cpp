// Copyright 2021-present StarRocks, Inc. All rights reserved.
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

// This file is based on code available under the Apache license here:
//   https://github.com/apache/incubator-doris/blob/master/be/src/exprs/base64.cpp

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "base64.h"

#include <cmath>

static char s_encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                  'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

static const char base64_pad = '=';

static short s_decoding_table[256] = {
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -1, -1, -2, -2, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -1, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, 62, -2, -2, -2, 63, 52, 53, 54, 55,
        56, 57, 58, 59, 60, 61, -2, -2, -2, -2, -2, -2, -2, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -2, -2, -2, -2, -2, -2, 26, 27, 28, 29, 30, 31, 32,
        33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
        -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2};

static int s_mod_table[] = {0, 2, 1};

namespace starrocks {

size_t base64_encode3(const unsigned char* data, size_t length, unsigned char* encoded_data) {
    auto output_length = (size_t)(4.0 * ceil((double)length / 3.0));

    if (encoded_data == nullptr) {
        return 0;
    }

    for (uint32_t i = 0, j = 0; i < length;) {
        uint32_t octet_a = i < length ? data[i++] : 0;
        uint32_t octet_b = i < length ? data[i++] : 0;
        uint32_t octet_c = i < length ? data[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = s_encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = s_encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = s_encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = s_encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < s_mod_table[length % 3]; i++) {
        encoded_data[output_length - 1 - i] = '=';
    }

    return output_length;
}

int64_t base64_decode3(const char* data, size_t length, char* decoded_data) {
    if (!data || !decoded_data) {
        return -1;
    }

    const unsigned char* src = reinterpret_cast<const unsigned char*>(data);
    unsigned char* dst = reinterpret_cast<unsigned char*>(decoded_data);
    size_t dst_idx = 0;

    // Fast path variables
    size_t i = 0;

    // Try fast path first - process complete 4-byte blocks without whitespace
    while (i + 4 <= length) {
        // Check for padding in this block
        if (src[i + 2] == base64_pad || src[i + 3] == base64_pad) {
            break;  // Handle padding in slow path
        }

        // Get all 4 values
        short val0 = s_decoding_table[src[i]];
        short val1 = s_decoding_table[src[i + 1]];
        short val2 = s_decoding_table[src[i + 2]];
        short val3 = s_decoding_table[src[i + 3]];

        // If any character is invalid or whitespace, switch to slow path
        if (val0 < 0 || val1 < 0 || val2 < 0 || val3 < 0) {
            break;
        }

        // Fast decode: 4 chars -> 3 bytes
        dst[dst_idx++] = (val0 << 2) | (val1 >> 4);
        dst[dst_idx++] = (val1 << 4) | (val2 >> 2);
        dst[dst_idx++] = (val2 << 6) | val3;

        i += 4;
    }

    // Slow path for remaining data (padding, whitespace, invalid chars)
    int bits_collected = 0;
    unsigned int accumulator = 0;

    while (i < length) {
        unsigned char ch = src[i++];

        if (ch == base64_pad) {
            // Handle padding
            if ((bits_collected % 8) != 0) {
                // We have incomplete bytes, this is valid padding
                // Skip any remaining padding characters
                while (i < length && src[i] == base64_pad) {
                    i++;
                }
                // After padding, only whitespace is allowed
                while (i < length) {
                    short val = s_decoding_table[src[i]];
                    if (val != -1) {  // Not whitespace
                        return -1;    // Invalid character after padding
                    }
                    i++;
                }
                break;
            } else {
                // Padding when we don't need it
                return -1;
            }
        }

        short val = s_decoding_table[ch];

        if (val == -1) {
            // Whitespace - skip
            continue;
        } else if (val == -2) {
            // Invalid character
            return -1;
        }

        // Valid base64 character
        accumulator = (accumulator << 6) | val;
        bits_collected += 6;

        if (bits_collected >= 8) {
            bits_collected -= 8;
            dst[dst_idx++] = (accumulator >> bits_collected) & 0xFF;
        }
    }

    // Final validation - we shouldn't have more than 4 bits left
    if (bits_collected >= 8) {
        return -1;
    }

    // Null terminate
    decoded_data[dst_idx] = '\0';

    return dst_idx;
}

} // namespace starrocks
