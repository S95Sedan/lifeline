#include "converters.hpp"
#include <cmath>
#include <cstdio>
#include <sstream>
#include <set>

namespace lifeline::setup {
namespace {
std::uint8_t at(View data, std::size_t p) { return slice(data, p, 1)[0]; }
int signed16(std::uint16_t value) { return std::int16_t(value); }
std::string sample_name(unsigned value) {
    char name[32];
    std::snprintf(name, sizeof(name), "samples/%03u.wav", value);
    return name;
}
std::string decimal(std::uint32_t v) { return std::to_string(std::int32_t(v)); }
std::string real(float value) {
    require(std::isfinite(value), "Invalid authored floating point value");
    char result[80];
    std::snprintf(result, sizeof(result), "%.9g", double(value));
    return result;
}
struct Rows {
    std::string text;
    template <class... T> void row(const T &...values) {
        bool first = true;
        auto one = [&](const auto &value) {
            if (!first)
                text += '\t';
            first = false;
            std::ostringstream out;
            out << value;
            text += out.str();
        };
        (one(values), ...);
        text += '\n';
    }
    void values(std::string_view tag, View data) {
        text += tag;
        for (auto value : data)
            text += '\t' + std::to_string(unsigned(value));
        text += '\n';
    }
};
std::string sjis(View data) {
    auto end = std::find(data.begin(), data.end(), 0);
    int size = int(end - data.begin());
    if (!size)
        return {};
    int count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(data.data()),
                                    size, nullptr, 0);
    require(count > 0, "Invalid Shift-JIS game text");
    std::wstring result(count, 0);
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(data.data()), size,
                        result.data(), count);
    return utf8(result);
}
std::string escaped(std::string_view text) {
    std::string result;
    for (auto c : text) {
        if (c == '\\')
            result += "\\\\";
        else if (c == '\t')
            result += "\\t";
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else
            result += c;
    }
    return result;
}
Bytes rgba_bitmap(View data, unsigned w, unsigned h) {
    require(w && h && data.size() == std::uint64_t(w) * h * 4, "Invalid RGBA texture extent");
    Bytes output = {'B', 'M'};
    put32(output, 122 + std::uint32_t(data.size()));
    put32(output, 0);
    put32(output, 122);
    put32(output, 108);
    put32(output, w);
    put32(output, h);
    put16(output, 1);
    put16(output, 32);
    put32(output, 3);
    put32(output, std::uint32_t(data.size()));
    for (int i = 0; i < 4; ++i)
        put32(output, 0);
    for (auto value : {0xff0000u, 0xff00u, 0xffu, 0xff000000u, 0x73524742u})
        put32(output, value);
    output.resize(122, 0);
    for (unsigned y = h; y-- > 0;)
        for (unsigned x = 0; x < w; ++x) {
            auto p = (std::size_t(y) * w + x) * 4;
            output.push_back(data[p + 2]);
            output.push_back(data[p + 1]);
            output.push_back(data[p]);
            output.push_back(data[p + 3]);
        }
    return output;
}
Bytes effect_bitmap(View data, unsigned w, unsigned h) {
    require(w && h && data.size() == std::uint64_t(w) * h * 2, "Invalid RGB16 texture extent");
    Bytes output = {'B', 'M'};
    put32(output, 54 + w * h * 4);
    put32(output, 0);
    put32(output, 54);
    put32(output, 40);
    put32(output, w);
    put32(output, h);
    put16(output, 1);
    put16(output, 32);
    put32(output, 0);
    put32(output, w * h * 4);
    for (int i = 0; i < 4; ++i)
        put32(output, 0);
    for (unsigned y = h; y-- > 0;)
        for (unsigned x = 0; x < w; ++x) {
            auto c = u16(data, (std::size_t(y) * w + x) * 2);
            output.push_back(std::uint8_t(((c >> 10) & 31) * 255 / 31));
            output.push_back(std::uint8_t(((c >> 5) & 31) * 255 / 31));
            output.push_back(std::uint8_t((c & 31) * 255 / 31));
            output.push_back(255);
        }
    return output;
}
Bytes index_bitmap(View data, unsigned w, unsigned h) {
    require(w && h && data.size() <= std::uint64_t(w) * h, "Invalid indexed texture extent");
    unsigned stride = (w + 3) & ~3u, offset = 1078;
    Bytes output = {'B', 'M'};
    put32(output, offset + stride * h);
    put32(output, 0);
    put32(output, offset);
    put32(output, 40);
    put32(output, w);
    put32(output, h);
    put16(output, 1);
    put16(output, 8);
    put32(output, 0);
    put32(output, stride * h);
    put32(output, 2835);
    put32(output, 2835);
    put32(output, 256);
    put32(output, 256);
    for (unsigned i = 0; i < 256; ++i) {
        output.push_back(std::uint8_t(i));
        output.push_back(std::uint8_t(i));
        output.push_back(std::uint8_t(i));
        output.push_back(0);
    }
    for (unsigned y = h; y-- > 0;)
        for (unsigned x = 0; x < stride; ++x) {
            auto p = std::size_t(y) * w + x;
            output.push_back(x < w && p < data.size() ? data[p] : 0);
        }
    return output;
}
constexpr int coefficients[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
void block(View data, std::size_t p, int &previous, int &older, Bytes &samples) {
    auto part = slice(data, p, 16);
    unsigned shift = part[0] & 15, predictor = part[0] >> 4;
    require(shift <= 12 && predictor <= 4 && part[1] <= 7, "Invalid authored ADPCM block");
    for (unsigned i = 0; i < 28; ++i) {
        int nibble = (part[2 + i / 2] >> ((i & 1) * 4)) & 15;
        if (nibble >= 8)
            nibble -= 16;
        int value = std::clamp(
            ((nibble * 4096) >> shift) +
                ((previous * coefficients[predictor][0] + older * coefficients[predictor][1] + 32) >> 6),
            -32768, 32767);
        older = previous;
        previous = value;
        put16(samples, std::uint16_t(std::int16_t(value)));
    }
}
} // namespace
Pcm decode_adpcm(View data, std::size_t offset, bool stop) {
    Pcm result;
    int previous = 0, older = 0;
    require(offset <= data.size(), "Sample offset is invalid");
    for (std::size_t p = offset; p + 16 <= data.size(); p += 16) {
        if (data[p + 1] & 4)
            result.loop = unsigned(result.samples.size() / 2);
        block(data, p, previous, older, result.samples);
        if (stop && (data[p + 1] & 1)) {
            result.repeating = bool(data[p + 1] & 2);
            return result;
        }
    }
    require(!stop, "Unterminated sound-bank sample");
    return result;
}
Bytes wave(View pcm, unsigned channels, unsigned rate) {
    require(channels && channels <= 16 && rate <= 192000 && pcm.size() <= 0xffffffd0u, "Invalid PCM extent");
    Bytes result = bytes("RIFF");
    put32(result, 36 + std::uint32_t(pcm.size()));
    append(result, bytes("WAVEfmt "));
    put32(result, 16);
    put16(result, 1);
    put16(result, std::uint16_t(channels));
    put32(result, rate);
    put32(result, rate * channels * 2);
    put16(result, std::uint16_t(channels * 2));
    put16(result, 16);
    append(result, bytes("data"));
    put32(result, std::uint32_t(pcm.size()));
    append(result, pcm);
    return result;
}
Bytes stream_pcm(View data, unsigned channels) {
    require(channels &&
                (channels == 1 || channels == 2 || channels == 4 || channels == 8 || channels == 16) &&
                data.size() % 2048 == 0,
            "Invalid streamed audio extent");
    std::vector<Bytes> pcm(channels);
    std::vector<int> previous(channels), older(channels);
    unsigned stride = 2048 / channels;
    for (std::size_t sector = 0; sector < data.size(); sector += 2048)
        for (unsigned c = 0; c < channels; ++c)
            for (unsigned p = 0; p < stride; p += 16)
                block(data, sector + c * stride + p, previous[c], older[c], pcm[c]);
    Bytes result;
    result.reserve(pcm[0].size() * channels);
    for (std::size_t i = 0; i < pcm[0].size(); i += 2)
        for (unsigned c = 0; c < channels; ++c) {
            result.push_back(pcm[c][i]);
            result.push_back(pcm[c][i + 1]);
        }
    return result;
}
Converted voice_bank(View directory, View stream) {
    require(directory.size() >= 16, "Truncated voice bank");
    bool streamed = u32(directory, 0) == 0x20327673 && u32(directory, 4) == 0x20000;
    require(streamed || (u32(directory, 0) == 0x20207673 && u32(directory, 4) == 0x10000),
            "Unsupported voice bank");
    unsigned count = u32(directory, 8), start = u32(directory, 12), stride = streamed ? 32 : 16;
    require(count && count <= 4096 && start >= 16 + count * stride && start <= directory.size(),
            "Invalid voice bank directory");
    Converted result;
    for (unsigned i = 0; i < count; ++i) {
        auto p = 16 + i * stride;
        auto id = u32(directory, p), offset = u32(directory, p + 4), metadata = u32(directory, p + 8),
             size = u32(directory, p + 12);
        if (streamed && (!offset || !size))
            continue;
        unsigned prefix = size, rate = 13230, sector = 0;
        if (streamed) {
            prefix = u32(directory, p + 16);
            sector = u32(directory, p + 20);
            rate = u32(directory, p + 24);
            require(prefix <= size && prefix % 16 == 0 && rate && rate <= 192000 && !u32(directory, p + 28),
                    "Invalid streamed voice entry");
        }
        require(offset >= start && size && size % 16 == 0, "Invalid voice sample");
        Bytes encoded;
        append(encoded, slice(directory, offset, prefix));
        if (prefix < size)
            append(encoded, slice(stream, std::uint64_t(sector) * 2048, size - prefix));
        // Voice banks can end at the directory extent without an end flag.
        int previous = 0, older = 0;
        Bytes pcm;
        for (std::size_t n = 0; n < encoded.size(); n += 16) {
            block(encoded, n, previous, older, pcm);
            if (encoded[n + 1] & 1)
                break;
        }
        auto output = wave(pcm, 1, rate);
        if (metadata) {
            auto n = u32(directory, metadata), blend = u32(directory, metadata + 4);
            require(n >= 2 && n <= 65536 && blend && !u32(directory, metadata + 8) &&
                        !u32(directory, metadata + 12),
                    "Invalid voice facial track");
            Bytes track;
            put32(track, 1);
            put32(track, n);
            put32(track, blend);
            for (unsigned j = 0; j < n; ++j) {
                auto t = metadata + 16 + j * 20;
                auto mouth = u32(directory, t), expression = u32(directory, t + 4),
                     duration = u32(directory, t + 8);
                require(mouth <= 7 && expression <= 255 && duration && !u32(directory, t + 12) &&
                            std::isfinite(f32(directory, t + 16)),
                        "Invalid facial pose");
                put32(track, expression * 8);
                put32(track, expression * 8 + mouth);
                put32(track, duration);
                put32(track, u32(directory, t + 16));
            }
            append(output, bytes("lfac"));
            put32(output, std::uint32_t(track.size()));
            append(output, track);
            auto length = std::uint32_t(output.size() - 8);
            for (unsigned j = 0; j < 4; ++j)
                output[4 + j] = std::uint8_t(length >> (j * 8));
        }
        require(result.emplace(std::to_string(id) + ".wav", std::move(output)).second, "Duplicate voice ID");
    }
    return result;
}
Converted sound_bank(View hd, View bd) {
    require(hd.size() >= 128 && fixed_string(hd, 12, 4) == "SShd" && u32(hd, 0) >= 128 &&
                u32(hd, 0) <= hd.size() && u32(hd, 4) == bd.size() && u32(hd, 16) == 0xffffffff,
            "Unsupported keyed sound bank");
    Rows rows;
    rows.row("LIFELINE_SOUND_BANK", 1);
    rows.row("master", unsigned(at(hd, u32(hd, 32))));
    auto velocity = u32(hd, 20);
    Bytes values;
    if (velocity == 0xffffffff) {
        for (unsigned i = 0; i < 128; ++i)
            values.push_back(std::uint8_t(i));
    } else {
        auto v = slice(hd, velocity + 2, 128);
        values.assign(v.begin(), v.end());
    }
    require(*std::max_element(values.begin(), values.end()) <= 127, "Invalid velocity map");
    rows.values("velocity", values);
    auto lfo = u32(hd, 24);
    if (lfo != 0xffffffff) {
        unsigned count = u16(hd, lfo) + 1;
        require(count <= 256, "Invalid LFO count");
        for (unsigned i = 0; i < count; ++i) {
            rows.text += "lfo\t" + std::to_string(i);
            for (auto v : slice(hd, lfo + u16(hd, lfo + 2 + i * 2), 60))
                rows.text += '\t' + std::to_string(unsigned(v));
            rows.text += '\n';
        }
    }
    Converted result;
    std::map<unsigned, unsigned> samples;
    std::map<unsigned, std::pair<unsigned, unsigned>> keys;
    auto base = u32(hd, 36);
    unsigned count = u16(hd, base) + 1;
    require(count <= 128, "Too many effect programs");
    for (unsigned i = 0; i < count; ++i) {
        auto off = u16(hd, base + 2 + i * 2);
        if (off == 0xffff)
            continue;
        auto p = base + off;
        require(at(hd, p) == 255, "Expected keyed effect program");
        unsigned first = at(hd, p + 6), last = at(hd, p + 7);
        require(first <= last && last <= 127, "Invalid effect key range");
        keys[i] = {first, last};
        rows.row("program", i, unsigned(at(hd, p + 1)), unsigned(at(hd, p + 2)), unsigned(at(hd, p + 4)));
        for (unsigned key = first; key <= last; ++key) {
            auto t = p + 8 + (key - first) * 16;
            unsigned offset = u16(hd, t + 4) * 8;
            if (!samples.contains(offset)) {
                unsigned id = unsigned(samples.size());
                samples[offset] = id;
                auto pcm = decode_adpcm(bd, offset);
                auto name = sample_name(id);
                result[name] = wave(pcm.samples, 1, 48000);
                rows.row("sample", id, name, pcm.loop, int(pcm.repeating));
            }
            rows.row("tone", i, key, samples.at(offset), unsigned(at(hd, t + 2)),
                     int(std::int8_t(at(hd, t + 3))), u16(hd, t + 6), u16(hd, t + 8),
                     unsigned(at(hd, t + 10)), unsigned(at(hd, t + 11)), unsigned(at(hd, t + 12)),
                     unsigned(at(hd, t + 13)), unsigned(at(hd, t + 14)), unsigned(at(hd, t + 15)));
        }
    }
    base = u32(hd, 28);
    unsigned groups = u16(hd, base) + 1;
    require(groups <= 128, "Too many effect groups");
    for (unsigned group = 0; group < groups; ++group) {
        auto offset = u16(hd, base + 2 + group * 2);
        if (offset == 0xffff)
            continue;
        auto p = base + (offset & ~1u);
        unsigned sequences = u16(hd, p) + 1;
        require(sequences <= 4096, "Too many sound sequences");
        for (unsigned sequence = 0; sequence < sequences; ++sequence) {
            offset = u16(hd, p + 2 + sequence * 2);
            if (offset == 0xffff)
                continue;
            unsigned start = base + offset, cursor = start;
            std::map<unsigned, unsigned> positions;
            std::vector<std::vector<std::string>> events;
            bool ended = false;
            for (unsigned iteration = 0; iteration < 10000; ++iteration) {
                positions[cursor - start] = unsigned(events.size());
                auto status = at(hd, cursor);
                if (status == 255 && at(hd, cursor + 1) == 0x2f && at(hd, cursor + 2) == 0) {
                    events.push_back({"end"});
                    ended = true;
                    break;
                }
                std::vector<std::string> event;
                auto number = [&](unsigned p) { return std::to_string(unsigned(at(hd, p))); };
                if (status == 0xa0) {
                    unsigned key = at(hd, cursor + 1), vel = at(hd, cursor + 2), program = at(hd, cursor + 3);
                    require(keys.contains(program) && keys.at(program).first <= key &&
                                key <= keys.at(program).second && vel <= 127,
                            "Invalid keyed sound event");
                    event = {"note", number(cursor + 1), number(cursor + 2), number(cursor + 3)};
                    cursor += 4;
                } else if (status == 0xb0 && at(hd, cursor + 1) == 96) {
                    event = {"jump", std::to_string(u16(hd, cursor + 2)), number(cursor + 4)};
                    cursor += 5;
                } else if (status == 0xb0 && (at(hd, cursor + 1) == 1 || at(hd, cursor + 1) == 2)) {
                    event = {at(hd, cursor + 1) == 1 ? "vibrato_depth" : "vibrato_rate", number(cursor + 2),
                             number(cursor + 3), number(cursor + 4)};
                    cursor += 5;
                } else if (status == 0xb0 && at(hd, cursor + 1) == 7) {
                    event = {"volume_ramp", number(cursor + 2), number(cursor + 3), number(cursor + 4),
                             number(cursor + 5)};
                    cursor += 6;
                } else if (status == 0xb0 && at(hd, cursor + 1) == 65) {
                    event = {"pitch_slide", number(cursor + 2),
                             std::to_string(int(std::int8_t(at(hd, cursor + 3)))), number(cursor + 4),
                             number(cursor + 5)};
                    cursor += 6;
                } else
                    throw std::runtime_error("Unsupported keyed sound event");
                unsigned delay = 0;
                bool complete = false;
                for (unsigned j = 0; j < 4; ++j) {
                    auto value = at(hd, cursor++);
                    delay = (delay << 7) | (value & 127);
                    if (!(value & 128)) {
                        complete = true;
                        break;
                    }
                }
                require(complete, "Invalid effect delay");
                event.push_back(std::to_string(delay));
                events.push_back(std::move(event));
            }
            require(ended, "Unterminated keyed sequence");
            rows.row("sequence", group, sequence);
            for (auto &event : events) {
                if (event[0] == "jump") {
                    auto offset_ = unsigned(std::stoul(event[1]));
                    require(positions.contains(offset_), "Invalid effect jump");
                    event[1] = std::to_string(positions.at(offset_));
                }
                for (std::size_t j = 0; j < event.size(); ++j) {
                    if (j)
                        rows.text += '\t';
                    rows.text += event[j];
                }
                rows.text += '\n';
            }
        }
    }
    result["bank.tsv"] = bytes(rows.text);
    return result;
}
Converted music_bank(View sq, View hd, View bd, int version) {
    require(hd.size() >= 128 && u32(hd, 0) == hd.size() && u32(hd, 4) == bd.size() &&
                fixed_string(hd, 12, 4) == "SShd" && fixed_string(sq, 12, 4) == "SSsq",
            "Unsupported music bank");
    auto volume = u16(sq, 0), ppqn = u16(sq, 2);
    unsigned tempo = u16(sq, 4);
    require(volume <= 127 && ppqn && ppqn <= 4096 && tempo && tempo <= 1000, "Invalid music timing");
    Rows rows;
    Converted result;
    std::map<unsigned, unsigned> samples;
    auto velocity = u32(hd, 20);
    Bytes values;
    if (velocity == 0xffffffff) {
        for (unsigned i = 0; i < 128; ++i)
            values.push_back(std::uint8_t(i));
    } else {
        auto v = slice(hd, velocity + 2, 128);
        values.assign(v.begin(), v.end());
    }
    require(*std::max_element(values.begin(), values.end()) <= 127, "Invalid music velocity map");
    rows.values("velocity", values);
    for (unsigned channel = 0; channel < 16; ++channel) {
        auto p = 16 + channel * 16;
        require(version != 2 || !at(sq, p + 9), "Music modulation requires native LFO");
        rows.row("channel", channel, unsigned(at(sq, p + 2)), unsigned(at(sq, p + 3)),
                 unsigned(at(sq, p + 4)), unsigned(at(sq, p + 10)));
    }
    auto base = u32(hd, 16);
    unsigned count = u16(hd, base) + 1;
    require(count <= 128, "Too many music programs");
    for (unsigned index = 0; index < count; ++index) {
        auto offset = u16(hd, base + 2 + index * 2);
        if (offset == 0xffff) {
            require(version == 2, "Sparse version 1 music program");
            rows.row("program", index, 0, 64, 0, 0);
            continue;
        }
        auto p = base + offset;
        require(at(hd, p) != 255 && at(hd, p + 1) <= 127 && at(hd, p + 2) <= 127,
                "Unsupported music program");
        rows.row("program", index, unsigned(at(hd, p + 1)), unsigned(at(hd, p + 2)), unsigned(at(hd, p + 4)),
                 int(bool(at(hd, p) & 128)));
        for (unsigned j = 0; j < unsigned(at(hd, p) & 127) + 1; ++j) {
            auto t = p + 8 + j * 16;
            unsigned offset_ = u16(hd, t + 4) * 8;
            if (!samples.contains(offset_)) {
                unsigned id = unsigned(samples.size());
                samples[offset_] = id;
                auto pcm = decode_adpcm(bd, offset_);
                auto name = sample_name(id);
                result[name] = wave(pcm.samples, 1, 48000);
                rows.row("sample", id, name, pcm.loop, int(pcm.repeating));
            }
            auto low = at(hd, t), high = at(hd, t + 1), root = at(hd, t + 2), vol = at(hd, t + 11),
                 pan = at(hd, t + 12), bend = at(hd, t + 13), flags = at(hd, t + 15);
            require(low <= high && high <= 127 && root <= 127 && vol <= 127 && pan <= 127 &&
                        !at(hd, t + 10) && at(hd, t + 14) == 127 && !(flags & 0x60),
                    "Unsupported music tone");
            rows.row("tone", index, unsigned(low), unsigned(high), unsigned(root), samples.at(offset_),
                     unsigned(vol), unsigned(pan), unsigned(bend), unsigned(flags), u16(hd, t + 6),
                     u16(hd, t + 8), int(std::int8_t(at(hd, t + 3))));
        }
    }
    unsigned p = 0x110, status = 0, position = 0;
    double frame = 0;
    bool loop_start = false, loop_end = false, ended = false;
    for (unsigned iteration = 0; iteration < 100000; ++iteration) {
        if (at(sq, p) & 128)
            status = at(sq, p++);
        auto kind = status & 0xf0;
        unsigned a = at(sq, p++);
        position = unsigned(std::floor(frame + .5));
        if (status == 255) {
            if (a == 0x2f) {
                ended = true;
                break;
            }
            require(a == 0x51, "Unsupported music meta event");
            tempo = u16(sq, p);
            p += 2;
            require(tempo && tempo <= 1000, "Invalid tempo event");
        } else if (kind == 0x80 || kind == 0x90 || kind == 0xb0) {
            unsigned b = at(sq, p++);
            require(a <= 127 && b <= 127, "Invalid music event");
            if (kind == 0xb0) {
                if (a == 99) {
                    if (b == 20 && (position == 0 || version == 2))
                        loop_start = true;
                    else if (b == 30)
                        loop_end = true;
                    else
                        throw std::runtime_error("Unsupported music loop");
                } else
                    require(a == 6 || a == 7 || a == 10 || (version == 2 && (a == 1 || a == 2)),
                            "Unsupported music controller");
                require(a != 1 || b == 0, "Music modulation requires LFO");
            }
            rows.row("event", position, status, a, b);
        } else if (kind == 0xc0 || kind == 0xe0) {
            require(a <= 127, "Invalid program or pitch");
            rows.row("event", position, status, a, 0);
        } else
            throw std::runtime_error("Unsupported music event");
        unsigned delta = 0;
        bool complete = false;
        for (unsigned j = 0; j < 4; ++j) {
            auto value = at(sq, p++);
            delta = (delta << 7) | (value & 127);
            if (!(value & 128)) {
                complete = true;
                break;
            }
        }
        require(complete, "Invalid music event delta");
        frame += double(delta) * 48000 * 60 / (ppqn * tempo);
        require(frame <= 48000 * 60 * 20, "Music duration exceeds limit");
    }
    require(ended && position && (version == 2 || (loop_start && loop_end)), "Incomplete music sequence");
    Rows output;
    output.row("LIFELINE_MUSIC", version);
    output.row(version == 1 ? "loop" : "length", position, volume);
    output.text += rows.text;
    result["sequence.tsv"] = bytes(output.text);
    return result;
}
Bytes normalization(View elf) {
    require(fixed_string(elf, 1, 3) == "ELF" && at(elf, 4) == 1 && at(elf, 5) == 1, "Expected 32-bit ELF");
    auto table = u32(elf, 28);
    auto stride = u16(elf, 42), count = u16(elf, 44);
    require(stride == 32 && count <= 128, "Invalid ELF segments");
    auto read = [&](unsigned address, unsigned size) {
        for (unsigned i = 0; i < count; ++i) {
            auto p = table + i * stride;
            if (u32(elf, p) != 1)
                continue;
            auto off = u32(elf, p + 4), base = u32(elf, p + 8), length = u32(elf, p + 16);
            if (base <= address && std::uint64_t(address) + size <= std::uint64_t(base) + length)
                return slice(elf, off + address - base, size);
        }
        throw std::runtime_error("Speech table outside ELF");
    };
    Bytes result = bytes("LSN1");
    put32(result, 1);
    put32(result, 21);
    put32(result, 10);
    for (unsigned i = 0; i < 3; ++i) {
        auto descriptor = read(0x341e68 + i * 8, 8);
        auto rows = u16(descriptor, 4), columns = u16(descriptor, 6);
        require(rows == (i == 2 ? 1 : 21) && columns == 10, "Unsupported speech projection");
        append(result, read(u32(descriptor, 0), rows * columns * 4));
    }
    append(result, read(0x340cb8, 21 * 4));
    append(result, read(0x340d10, 21 * 4));
    append(result, read(0x340d88, 353 * 4));
    return result;
}
Converted font(View data) {
    require(data.size() >= 0x150, "Truncated game font");
    unsigned end = u16(data, 0) * 16 + 16, w = u32(data, 0x120), h = u32(data, 0x124), cell = u16(data, 0x3c),
             columns = u16(data, 0x3e);
    require(fixed_string(data, end, 8) == "bin 1.00" && w == 1024 && h == 440 && cell == 22 &&
                columns == 46 && end == 0x150 + w * h / 2,
            "Unsupported font atlas");
    unsigned first = u32(data, end + 8), count = u32(data, end + 12);
    require(first == 0x8140 && count == 1832 && (data.size() - end - 128) % 2 == 0, "Invalid font table");
    std::uint8_t palette[32];
    for (unsigned i = 0; i < 32; ++i)
        palette[i] = at(data, 0x73 + i * 4);
    for (unsigned i = 0; i < 8; ++i)
        std::swap(palette[8 + i], palette[16 + i]);
    Bytes pixels(std::size_t(w) * h * 8);
    for (unsigned i = 0; i < w * h / 2; ++i) {
        auto value = at(data, 0x150 + i);
        for (unsigned half = 0; half < 2; ++half) {
            auto index = (value >> (half * 4)) & 15;
            for (unsigned plane = 0; plane < 2; ++plane) {
                auto p = (std::size_t(i * 2 + half) + plane * w * h) * 4;
                pixels[p] = pixels[p + 1] = pixels[p + 2] = 255;
                pixels[p + 3] = std::uint8_t(std::min(255u, unsigned(palette[index + plane * 16]) * 2));
            }
        }
    }
    Rows rows;
    rows.row("LIFELINE_FONT", 1);
    rows.row("size", cell);
    for (std::size_t p = end + 128; p < data.size(); p += 2) {
        unsigned code = first + unsigned((p - end - 128) / 2), packed = u16(data, p);
        if (!packed && code != first)
            continue;
        unsigned x = (packed >> 8) * cell, y = ((packed & 126) >> 1) * cell + (packed & 1) * h;
        require(x + cell <= w && y + cell <= h * 2, "Font glyph outside atlas");
        rows.row("glyph", code, x, y);
    }
    return {{"atlas.bmp", rgba_bitmap(pixels, w, h * 2)}, {"font.tsv", bytes(rows.text)}};
}
Bytes convert_member(const std::string &conversion, View data, const Json &record) {
    if (conversion == "raw")
        return Bytes(data.begin(), data.end());
    if (conversion == "effect-bitmap-v1")
        return effect_bitmap(data, record.at("width"), record.at("height"));
    if (conversion == "rgba-bitmap-v1")
        return rgba_bitmap(data, record.at("width"), record.at("height"));
    if (conversion == "effect-index-bitmap-v1")
        return index_bitmap(data, record.at("width"), record.at("height"));
    if (conversion == "speech-vocabulary-v1") {
        unsigned length = u32(data, 0), count = u32(data, 4);
        require(length >= 8 && length <= data.size() && count && count <= 4096, "Invalid speech word table");
        Rows rows;
        rows.row("LIFELINE_SPEECH_VOCABULARY", 1);
        rows.row("id", "kind", "text");
        std::size_t p = 8;
        unsigned spoken = 0;
        for (unsigned i = 0; i < count; ++i) {
            auto rest = slice(data, p, length - p);
            auto end = std::find(rest.begin(), rest.end(), 0);
            require(end != rest.end(), "Unterminated speech word");
            auto size = std::size_t(end - rest.begin());
            auto text = sjis(rest.first(size));
            bool control = text.empty() || text == ".S" || text == "...";
            if (!control)
                ++spoken;
            rows.row(i, control ? "control" : "spoken", text);
            p += size + 1;
        }
        require(p == length && spoken, "Invalid speech word count");
        return bytes(rows.text);
    }
    if (conversion == "motion-sounds-v1") {
        require(fixed_string(data, 0, 3) == "mse" && u32(data, 4) == 0x20020720,
                "Unsupported motion sound table");
        unsigned count = u32(data, 8);
        require(data.size() == 16ull + count * 8ull, "Invalid motion sound length");
        Rows rows;
        rows.row("LIFELINE_MOTION_SOUNDS", 1);
        for (unsigned i = 0; i < count; ++i) {
            auto p = 16 + i * 8;
            rows.row("cue", u16(data, p), u16(data, p + 2), u16(data, p + 4), int(u16(data, p + 6) != 0));
        }
        return bytes(rows.text);
    }
    if (conversion == "inventory-tsv-v1") {
        require(fixed_string(data, 0, 4) == "itd " && u32(data, 4) == 0x10000, "Unsupported inventory table");
        unsigned count = u32(data, 8), end = 16 + count * 32;
        require(end <= data.size(), "Truncated inventory");
        Rows rows;
        rows.row("LIFELINE_INVENTORY", 1);
        std::set<std::int32_t> ids;
        auto string = [&](unsigned off) {
            require(off >= end && off < data.size(), "Invalid inventory string offset");
            return escaped(sjis(data.subspan(off)));
        };
        for (unsigned i = 0; i < count; ++i) {
            auto p = 16 + i * 32;
            require(ids.insert(std::int32_t(u32(data, p))).second, "Duplicate inventory record");
            rows.row(decimal(u32(data, p)), decimal(u32(data, p + 4)), decimal(u32(data, p + 8)),
                     decimal(u32(data, p + 12)), decimal(u32(data, p + 24)), decimal(u32(data, p + 28)),
                     string(u32(data, p + 16)), string(u32(data, p + 20)));
        }
        return bytes(rows.text);
    }
    if (conversion == "interaction-tsv-v1") {
        require(fixed_string(data, 0, 4) == "ana " && u32(data, 4) == 0x10000,
                "Unsupported interaction table");
        unsigned count = u32(data, 8);
        require(count <= 65536 && 16ull + count * 4ull <= data.size(), "Truncated interaction offsets");
        Rows rows;
        rows.row("LIFELINE_INTERACTIONS", 1);
        for (unsigned i = 0; i < count; ++i) {
            auto p = u32(data, 16 + i * 4),
                 end = i + 1 < count ? u32(data, 20 + i * 4) : unsigned(data.size());
            require(p >= 16 + count * 4 && p + 56 <= end && end <= data.size(), "Invalid interaction record");
            rows.text += std::to_string(i);
            for (unsigned j = 0; j < 8; ++j)
                rows.text += '\t' + decimal(u32(data, p + j * 4));
            for (unsigned j = 0; j < 4; ++j)
                rows.text += '\t' + real(f32(data, p + 32 + j * 4));
            unsigned cursor = p + 56;
            for (unsigned j = 0; j < 2; ++j) {
                auto length = u32(data, p + 48 + j * 4);
                require(cursor + length <= end, "Invalid interaction string");
                rows.text += '\t' + escaped(sjis(slice(data, cursor, length)));
                cursor += length;
            }
            rows.text += '\n';
        }
        return bytes(rows.text);
    }
    if (conversion == "stream-wav-v1")
        return wave(stream_pcm(data, record.at("channels")), record.at("channels"), record.at("rate"));
    throw std::runtime_error("Unsupported native conversion: " + conversion);
}
} // namespace lifeline::setup
