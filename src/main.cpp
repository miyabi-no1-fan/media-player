#include <RtAudio.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "audio.hpp"
#include "decoder.hpp"
#include "matrix.hpp"
#include "state.hpp"
#include "video.hpp"

static constexpr char USAGE[] =
    "Usage: vplayer [options] <file>\n"
    "       vplayer --help\n"
    "\n"
    "Play video and audio of a media file.\n"
    "\n"
    "Options:\n"
    "    --help, -h                     Show this message.\n"
    "    --loop [n]                     Loop the video for [n] times. If [n] < 0, loop forever\n"
    "    --rotate [a]                   Rotate the video by [a] degrees counter-clockwise\n"
    "    --flip horizontally | h        Flip the video horizontally\n"
    "           vertically   | v        Flip the video vertically\n";

struct Info {
    std::string path;
    int loop = 1;
    aglea::mat2x2 transformation = aglea::mat2x2::identity();
};

Info parse(int argc, const char* argv[]);

int main(int argc, const char* argv[]) try {
    Info info = parse(argc, argv);

    std::unique_ptr<Video> video = nullptr;

    for (int i = 0; i < info.loop; i++) {
        if (video) video->clear_state();

        Packet::init_channels();
        Frame::init_channels();

        Decoder decoder(info.path.c_str());
        std::thread handle = decoder.launch();

        std::unique_ptr<Audio> audio = nullptr;

        if (decoder.has_audio()) {
            audio = std::make_unique<Audio>(
                *decoder.output_channels(),
                *decoder.output_sample_rate(),
                rt_audio_format(*decoder.output_sample_format()));
        }

        if (decoder.has_video() && video == nullptr) {
            video = std::make_unique<Video>(
                static_cast<std::uint32_t>(*decoder.output_width()),
                static_cast<std::uint32_t>(*decoder.output_height()),
                std::round(*decoder.fps()),
                *decoder.duration_us(),
                "vplayer: " + info.path,
                info.transformation);
        }

        if (audio) audio->play();
        if (video) video->play();

        handle.join();
    }

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

            else if (arg.compare("--loop") == 0) {
                i++;
                std::istringstream iss(argv[i]);
                iss >> info.loop;
                if (!iss || !iss.eof()) {
                    throw std::invalid_argument("Unknown argument for --loop");
                }
                if (info.loop < 0) info.loop = std::numeric_limits<decltype(info.loop)>::max();
            }

            else if (arg.compare("--rotate") == 0) {
                i++;
                std::istringstream iss(argv[i]);
                double deg;
                iss >> deg;
                if (!iss || !iss.eof()) {
                    throw std::invalid_argument("Unknown argument for --rotate");
                }
                double rad = -deg * std::numbers::pi / 180.0;
                aglea::mat2x2 rotation = {
                    { std::cos(rad), -std::sin(rad) },
                    { std::sin(rad), std::cos(rad) },
                };
                info.transformation = rotation * info.transformation;
            }

            else if (arg.compare("--flip") == 0) {
                i++;
                std::string arg(argv[i]);
                if (arg.compare("horizontally") == 0 ||
                    arg.compare("h") == 0) {
                    info.transformation = aglea::mat2x2{
                        { -1, 0 },
                        { 0, 1 },
                    } * info.transformation;
                } else if (arg.compare("vertically") == 0 ||
                           arg.compare("v") == 0) {
                    info.transformation = aglea::mat2x2{
                        { 1, 0 },
                        { 0, -1 },
                    } * info.transformation;
                } else {
                    throw std::invalid_argument("Unknown argument for --flip");
                }
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
