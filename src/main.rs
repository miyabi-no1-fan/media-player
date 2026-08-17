extern crate ffmpeg_next as ffmpeg;

use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use minifb::{Key, KeyRepeat, Window};

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
    /// indicate that the app should exit
    Exit,
}

fn help() {
    println!("media-player\nUsage: media-player [media file]");
    println!("Options:");
    println!("--help | -h          Print the this help description.");
    println!("--repeat | -r [v]    How many times to repeat the video, v must be > 0");
    println!("                     run again forever if v == 0");
}

fn main() {
    let mut path: Option<String> = None;
    let mut repeat = 1;

    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        help();
        std::process::exit(1);
    }

    let mut args = args[1..].iter().cloned();
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--help" | "-h" => {
                help();
                return;
            }
            "--repeat" | "-r" => {
                let Some(v) = args.next() else {
                    eprintln!("Invalid argument. Expect a positive number after {arg} flag");
                    std::process::exit(1);
                };
                let Ok(num) = v.parse::<u32>() else {
                    eprintln!("Invalid argument. Expect a positive number after {arg} flag");
                    std::process::exit(1);
                };
                if num == 0 {
                    // run again forever was a lie
                    repeat = usize::MAX;
                } else {
                    repeat = num as usize;
                }
            }
            _ if path.is_none() => path = Some(arg),
            _ => {
                eprintln!("Invalid argument. Unknown argument");
                help();
                std::process::exit(1);
            }
        }
    }

    let Some(path) = path else {
        eprintln!("Invalid argument. Expect a file to open.");
        std::process::exit(1);
    };

    let mut window = None;

    for _ in 0..repeat {
        window = match run(path.clone(), window) {
            Ok(window) => Some(window),
            Err(err) => match err {
                Error::Exit => break,
                err => {
                    eprintln!("{err:?}");
                    std::process::exit(1);
                }
            },
        }
    }
}

fn run(path: String, window: Option<Window>) -> Result<Window, Error> {
    // We're expecting both the audio and the video to exist.
    // TODO: Handle the audio/video only case.

    let (decoder, fps, width, height) = Decoder::new(&path)?;
    let fps = fps.round() as usize; // <- remember to round here

    // if we use bounded channel, the decoder would stuck waiting for
    // videos to be consumed while audio is empty
    let (video_prod, video_cons) = crossbeam_channel::unbounded();
    let (audio_prod, audio_cons) = crossbeam_channel::unbounded();

    let (audio_stream, audio_config) =
        audio::audio_init(audio_cons.clone(), audio::InitOpts::Default)?;

    let handle = Decoder::decode(decoder, audio_config, video_prod, audio_prod);

    // waiting for the few first frame to be ready
    while video_cons.len() < 10 || audio_cons.len() < 10 {
        thread::sleep(Duration::from_millis(1));
    }
    drop(audio_cons); // unused here so drop early

    let mut window = match window {
        Some(w) => w,
        None => video::new_window(path.as_str(), fps, width, height)?,
    };

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

    Ok(window)
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
            Self::Exit => panic!("Idiot, why would you print this error?"),
        }
    }
}
