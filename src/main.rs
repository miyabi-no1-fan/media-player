extern crate ffmpeg_next as ffmpeg;

use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use minifb::{Key, KeyRepeat};

use crate::{decode::Decoder, video::Video};

mod audio; // cpal
mod decode; // ffmpeg
mod video; // minifb

const WIDTH_LIMIT: usize = 15360;
const HEIGHT_LIMIT: usize = 8640;
const FPS_LIMIT: usize = 1000;

fn main() {
    // Currently this would panic on errors.
    // TODO: Handle the error properbly via error messages.

    // We're expecting both the audio and the video to exist.
    // TODO: Handle the audio/video only case.
    let path = std::env::args().nth(1).expect("Cannot open file.");
    let (decoder, fps, width, height) = Decoder::new(&path);
    let fps = fps.round() as usize; // <- remember to round here

    if width > WIDTH_LIMIT as u32 {
        eprintln!("Error: Video exceed width limit. Limit is {WIDTH_LIMIT}, found {width}");
        return;
    }

    if height > HEIGHT_LIMIT as u32 {
        eprintln!("Error: Video exceed height limit. Limit is {HEIGHT_LIMIT}, found {height}");
        return;
    }

    if fps > FPS_LIMIT {
        eprintln!("Error: Video exceed fps limit. Limit is {FPS_LIMIT}, found {fps}");
        return;
    }

    let mut window = video::new_window(path.as_str(), fps, width, height);

    // if we use bounded channel, the decoder would stuck waiting for
    // videos to be consumed while audio is empty
    let (video_prod, video_cons) = crossbeam_channel::unbounded();
    let (audio_prod, audio_cons) = crossbeam_channel::unbounded();

    let (audio_stream, audio_config) =
        audio::audio_init(audio_cons.clone(), audio::InitOpts::Default);

    Decoder::decode(decoder, audio_config, video_prod, audio_prod);

    // waiting for the few first frame to be ready
    while video_cons.len() < 10 || audio_cons.len() < 10 {
        thread::sleep(Duration::from_millis(1));
    }
    drop(audio_cons); // unused here so drop early

    audio_stream.play().unwrap();

    let mut video = Video::new(width, height, fps, video_cons);

    while video.update(&mut window) {
        for key in window.get_keys_pressed(KeyRepeat::No).iter() {
            match key {
                Key::Space => video.pause(&audio_stream),
                // TODO: handle arrow keys for skip +-10s
                _ => {}
            }
        }
        // TODO: Add progress bar.
        // On Hyprland, `minifb` window does not show the mouse cursor
        // So it's better to show the progress bar in the terminal instead.
    }
}
