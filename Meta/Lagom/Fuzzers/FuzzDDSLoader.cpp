/*
 * Copyright (c) 2023, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/ImageFormats/DDSLoader.h>

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    auto decoder_or_error = Gfx::DDSImageDecoderPlugin::create({ data, size });
    if (decoder_or_error.is_error())
        return 0;
    auto decoder = decoder_or_error.release_value();
    (void)decoder->frame(0);
    return 0;
}
