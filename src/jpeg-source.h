// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#ifndef JPEG_SOURCE_H_
#define JPEG_SOURCE_H_

#include <csignal>
#include <memory>
#include <string>

#include "display-options.h"
#include "framebuffer.h"
#include "image-source.h"
#include "renderer.h"
#include "timg-time.h"

namespace timg {
// Special case for JPEG decoding, as we can make use of decode+rough_scale
// in one go.
class JPEGSource final : public ImageSource {
public:
    explicit JPEGSource(const std::string &filename) : ImageSource(filename) {}

    static const char *VersionInfo();

    bool LoadAndScale(const DisplayOptions &options, int frame_offset,
                      int frame_count) final;

    void SendFrames(const Duration &duration, int loops,
                    const volatile sig_atomic_t &interrupt_received,
                    const Renderer::WriteFramebufferFun &sink) final;

    std::string FormatTitle(const std::string &format_string) const final;

private:
    int IndentationIfCentered(const timg::Framebuffer &image) const;

    DisplayOptions options_;
    int orig_width_, orig_height_;
    std::unique_ptr<timg::Framebuffer> image_;
};

}  // namespace timg

#endif  // JPEG_SOURCE_H_
