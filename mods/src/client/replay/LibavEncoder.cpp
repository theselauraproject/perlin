#include "client/replay/LibavEncoder.hpp"

#include <algorithm>
#include <cstring>

#ifdef PERLIN_HAVE_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#endif

namespace perlin::replay::ffmpeg {
#ifndef PERLIN_HAVE_LIBAV
    bool LibavCompiledIn() {
        return false;
    }
    Catalog QueryLibav() {
        return Catalog{};
    }
    struct LibavEncoder::Impl {};
    LibavEncoder::LibavEncoder() = default;
    LibavEncoder::~LibavEncoder() = default;
    bool LibavEncoder::Start(const Settings&, const Catalog&, std::string& error) {
        error = "this build of Perlin does not include the video encoder libraries";
        return false;
    }
    bool LibavEncoder::WriteFrame(const uint8_t*, size_t) {
        return false;
    }
    bool LibavEncoder::Finish(std::string&) {
        return false;
    }
    void LibavEncoder::Abort() {}
#else
    namespace {
        std::string Err(int code) {
            char buf[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(code, buf, sizeof buf);
            return buf;
        }
    }

    bool LibavCompiledIn() {
        return true;
    }

    Catalog QueryLibav() {
        Catalog cat;
        cat.available = true;
        cat.builtin = true;
        cat.path = "built into Perlin";
        cat.version = std::string("FFmpeg ") + av_version_info();
        void* it = nullptr;
        while (const AVCodec* c = av_codec_iterate(&it))
            if (av_codec_is_encoder(c) && c->type == AVMEDIA_TYPE_VIDEO)
                cat.encoders.push_back(c->name);
        it = nullptr;
        while (const AVOutputFormat* f = av_muxer_iterate(&it))
            cat.muxers.push_back(f->name);
        std::sort(cat.encoders.begin(), cat.encoders.end());
        cat.encoders.erase(std::unique(cat.encoders.begin(), cat.encoders.end()), cat.encoders.end());
        std::sort(cat.muxers.begin(), cat.muxers.end());
        return cat;
    }

    struct LibavEncoder::Impl {
        AVFormatContext* fmt = nullptr;
        AVCodecContext* ctx = nullptr;
        AVStream* stream = nullptr;
        SwsContext* sws = nullptr;
        AVFrame* frame = nullptr;
        AVPacket* packet = nullptr;
        int inW = 0, inH = 0;
        int64_t pts = 0;
        bool headerWritten = false, ioOpen = false;

        void Free() {
            if (sws)
                sws_freeContext(sws);
            sws = nullptr;
            if (frame)
                av_frame_free(&frame);
            if (packet)
                av_packet_free(&packet);
            if (ctx)
                avcodec_free_context(&ctx);
            if (fmt) {
                if (ioOpen && fmt->pb)
                    avio_closep(&fmt->pb);
                avformat_free_context(fmt);
                fmt = nullptr;
            }
            ioOpen = false;
            headerWritten = false;
        }

        int Drain(std::string& error) {
            for (;;) {
                const int r = avcodec_receive_packet(ctx, packet);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                    return 0;
                if (r < 0) {
                    error = "encoder error: " + Err(r);
                    return r;
                }
                av_packet_rescale_ts(packet, ctx->time_base, stream->time_base);
                packet->stream_index = stream->index;
                const int w = av_interleaved_write_frame(fmt, packet);
                av_packet_unref(packet);
                if (w < 0) {
                    error = "write error: " + Err(w);
                    return w;
                }
            }
        }
    };

    LibavEncoder::LibavEncoder() : impl(std::make_unique<Impl>()) {}
    LibavEncoder::~LibavEncoder() {
        Abort();
    }

    void LibavEncoder::Abort() {
        if (impl)
            impl->Free();
    }

    bool LibavEncoder::Start(const Settings& s, const Catalog& cat, std::string& error) {
        Impl& m = *impl;
        m.Free();
        static bool logSet = [] {
            av_log_set_level(AV_LOG_WARNING);
            return true;
        }();
        (void)logSet;
        EncodePlan plan;
        if (!PlanEncode(s, cat, plan, error))
            return false;
        const AVCodec* codec = avcodec_find_encoder_by_name(plan.encoder.c_str());
        if (!codec) {
            error = "this build has no encoder \"" + plan.encoder + "\"";
            return false;
        }
        int r = avformat_alloc_output_context2(
            &m.fmt, nullptr, plan.muxer.empty() ? nullptr : plan.muxer.c_str(), s.outputPath.c_str());
        if (r < 0 || !m.fmt) {
            error = "cannot choose an output format: " + Err(r);
            m.Free();
            return false;
        }
        m.stream = avformat_new_stream(m.fmt, nullptr);
        m.ctx = avcodec_alloc_context3(codec);
        if (!m.stream || !m.ctx) {
            error = "out of memory";
            m.Free();
            return false;
        }
        const AVPixelFormat pix = av_get_pix_fmt(plan.pixFmt.c_str());
        if (pix == AV_PIX_FMT_NONE) {
            error = "unknown pixel format \"" + plan.pixFmt + "\"";
            m.Free();
            return false;
        }
        m.inW = s.inputWidth > 0 ? s.inputWidth : s.width;
        m.inH = s.inputHeight > 0 ? s.inputHeight : s.height;
        const AVRational framerate = av_d2q(s.fps, 1001000);
        m.ctx->width = s.width;
        m.ctx->height = s.height;
        m.ctx->pix_fmt = pix;
        m.ctx->time_base = av_inv_q(framerate);
        m.ctx->framerate = framerate;
        m.ctx->sample_aspect_ratio = AVRational{1, 1};
        m.ctx->thread_count = 0;
        m.ctx->gop_size = std::max(1, static_cast<int>(s.fps * 5.0));
        if (plan.qscale >= 0) {
            m.ctx->flags |= AV_CODEC_FLAG_QSCALE;
            m.ctx->global_quality = FF_QP2LAMBDA * plan.qscale;
        }
        if (plan.codecTag.size() == 4)
            m.ctx->codec_tag = MKTAG(plan.codecTag[0], plan.codecTag[1], plan.codecTag[2], plan.codecTag[3]);
        if (m.fmt->oformat->flags & AVFMT_GLOBALHEADER)
            m.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* opts = nullptr;
        for (const auto& kv : plan.options)
            av_dict_set(&opts, kv.first.c_str(), kv.second.c_str(), 0);
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix);
        const bool yuv = desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 3 &&
                         !(desc->flags & AV_PIX_FMT_FLAG_PAL);
        const bool fullRange = plan.pixFmt.starts_with("yuvj");
        if (plan.tagColors) {
            av_dict_set(&opts, "colorspace", "bt709", 0);
            av_dict_set(&opts, "color_primaries", "bt709", 0);
            av_dict_set(&opts, "color_trc", "bt709", 0);
            av_dict_set(&opts, "color_range", "tv", 0);
        }
        r = avcodec_open2(m.ctx, codec, &opts);
        av_dict_free(&opts);
        if (r < 0) {
            error = "cannot open encoder " + plan.encoder + ": " + Err(r);
            m.Free();
            return false;
        }
        avcodec_parameters_from_context(m.stream->codecpar, m.ctx);
        m.stream->time_base = m.ctx->time_base;
        m.stream->avg_frame_rate = framerate;
        if (!(m.fmt->oformat->flags & AVFMT_NOFILE)) {
            r = avio_open(&m.fmt->pb, s.outputPath.c_str(), AVIO_FLAG_WRITE);
            if (r < 0) {
                error = "cannot create " + s.outputPath + ": " + Err(r);
                m.Free();
                return false;
            }
            m.ioOpen = true;
        }
        r = avformat_write_header(m.fmt, nullptr);
        if (r < 0) {
            error = "cannot write the file header: " + Err(r);
            m.Free();
            return false;
        }
        m.headerWritten = true;
        m.sws = sws_getContext(m.inW, m.inH, AV_PIX_FMT_BGRA, s.width, s.height, pix, SWS_AREA, nullptr,
                               nullptr, nullptr);
        if (!m.sws) {
            error = "cannot convert frames to " + plan.pixFmt;
            m.Free();
            return false;
        }
        if (yuv) {
            const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(m.sws, coeffs, 1, coeffs, fullRange ? 1 : 0, 0, 1 << 16, 1 << 16);
        }
        m.frame = av_frame_alloc();
        m.packet = av_packet_alloc();
        if (!m.frame || !m.packet) {
            error = "out of memory";
            m.Free();
            return false;
        }
        m.frame->format = pix;
        m.frame->width = s.width;
        m.frame->height = s.height;
        r = av_frame_get_buffer(m.frame, 0);
        if (r < 0) {
            error = "cannot allocate a frame: " + Err(r);
            m.Free();
            return false;
        }
        m.pts = 0;
        return true;
    }

    bool LibavEncoder::WriteFrame(const uint8_t* bgra, size_t bytes) {
        Impl& m = *impl;
        if (!m.ctx || bytes != static_cast<size_t>(m.inW) * static_cast<size_t>(m.inH) * 4)
            return false;
        if (av_frame_make_writable(m.frame) < 0)
            return false;
        const uint8_t* src[1] = {bgra};
        const int stride[1] = {m.inW * 4};
        sws_scale(m.sws, src, stride, 0, m.inH, m.frame->data, m.frame->linesize);
        m.frame->pts = m.pts++;
        int r = avcodec_send_frame(m.ctx, m.frame);
        if (r < 0)
            return false;
        std::string err;
        return m.Drain(err) >= 0;
    }

    bool LibavEncoder::Finish(std::string& error) {
        Impl& m = *impl;
        if (!m.ctx) {
            error = "not started";
            return false;
        }
        bool ok = true;
        int r = avcodec_send_frame(m.ctx, nullptr);
        if (r >= 0)
            ok = m.Drain(error) >= 0;
        if (ok && m.headerWritten) {
            r = av_write_trailer(m.fmt);
            if (r < 0) {
                error = "cannot finish the file: " + Err(r);
                ok = false;
            }
        }
        m.Free();
        return ok;
    }
#endif
}
