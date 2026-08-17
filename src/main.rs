extern crate ffmpeg_next as ffmpeg;

use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use minifb::{Key, KeyRepeat};

use crate::{decode::Decoder, video::Video};

mod audio; // cpal
mod decode; // ffmpeg
mod video; // minifb

fn main() {
    let path = std::env::args().nth(1).expect("Cannot open file.");
    let (decoder, fps, width, height) = Decoder::new(&path);

    let fps = fps.round() as usize; // <- remember to round here
    let (width, height) = (width as usize, height as usize);

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
                _ => {}
            }
        }
    }
}
