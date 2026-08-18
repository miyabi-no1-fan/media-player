extern crate ffmpeg_next as ffmpeg;

use std::{
    io::Write,
    sync::{Arc, atomic},
    thread,
    time::Duration,
};

use cpal::traits::StreamTrait;
use minifb::{Key, KeyRepeat, Window};

use crate::{decode::Decoder, video::Video};

mod audio; // cpal
mod decode; // ffmpeg
mod video; // minifb

const WIDTH_LIMIT: usize = 15360;
const HEIGHT_LIMIT: usize = 8640;
const FPS_LIMIT: usize = 1000;

const DECODE_QUEUE_LEN: usize = 20;

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
    println!("--repeat | -r [n]    Repeat n times. Repeat forever if n is 0");
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
                    // repeat forever was a lie :)
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
            Ok(window) => window,
            Err(err) => match err {
                Error::Exit => break,
                err => {
                    eprintln!("{err:?}");
                    std::process::exit(1);
                }
            },
        };
    }
}

fn run(path: String, window: Option<Window>) -> Result<Option<Window>, Error> {
    let (decoder, fps, width, height) = Decoder::new(&path)?;

    let duration = decoder.duration();
    if duration == 0 {
        return Ok(window);
    }

    let mut is_video = fps.is_some() && width.is_some() && height.is_some();

    let fps = if is_video {
        Some(fps.unwrap().round() as usize) // <- remember to round here
    } else {
        None
    };

    // if we use bounded channel, the decoder would stuck waiting for
    // videos to be consumed while audio is empty
    let (video_prod, video_cons) = crossbeam_channel::unbounded();
    let (audio_prod, audio_cons) = crossbeam_channel::unbounded();

    // we'll run no audio if `audio_init` failed
    let (audio_stream, audio_config, audio_status) =
        match audio::audio_init(audio_cons.clone(), audio::InitOpts::Default) {
            Ok((a, b, c)) => (Some(a), Some(b), Some(c)),
            Err(_) => (None, None, None),
        };

    let is_audio = audio_stream.is_some() && audio_config.is_some() && audio_status.is_some();

    let handle = Decoder::decode(decoder, audio_config, Some(video_prod), Some(audio_prod));

    // waiting for the few first frame to be ready
    while (is_video && video_cons.len() <= DECODE_QUEUE_LEN)
        || (is_audio && audio_cons.len() <= DECODE_QUEUE_LEN)
    {
        thread::sleep(Duration::from_millis(10));
    }
    drop(audio_cons); // unused here so drop early

    // if window is some, use the window,
    // else, create a new window,
    // if create new window fail, fallback to no video.
    let mut window = match window {
        Some(window) => Some(window),
        None => {
            if is_video {
                if let Ok(window) =
                    video::new_window(path.as_str(), fps.unwrap(), width.unwrap(), height.unwrap())
                {
                    Some(window)
                } else {
                    is_video = false;
                    None
                }
            } else {
                None
            }
        }
    };

    let is_paused = Arc::new(atomic::AtomicBool::new(false));
    {
        let is_paused = is_paused.clone();
        thread::spawn(move || {
            let mut time = 0;
            while time <= duration {
                print!("\x1B[1A");
                print!("\x1B[2K");
                print!("\r");
                println!("{time} / {duration}");
                let _ = std::io::stdout().flush();
                thread::sleep(Duration::from_secs_f64(1.0));
                while is_paused.load(atomic::Ordering::Acquire) {
                    thread::sleep(Duration::from_millis(1));
                }
                time += 1;
            }
        });
    }

    if is_audio {
        audio_stream.as_ref().unwrap().play()?;
    }

    if is_video {
        let window = window.as_mut().unwrap();
        let mut video = Video::new(width.unwrap(), height.unwrap(), fps.unwrap(), video_cons);

        // update will exit if window is close or
        // if `video_cons` can't `recv` any frames more
        while video.update(window)? {
            for key in window.get_keys_pressed(KeyRepeat::No).iter() {
                match key {
                    Key::Space => {
                        video.is_paused = !video.is_paused;
                        is_paused.fetch_not(atomic::Ordering::AcqRel);
                        if is_audio {
                            audio_status
                                .as_ref()
                                .unwrap()
                                .fetch_not(atomic::Ordering::AcqRel);
                        }
                    }

                    // TODO: handle arrow keys for skip +-10s
                    _ => {}
                }
            }
        }
    }

    // join decoder thread to check for errors
    handle
        .join()
        .expect("Couldn't join on the associated thread")?;

    // wait until `audio_status` is false if audio is playing
    if is_audio {
        let status = audio_status.as_ref().unwrap();
        while status.load(atomic::Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(10));
        }
    }

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
