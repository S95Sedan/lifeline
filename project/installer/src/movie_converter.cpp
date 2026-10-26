#include "converters.hpp"
#include "movies.hpp"
#include <bcrypt.h>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
using namespace lifeline::setup;
namespace {
void av_check(int result, const char *action) {
    if (result >= 0)
        return;
    char message[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(action) + ": " + message);
}
struct Frame {
    AVFrame *value = av_frame_alloc();
    ~Frame() { av_frame_free(&value); }
    Frame() { require(value != nullptr, "Cannot allocate video frame"); }
    Frame(const Frame &) = delete;
};
struct Packet {
    AVPacket *value = av_packet_alloc();
    ~Packet() { av_packet_free(&value); }
    Packet() { require(value != nullptr, "Cannot allocate media packet"); }
};
struct Codec {
    AVCodecContext *value = nullptr;
    explicit Codec(const char *name, bool encode = false) {
        auto codec = encode ? avcodec_find_encoder_by_name(name) : avcodec_find_decoder_by_name(name);
        require(codec != nullptr, std::string("Codec unavailable: ") + name);
        value = avcodec_alloc_context3(codec);
        require(value != nullptr, "Cannot allocate codec");
    }
    ~Codec() { avcodec_free_context(&value); }
    void open() { av_check(avcodec_open2(value, value->codec, nullptr), "Open codec"); }
};
struct Scale {
    SwsContext *value = nullptr;
    ~Scale() { sws_freeContext(value); }
    void convert(AVFrame *source, AVFrame *destination) {
        value = sws_getCachedContext(
            value, source->width, source->height, AVPixelFormat(source->format), destination->width,
            destination->height, AVPixelFormat(destination->format), SWS_BILINEAR, nullptr, nullptr, nullptr);
        require(value != nullptr, "Cannot create color converter");
        require(sws_scale(value, source->data, source->linesize, 0, source->height, destination->data,
                          destination->linesize) == destination->height,
                "Cannot convert picture");
    }
};
struct Mux {
    AVFormatContext *value = nullptr;
    explicit Mux(const fs::path &path) {
        av_check(avformat_alloc_output_context2(&value, nullptr, "mp4", path_text(path).c_str()),
                 "Create MP4 output");
        require(value != nullptr, "Cannot create MP4 output");
        av_check(avio_open(&value->pb, path_text(path).c_str(), AVIO_FLAG_WRITE), "Open MP4 output");
    }
    ~Mux() {
        if (value) {
            if (value->pb)
                avio_closep(&value->pb);
            avformat_free_context(value);
        }
    }
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Bytes storage;
    Hash() {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
                "Cannot open movie hash");
        DWORD size = 0, used = 0;
        require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                  sizeof(size), &used, 0) >= 0,
                "Cannot read movie hash properties");
        storage.resize(size);
        require(BCryptCreateHash(algorithm, &hash, storage.data(), size, nullptr, 0, 0) >= 0,
                "Cannot create movie hash");
    }
    ~Hash() {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(View bytes_) {
        require(BCryptHashData(hash, const_cast<PUCHAR>(bytes_.data()), ULONG(bytes_.size()), 0) >= 0,
                "Cannot hash movie data");
    }
    std::string finish() {
        std::uint8_t result[32];
        require(BCryptFinishHash(hash, result, 32, 0) >= 0, "Cannot finish movie hash");
        static constexpr char digits[] = "0123456789abcdef";
        std::string text;
        for (auto value : result) {
            text += digits[value >> 4];
            text += digits[value & 15];
        }
        return text;
    }
};
struct Picture {
    std::map<std::string, View> parts;
};
std::vector<Picture> pictures(View source) {
    require(source.size() >= 48 && fixed_string(source, 0, 8) == "sd3strv3", "Unsupported SD3 movie");
    unsigned count = u32(source, 8);
    require(count && count < 100000, "Invalid movie frame count");
    std::size_t p = 32;
    std::vector<Picture> result;
    for (unsigned i = 0; i < count; ++i) {
        require(fixed_string(source, p, 4) == "AVF1" && u32(source, p + 4) == u32(source, p + 8) &&
                    !u32(source, p + 12),
                "Invalid movie frame envelope");
        auto size = u32(source, p + 4);
        p += 16;
        auto end = p + size;
        require(end <= source.size(), "Truncated movie frame");
        Picture picture;
        while (p < end) {
            auto tag = fixed_string(source, p, 4);
            auto allocated = u32(source, p + 4), used = u32(source, p + 8);
            require((tag == "VSF1" || tag == "VSF2" || tag == "ASC1") && used <= allocated &&
                        (!u32(source, p + 12)) && (tag == "VSF1" || used == allocated),
                    "Invalid movie subchunk");
            p += 16;
            require(p + allocated <= end, "Movie subchunk exceeds its frame");
            require(std::all_of(source.begin() + p + used, source.begin() + p + allocated,
                                [](auto byte) { return byte == 0; }),
                    "Nonzero movie padding");
            require(picture.parts.emplace(tag, slice(source, p, used)).second, "Duplicate movie subchunk");
            p += allocated;
        }
        require(p == end, "Invalid movie frame alignment");
        result.push_back(std::move(picture));
    }
    require(source.size() == p + 16 && fixed_string(source, p, 4) == "ENDC" &&
                std::all_of(source.begin() + p + 4, source.end(), [](auto byte) { return byte == 0; }),
            "Missing movie terminator");
    return result;
}
void decode(Codec &codec, View data, AVFrame *frame, bool drain = false) {
    Packet packet;
    av_check(av_new_packet(packet.value, int(data.size())), "Allocate movie packet");
    std::copy(data.begin(), data.end(), packet.value->data);
    av_check(avcodec_send_packet(codec.value, packet.value), "Decode movie packet");
    int result = avcodec_receive_frame(codec.value, frame);
    if (result == AVERROR(EAGAIN) && drain) {
        av_check(avcodec_send_packet(codec.value, nullptr), "Drain picture decoder");
        result = avcodec_receive_frame(codec.value, frame);
    }
    av_check(result, "Read decoded movie picture");
}
void make_frame(AVFrame *frame, unsigned w, unsigned h, AVPixelFormat format) {
    frame->width = int(w);
    frame->height = int(h);
    frame->format = format;
    av_check(av_frame_get_buffer(frame, 32), "Allocate picture planes");
}
struct Bits {
    Bytes data;
    unsigned used = 0;
    void put(std::uint64_t value, unsigned count) {
        require(count <= 32 && (count == 32 || value < (1ull << count)), "Invalid MPEG header field");
        for (unsigned bit = count; bit-- > 0;) {
            if (!used)
                data.push_back(0);
            data.back() |= std::uint8_t(((value >> bit) & 1) << (7 - used));
            used = (used + 1) & 7;
        }
    }
    void aligned() { used = 0; }
};
Bytes mpeg_picture(View payload, unsigned width, unsigned height, Bytes &matrix) {
    unsigned total = u32(payload, 0), coded = u32(payload, 4), flags = u32(payload, 8),
             options = u32(payload, 12);
    require(total == payload.size() && coded + 16 == total && (flags == 0x60 || flags == 0x62) &&
                !(options & ~1u),
            "Unsupported VSF2 descriptor");
    std::size_t start = 16;
    if (options & 1) {
        auto m = slice(payload, 16, 64);
        require(std::all_of(m.begin(), m.end(), [](auto v) { return v != 0; }),
                "Invalid quantization matrix");
        matrix.assign(m.begin(), m.end());
        start = 80;
    }
    Bits bits;
    auto marker = [&](unsigned code) {
        bits.aligned();
        bits.put(0, 8);
        bits.put(0, 8);
        bits.put(1, 8);
        bits.put(code, 8);
    };
    marker(0xb3);
    bits.put(width, 12);
    bits.put(height, 12);
    bits.put(1, 4);
    bits.put(5, 4);
    bits.put(262143, 18);
    bits.put(1, 1);
    bits.put(112, 10);
    bits.put(0, 1);
    bits.put(!matrix.empty(), 1);
    for (auto v : matrix)
        bits.put(v, 8);
    bits.put(0, 1);
    marker(0xb5);
    for (auto [value, count] : std::initializer_list<std::pair<unsigned, unsigned>>{{1, 4},
                                                                                    {0x48, 8},
                                                                                    {1, 1},
                                                                                    {1, 2},
                                                                                    {0, 2},
                                                                                    {0, 2},
                                                                                    {0, 12},
                                                                                    {1, 1},
                                                                                    {0, 8},
                                                                                    {0, 1},
                                                                                    {0, 2},
                                                                                    {0, 5}})
        bits.put(value, count);
    marker(0);
    bits.put(0, 10);
    bits.put(1, 3);
    bits.put(65535, 16);
    bits.put(0, 1);
    marker(0xb5);
    for (auto [value, count] :
         std::initializer_list<std::pair<unsigned, unsigned>>{{8, 4},
                                                              {65535, 16},
                                                              {flags & 3, 2},
                                                              {3, 2},
                                                              {0, 1},
                                                              {1, 1},
                                                              {0, 1},
                                                              {unsigned(bool(flags & 64)), 1},
                                                              {unsigned(bool(flags & 32)), 1},
                                                              {unsigned(bool(flags & 16)), 1},
                                                              {0, 1},
                                                              {1, 1},
                                                              {1, 1},
                                                              {0, 1}})
        bits.put(value, count);
    const std::uint8_t terminator[] = {0, 0, 1, 0xb0};
    for (unsigned row = 0; row < height / 16; ++row) {
        auto from = payload.begin() + start;
        auto end = std::search(from, payload.end(), std::begin(terminator), std::end(terminator));
        require(end != payload.end(), "Missing IPU strip terminator");
        auto length = std::size_t(end - from);
        marker(row + 1);
        bits.put(5, 7);
        for (auto v : View(from, length))
            bits.put(v, 8);
        bits.aligned();
        start = std::size_t(end - payload.begin()) + 4;
    }
    require(std::all_of(payload.begin() + start, payload.end(), [](auto value) { return !value; }),
            "Nonzero IPU padding");
    marker(0xb7);
    return bits.data;
}
void write_encoded(Mux &mux, Codec &encoder, AVStream *stream, AVFrame *frame) {
    av_check(avcodec_send_frame(encoder.value, frame), "Encode native movie frame");
    Packet packet;
    for (;;) {
        auto result = avcodec_receive_packet(encoder.value, packet.value);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            break;
        av_check(result, "Receive encoded movie packet");
        if (encoder.value->codec_type == AVMEDIA_TYPE_VIDEO && !packet.value->duration)
            packet.value->duration = 1;
        av_packet_rescale_ts(packet.value, encoder.value->time_base, stream->time_base);
        packet.value->stream_index = stream->index;
        av_check(av_interleaved_write_frame(mux.value, packet.value), "Write MP4 packet");
        av_packet_unref(packet.value);
    }
}
Bytes audio_pcm(View encoded) {
    require(encoded.size() % 2048 == 0, "Incomplete movie audio group");
    Codec decoder("adpcm_psx");
    decoder.value->sample_rate = 48000;
    av_channel_layout_default(&decoder.value->ch_layout, 2);
    decoder.open();
    Frame frame;
    Bytes pcm;
    pcm.reserve(encoded.size() * 7 / 2);
    for (std::size_t p = 0; p < encoded.size(); p += 2048) {
        Bytes packet;
        for (unsigned i = 0; i < 1024; i += 16) {
            append(packet, slice(encoded, p + i, 16));
            append(packet, slice(encoded, p + 1024 + i, 16));
        }
        decode(decoder, packet, frame.value);
        require(frame.value->format == AV_SAMPLE_FMT_S16P && frame.value->ch_layout.nb_channels == 2,
                "Unexpected movie audio format");
        for (int i = 0; i < frame.value->nb_samples; ++i) {
            append(pcm, View(frame.value->data[0] + i * 2, 2));
            append(pcm, View(frame.value->data[1] + i * 2, 2));
        }
        av_frame_unref(frame.value);
    }
    return pcm;
}
unsigned aperture(const fs::path &movie, unsigned width, unsigned height) {
    AVFormatContext *raw = nullptr;
    av_check(avformat_open_input(&raw, path_text(movie).c_str(), nullptr, nullptr),
             "Open movie aperture scan");
    struct Cleanup {
        AVFormatContext *&p;
        ~Cleanup() { avformat_close_input(&p); }
    } cleanup{raw};
    av_check(avformat_find_stream_info(raw, nullptr), "Read movie tracks");
    int stream = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    av_check(stream, "Find video track");
    Codec decoder("h264");
    av_check(avcodec_parameters_to_context(decoder.value, raw->streams[stream]->codecpar),
             "Read aperture decoder parameters");
    decoder.open();
    Packet packet;
    Frame frame, rgb;
    make_frame(rgb.value, width, height, AV_PIX_FMT_RGB24);
    Scale scale;
    unsigned margin = height, count = 0;
    auto scan = [&] {
        scale.convert(frame.value, rgb.value);
        unsigned first = height, last = height;
        for (unsigned y = 0; y < height; ++y) {
            auto row = View(rgb.value->data[0] + y * rgb.value->linesize[0], width * 3);
            if (std::any_of(row.begin(), row.end(), [](auto value) { return value > 8; })) {
                first = std::min(first, y);
                last = height - 1 - y;
            }
        }
        if (first < height)
            margin = std::min(margin, std::min(first, last));
        ++count;
    };
    while (av_read_frame(raw, packet.value) >= 0) {
        if (packet.value->stream_index == stream) {
            av_check(avcodec_send_packet(decoder.value, packet.value), "Scan aperture picture");
            while (avcodec_receive_frame(decoder.value, frame.value) >= 0) {
                scan();
                av_frame_unref(frame.value);
            }
        }
        av_packet_unref(packet.value);
    }
    av_check(avcodec_send_packet(decoder.value, nullptr), "Drain aperture scan");
    while (avcodec_receive_frame(decoder.value, frame.value) >= 0) {
        scan();
        av_frame_unref(frame.value);
    }
    require(count, "Empty converted movie");
    if (margin == height)
        margin = 0;
    margin = margin > 2 ? margin - 2 : 0;
    if (margin < height / 32 || margin > height / 4)
        margin = 0;
    return margin;
}
void convert(const fs::path &iso, const fs::path &output, const std::string &name, const std::string &archive,
             const std::string &member) {
    Disc disc(iso);
    auto source = disc.file(archive);
    if (!member.empty()) {
        auto members = archive_members(source);
        require(members.contains(member), "Movie member is missing");
        source = std::move(members.at(member));
    }
    auto frames = pictures(source);
    unsigned width = u32(source, 12), height = u32(source, 16), rate = u32(source, 20), hz = u32(source, 24),
             channels = u32(source, 28);
    require(width && width <= 640 && height && height <= 448 && width % 16 == 0 && height % 16 == 0 &&
                rate == 30 && ((hz == 48000 && channels == 2) || (!hz && !channels)),
            "Unsupported movie dimensions or audio");
    Bytes compressed;
    unsigned video_count = 0;
    bool tail = false;
    for (const auto &frame : frames) {
        bool picture = frame.parts.contains("VSF1") || frame.parts.contains("VSF2");
        require(!(frame.parts.contains("VSF1") && frame.parts.contains("VSF2")),
                "Multiple pictures in a movie frame");
        if (picture) {
            require(!tail, "Picture after audio-only tail");
            ++video_count;
        } else
            tail = true;
        if (frame.parts.contains("ASC1"))
            append(compressed, frame.parts.at("ASC1"));
    }
    auto pcm = audio_pcm(compressed);
    require(channels || pcm.empty(), "Audio in silent movie");
    std::uint64_t samples = pcm.size() / 4;
    fs::create_directories(output);
    auto movie = safe_path(output, name + ".mp4");
    Hash rgb_hash;
    {
        Mux mux(movie);
        Codec video("h264_mf", true);
        video.value->width = int(width);
        video.value->height = int(height);
        video.value->pix_fmt = AV_PIX_FMT_YUV420P;
        video.value->time_base = {1, 30};
        video.value->framerate = {30, 1};
        video.value->gop_size = 60;
        video.value->max_b_frames = 0;
        video.value->thread_count = 4;
        video.value->profile = AV_PROFILE_H264_HIGH;
        video.value->bit_rate = 12'000'000;
        if (mux.value->oformat->flags & AVFMT_GLOBALHEADER)
            video.value->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        av_check(av_opt_set(video.value->priv_data, "rate_control", "quality", 0), "Set movie rate control");
        av_check(av_opt_set_int(video.value->priv_data, "quality", 100, 0), "Set movie quality");
        av_check(av_opt_set(video.value->priv_data, "scenario", "archive", 0), "Set movie encoder scenario");
        video.open();
        auto video_stream = avformat_new_stream(mux.value, nullptr);
        require(video_stream, "Cannot create video track");
        video_stream->time_base = video.value->time_base;
        av_check(avcodec_parameters_from_context(video_stream->codecpar, video.value),
                 "Set video track parameters");
        std::unique_ptr<Codec> audio;
        AVStream *audio_stream = nullptr;
        if (channels) {
            audio = std::make_unique<Codec>("aac", true);
            audio->value->sample_rate = 48000;
            audio->value->sample_fmt = AV_SAMPLE_FMT_FLTP;
            audio->value->time_base = {1, 48000};
            audio->value->bit_rate = 320000;
            av_channel_layout_default(&audio->value->ch_layout, 2);
            if (mux.value->oformat->flags & AVFMT_GLOBALHEADER)
                audio->value->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            audio->open();
            audio_stream = avformat_new_stream(mux.value, nullptr);
            require(audio_stream, "Cannot create audio track");
            audio_stream->time_base = {1, 48000};
            av_check(avcodec_parameters_from_context(audio_stream->codecpar, audio->value),
                     "Set audio track parameters");
        }
        AVDictionary *options = nullptr;
        av_dict_set(&options, "movflags", "+faststart", 0);
        av_dict_set(&options, "movie_timescale", "48000", 0);
        auto header = avformat_write_header(mux.value, &options);
        av_dict_free(&options);
        av_check(header, "Write MP4 header");
        Codec strip("ipu"), full("ipu"), mpeg("mpeg2video");
        strip.value->width = int(width);
        strip.value->height = 16;
        strip.open();
        full.value->width = int(width);
        full.value->height = int(height);
        full.open();
        mpeg.open();
        Frame picture, assembled, rgb, encoded;
        make_frame(assembled.value, width, height, AV_PIX_FMT_YUV420P);
        make_frame(rgb.value, width, height, AV_PIX_FMT_RGB24);
        make_frame(encoded.value, width, height, AV_PIX_FMT_YUV420P);
        Scale to_rgb, to_yuv;
        Bytes matrix;
        std::uint64_t audio_position = 0;
        auto encode_audio = [&](std::uint64_t limit) {
            while (audio && audio_position < samples && audio_position < limit) {
                Frame frame;
                frame.value->format = AV_SAMPLE_FMT_FLTP;
                frame.value->sample_rate = 48000;
                av_check(av_channel_layout_copy(&frame.value->ch_layout, &audio->value->ch_layout),
                         "Set AAC frame channels");
                frame.value->nb_samples =
                    int(std::min<std::uint64_t>(audio->value->frame_size, samples - audio_position));
                frame.value->pts = std::int64_t(audio_position);
                av_check(av_frame_get_buffer(frame.value, 0), "Allocate AAC frame");
                for (int i = 0; i < frame.value->nb_samples; ++i)
                    for (unsigned c = 0; c < 2; ++c)
                        reinterpret_cast<float *>(frame.value->data[c])[i] =
                            float(std::int16_t(u16(pcm, std::size_t((audio_position + i) * 4 + c * 2)))) /
                            32768.0f;
                audio_position += frame.value->nb_samples;
                write_encoded(mux, *audio, audio_stream, frame.value);
            }
        };
        for (unsigned number = 0; number < frames.size(); ++number) {
            const auto &parts = frames[number].parts;
            if (parts.contains("VSF2") || parts.contains("VSF1")) {
                AVFrame *decoded = nullptr;
                if (parts.contains("VSF1")) {
                    decode(full, parts.at("VSF1"), picture.value);
                    decoded = picture.value;
                } else {
                    auto payload = parts.at("VSF2");
                    if (!matrix.empty() || u32(payload, 12)) {
                        auto packet = mpeg_picture(payload, width, height, matrix);
                        decode(mpeg, packet, picture.value, true);
                        avcodec_flush_buffers(mpeg.value);
                        decoded = picture.value;
                    } else {
                        require(u32(payload, 0) == payload.size() && u32(payload, 4) + 16 == payload.size() &&
                                    (u32(payload, 8) == 0x60 || u32(payload, 8) == 0x62),
                                "Unsupported default IPU picture");
                        std::size_t p = 16;
                        const std::uint8_t terminator[] = {0, 0, 1, 0xb0};
                        av_check(av_frame_make_writable(assembled.value), "Reuse picture planes");
                        for (unsigned row = 0; row < height / 16; ++row) {
                            auto end = std::search(payload.begin() + p, payload.end(), std::begin(terminator),
                                                   std::end(terminator));
                            require(end != payload.end(), "Missing movie strip terminator");
                            auto length = std::size_t(end - payload.begin()) + 4 - p;
                            Bytes packet = {std::uint8_t(u32(payload, 8))};
                            append(packet, slice(payload, p, length));
                            p += length;
                            decode(strip, packet, picture.value);
                            require(picture.value->format == AV_PIX_FMT_YUV420P,
                                    "Unexpected IPU picture format");
                            for (unsigned c = 0; c < 3; ++c) {
                                unsigned w = c ? width / 2 : width, h = c ? 8 : 16;
                                for (unsigned y = 0; y < h; ++y)
                                    std::copy_n(picture.value->data[c] + y * picture.value->linesize[c], w,
                                                assembled.value->data[c] +
                                                    (row * h + y) * assembled.value->linesize[c]);
                            }
                            av_frame_unref(picture.value);
                        }
                        require(std::all_of(payload.begin() + p, payload.end(),
                                            [](auto value) { return !value; }),
                                "Nonzero picture padding");
                        decoded = assembled.value;
                    }
                }
                require(decoded->width == int(width) && decoded->height == int(height),
                        "Movie picture dimensions changed");
                av_check(av_frame_make_writable(rgb.value), "Reuse RGB picture");
                to_rgb.convert(decoded, rgb.value);
                for (unsigned y = 0; y < height; ++y)
                    rgb_hash.add(View(rgb.value->data[0] + y * rgb.value->linesize[0], width * 3));
                av_check(av_frame_make_writable(encoded.value), "Reuse encoded picture");
                to_yuv.convert(rgb.value, encoded.value);
                encoded.value->pts = number;
                write_encoded(mux, video, video_stream, encoded.value);
                av_frame_unref(picture.value);
            }
            encode_audio(std::uint64_t(number + 1) * 1600);
        }
        encode_audio(samples);
        write_encoded(mux, video, video_stream, nullptr);
        if (audio)
            write_encoded(mux, *audio, audio_stream, nullptr);
        av_check(av_write_trailer(mux.value), "Finish MP4 output");
    }
    unsigned margin = aperture(movie, width, height);
    std::string descriptor = "lifeline-movie-5\n" + std::to_string(width) + " " + std::to_string(height) +
                             " 30 " + std::to_string(frames.size()) + " " + std::to_string(video_count) +
                             " " + std::to_string(channels ? 48000 : 0) + " " + std::to_string(channels) +
                             " " + std::to_string(samples) + " " + std::to_string(channels ? 1024 : 0) + "\n";
    write_text(output / wide(name + ".txt"), descriptor);
    write_text(output / wide(name + ".aperture"), "lifeline-aperture-1\n" + std::to_string(width) + " " +
                                                      std::to_string(height) + " 0 " +
                                                      std::to_string(margin) + " " + std::to_string(width) +
                                                      " " + std::to_string(height - margin * 2) + "\n");
    Json metadata = {{"version", 4},
                     {"source", archive + (member.empty() ? "" : ":" + member)},
                     {"sha256", sha256(source)},
                     {"converter", "native-ffmpeg-minimal-8.1.3-mf"},
                     {"video", "H.264 High, YUV420, Windows quality 100, 30 fps, no B frames"},
                     {"audio", channels ? "AAC 320 kbit/s stereo, 48 kHz" : "none"},
                     {"width", width},
                     {"height", height},
                     {"timeline_frames", frames.size()},
                     {"video_frames", video_count},
                     {"audio_samples", samples},
                     {"audio_priming_samples", channels ? 1024 : 0},
                     {"media_bytes", fs::file_size(movie)},
                     {"media_sha256", file_sha256(movie)},
                     {"source_rgb24_sha256", rgb_hash.finish()},
                     {"source_pcm16_sha256", sha256(pcm)}};
    write_text(output / wide(name + ".json"), metadata.dump(2) + "\n");
}
} // namespace
void lifeline::setup::convert_movie(const fs::path &iso, const fs::path &output, const std::string &name,
                                  const std::string &archive, const std::string &member) {
    av_log_set_level(AV_LOG_ERROR);
    convert(iso, output, name, archive, member);
}
