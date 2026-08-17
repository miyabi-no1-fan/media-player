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

#[allow(dead_code)]
enum Error {
    Log(&'static str),
    Decode(ffmpeg::Error),
    Audio(cpal::Error),
    Window(minifb::Error),
}

fn main() {
    let path = match std::env::args().nth(1) {
        Some(v) => v,
        None => {
            eprintln!("Cannot open file");
            return;
        }
    };

    // TODO: parse input args for options like -h etc

    match run(path) {
        Ok(()) => {}
        Err(msg) => {
            eprintln!("{:?}", msg);
            return;
        }
    }
}

fn run(path: String) -> Result<(), Error> {
    // We're expecting both the audio and the video to exist.
    // TODO: Handle the audio/video only case.

    let (decoder, fps, width, height) = Decoder::new(&path)?;
    let fps = fps.round() as usize; // <- remember to round here

    // if we use bounded channel, the decoder would stuck waiting for
    // videos to be consumed while audio is empty
    let (video_prod, video_cons) = crossbeam_channel::unbounded();
    let (audio_prod, audio_cons) = crossbeam_channel::unbounded();

    let (audio_stream, audio_config) =
        audio::audio_init(audio_cons.clone(), audio::InitOpts::Best)?;

    let handle = Decoder::decode(decoder, audio_config, video_prod, audio_prod);

    // waiting for the few first frame to be ready
    while video_cons.len() < 10 || audio_cons.len() < 10 {
        thread::sleep(Duration::from_millis(1));
    }
    drop(audio_cons); // unused here so drop early

    let mut window = video::new_window(path.as_str(), fps, width, height)?;
    let mut video = Video::new(width, height, fps, video_cons);

    audio_stream.play()?;

    while video.update(&mut window)? {
        for key in window.get_keys_pressed(KeyRepeat::No).iter() {
            match key {
                Key::Space => video.pause(&audio_stream)?,

                // TODO: handle arrow keys for skip +-10s
                _ => {}
            }
        }
        // TODO: Add progress bar.
        // On Hyprland, `minifb` window does not show the mouse cursor.
        // So it's better to show the progress bar in the terminal instead.
    }

    handle
        .join()
        .expect("Couldn't join on the associated thread")?;

    Ok(())
}

impl From<ffmpeg::Error> for Error {
    fn from(value: ffmpeg::Error) -> Self {
        Self::Decode(value)
    }
}

impl From<cpal::Error> for Error {
    fn from(value: cpal::Error) -> Self {
        Self::Audio(value)
    }
}

impl From<minifb::Error> for Error {
    fn from(value: minifb::Error) -> Self {
        Self::Window(value)
    }
}

impl std::fmt::Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Log(msg) => write!(f, "Error: {msg}"),
            Self::Decode(e) => write!(f, "Decode Error: {e:?}"),
            Self::Window(e) => write!(f, "Window Error: {e:?}"),
            Self::Audio(e) => write!(f, "Audio Error: {e:?}"),
        }
    }
}
