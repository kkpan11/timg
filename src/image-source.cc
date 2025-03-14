// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
// (c) 2016-2021 Henner Zeller <h.zeller@acm.org>
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

#include "image-source.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

// Various implementations for the factory.
#include "display-options.h"
#include "graphics-magick-source.h"
#include "jpeg-source.h"
#include "openslide-source.h"
#include "pdf-image-source.h"
#include "qoi-image-source.h"
#include "stb-image-source.h"
#include "svg-image-source.h"
#include "video-source.h"

namespace timg {

// Returns 'true' if image needs scaling.
bool ImageSource::CalcScaleToFitDisplay(int img_width, int img_height,
                                        const DisplayOptions &orig_options,
                                        bool fit_in_rotated, int *target_width,
                                        int *target_height) {
    DisplayOptions options = orig_options;
    if (fit_in_rotated) {
        std::swap(options.width, options.height);
        std::swap(options.fill_width, options.fill_height);
        options.width_stretch = 1.0f / orig_options.width_stretch;
    }

    // Clamp stretch to reasonable values.
    float width_stretch          = options.width_stretch;
    const float kMaxAcceptFactor = 5.0;  // Clamp to reasonable factor.
    if (width_stretch > kMaxAcceptFactor) width_stretch = kMaxAcceptFactor;
    if (width_stretch < 1 / kMaxAcceptFactor)
        width_stretch = 1 / kMaxAcceptFactor;

    if (width_stretch > 1.0f) {
        options.width /= width_stretch;  // pretend to have less space
    }
    else {
        options.height *= width_stretch;
    }
    const float width_fraction  = (float)options.width / img_width;
    const float height_fraction = (float)options.height / img_height;

    // If the image < screen, only upscale if do_upscale requested
    if (!options.upscale && (options.fill_height || width_fraction > 1.0) &&
        (options.fill_width || height_fraction > 1.0)) {
        *target_width  = img_width;
        *target_height = img_height;
        if (options.cell_x_px == 2) {
            // The quarter block feels a bit like good old EGA graphics
            // with some broken aspect ratio...
            *target_width *= 2;
            return true;
        }
        return false;
    }

    *target_width  = options.width;
    *target_height = options.height;

    if (options.fill_width && options.fill_height) {
        // Fill as much as we can get in available space.
        // Largest scale fraction determines that. This is for some diagonal
        // scroll modes.
        const float larger_fraction = (width_fraction > height_fraction)
                                          ? width_fraction
                                          : height_fraction;
        *target_width               = (int)roundf(larger_fraction * img_width);
        *target_height              = (int)roundf(larger_fraction * img_height);
    }
    else if (options.fill_height) {
        // Make things fit in vertical space.
        // While the height constraint stays the same, we can expand width to
        // wider than screen.
        *target_width = (int)roundf(height_fraction * img_width);
    }
    else if (options.fill_width) {
        // dito, vertical. Make things fit in horizontal, overflow vertical.
        *target_height = (int)roundf(width_fraction * img_height);
    }
    else {
        // Typical situation: whatever limits first
        const float smaller_fraction = (width_fraction < height_fraction)
                                           ? width_fraction
                                           : height_fraction;
        *target_width  = (int)roundf(smaller_fraction * img_width);
        *target_height = (int)roundf(smaller_fraction * img_height);
    }

    if (width_stretch > 1.0f) {
        *target_width *= width_stretch;
    }
    else {
        *target_height /= width_stretch;
    }

    // floor() to next full character cell size but only if we in one of
    // the block modes.
    if (options.cell_x_px > 0 && options.cell_x_px <= 2 &&
        options.cell_y_px > 0 && options.cell_y_px <= 2) {
        *target_width  = *target_width / options.cell_x_px * options.cell_x_px;
        *target_height = *target_height / options.cell_y_px * options.cell_y_px;
    }

    // Don't scale down to nothing...
    if (*target_width <= 0) *target_width = 1;
    if (*target_height <= 0) *target_height = 1;

    if (options.upscale_integer && *target_width > img_width &&
        *target_height > img_height) {
        // Correct for aspect ratio mismatch of quarter rendering.
        const float aspect_correct = options.cell_x_px == 2 ? 2 : 1;
        const float wf = 1.0f * *target_width / aspect_correct / img_width;
        const float hf = 1.0f * *target_height / img_height;
        const float smaller_factor = wf < hf ? wf : hf;
        if (smaller_factor > 1.0f) {
            *target_width  = aspect_correct * floor(smaller_factor) * img_width;
            *target_height = floor(smaller_factor) * img_height;
        }
    }

    return *target_width != img_width || *target_height != img_height;
}

ImageSource *ImageSource::Create(const std::string &filename,
                                 const DisplayOptions &options,
                                 int frame_offset, int frame_count,
                                 bool attempt_image_loading,
                                 bool attempt_video_loading,
                                 std::string *error) {
    std::unique_ptr<ImageSource> result;
    if (attempt_image_loading) {
#ifdef WITH_TIMG_OPENSLIDE_SUPPORT
        result.reset(new OpenSlideSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_QOI
        result.reset(new QOIImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_JPEG
        result.reset(new JPEGSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_RSVG
        result.reset(new SVGImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_POPPLER
        result.reset(new PDFImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_GRPAPHICSMAGICK
        result.reset(new GraphicsMagickSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif

#ifdef WITH_TIMG_STB
        // STB image loading always last as last fallback resort.
        result.reset(new STBImageSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
#endif
    }  // end attempt image loading

#ifdef WITH_TIMG_VIDEO
    if (attempt_video_loading) {
        result.reset(new VideoSource(filename));
        if (result->LoadAndScale(options, frame_offset, frame_count)) {
            return result.release();
        }
    }  // end attempt video loading
#endif

    // Ran into trouble opening. Let's see if this is even an accessible file.
    if (filename != "-") {
        struct stat statresult;
        if (stat(filename.c_str(), &statresult) < 0) {
            error->append(filename).append(": ").append(strerror(errno));
        }
        else if (S_ISDIR(statresult.st_mode)) {
            error->append(filename).append(": is a directory");
        }
        else if (access(filename.c_str(), R_OK) < 0) {
            error->append(filename).append(": ").append(strerror(errno));
        }
    }

    // We either loaded, played and continue'ed, or we end up here.
    // fprintf(stderr, "%s: couldn't load\n", filename);
#ifdef WITH_TIMG_VIDEO
    if (filename == "-" || filename == "/dev/stdin") {
        *error = "If this is a video on stdin, use '-V' to skip image probing";
    }
#endif

#ifndef WITH_TIMG_VIDEO
    if (error->empty()) {
        const char *const end_filename = filename.data() + filename.length();
        for (const char *suffix :
             {".mov", ".mp4", ".mkv", ".avi", ".wmv", ".webm"}) {
            const size_t suffix_len = strlen(suffix);
            if (filename.length() < suffix_len) continue;
            if (strcasecmp(end_filename - suffix_len, suffix) == 0) {
                error->append(filename).append(
                    ": looks like a video file, but video support not compiled "
                    "into this timg.");
                break;
            }
        }
    }
#endif

    return nullptr;
}

static std::string Basename(const std::string &filename) {
    const size_t last_slash_pos = filename.find_last_of("/\\");
    return last_slash_pos == std::string::npos
               ? filename
               : filename.substr(last_slash_pos + 1);
}

std::string ImageSource::FormatFromParameters(const std::string &fmt_string,
                                              const std::string &filename,
                                              int orig_width, int orig_height,
                                              const char *decoder) {
    std::stringstream result;
    for (size_t i = 0; i < fmt_string.length(); ++i) {
        if (fmt_string[i] != '%' || i >= fmt_string.length() - 1) {
            result << fmt_string[i];
            continue;
        }

        ++i;
        switch (fmt_string[i]) {
        case 'f': result << filename; break;
        case 'b': result << Basename(filename); break;
        case 'w': result << orig_width; break;
        case 'h': result << orig_height; break;
        case 'D': result << decoder; break;
        default: result << fmt_string[i]; break;
        }
    }

    return result.str();
}

static bool HasAPNGHeader(const std::string &filename) {
    static constexpr size_t kPngHeaderLen = 8;

    // Iterate through headers in the first kibibyte until we find an acTL one.
    const int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) return false;
    size_t pos = kPngHeaderLen;
    uint8_t buf[8];
    bool found_actl_header = false;
    while (!found_actl_header && pos < 1024) {
        const ssize_t len = pread(fd, buf, 8, pos);
        if (len < 0 || len != 8) break;  // Best effort.
        found_actl_header = (memcmp(buf + 4, "acTL", 4) == 0);
        // Header contains data length; add sizeof() for len, CRC, and ChunkType
        pos += ntohl(*(uint32_t *)buf) + 12;
    }
    close(fd);
    return found_actl_header;
}

bool ImageSource::LooksLikeAPNG(const std::string &filename) {
    const char *const file = filename.c_str();
    for (const char *ending : {".png", ".apng"}) {
        if (strcasecmp(file + strlen(file) - strlen(ending), ending) == 0 &&
            HasAPNGHeader(filename)) {
            return true;
        }
    }
    return false;
}

}  // namespace timg
