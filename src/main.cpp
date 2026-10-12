#include <RtAudio.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "audio.hpp"
#include "decoder.hpp"
#include "ringbuf.hpp"
#include "video.hpp"

static constexpr char USAGE[] =
    "Usage: vplayer [options] <file>\n"
    "       vplayer --help\n"
    "\n"
    "Play video and audio of a media file.\n"
    "\n"
    "Options:\n"
    "    --help, -h              Show this message.\n";

struct Info {
    std::string path;
};

Info parse(int argc, const char* argv[]);

int main(int argc, const char* argv[]) try {
    Info info = parse(argc, argv);

    Decoder decoder(info.path.c_str());

    auto [video_sender, video_receiver] = RingBuf::bounded_channel<Decoder::Frame>(10);
    auto [audio_sender, audio_receiver] = RingBuf::bounded_channel<Decoder::Frame>(10);

    auto decode_thrd = decoder.launch(std::move(video_sender), std::move(audio_sender));

    std::unique_ptr<Audio> audio = nullptr;
    std::unique_ptr<Video> video = nullptr;

    if (decoder.has_audio()) {
        audio = std::make_unique<Audio>(
            *decoder.output_channels(),
            *decoder.output_sample_rate(),
            rt_audio_format(*decoder.output_sample_format()),
            std::move(audio_receiver));
    }

    if (decoder.has_video()) {
        video = std::make_unique<Video>(
            static_cast<std::uint32_t>(*decoder.output_width()),
            static_cast<std::uint32_t>(*decoder.output_height()),
            std::round(*decoder.fps()),
            "vplayer: " + info.path,
            std::move(video_receiver));
    }

    if (audio) audio->play();
    if (video) video->play();
    decode_thrd.detach();

    return 0;
} catch (std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
} catch (...) {
    std::cerr << "Unknown error\n";
    return 1;
}

void help() {
    std::cout << USAGE << "\n";
}

void error(const char* msg) {
    help();
    std::cerr << msg << "\n";
    std::exit(1);
}

Info parse(int argc, const char* argv[]) {
    if (argc < 2) {
        error("No input file found");
    }

    Info info = {};

    int i = 1;
    while (i < argc) {
        std::string arg(argv[i]);
        if (!arg.empty()) {
            if (arg.compare("--help") == 0 ||
                arg.compare("-h") == 0) {
                help();
                std::exit(0);
            }

            else if (info.path.empty()) {
                info.path = std::move(arg);
            }

            else {
                error(("Unknown argument: " + std::move(arg) + "\n").c_str());
            }
        }
        i++;
    }

    if (info.path.empty()) {
        error("No input file found");
    }

    return info;
}
