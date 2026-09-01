// media-player TODOs:
// move window to main
// move decoder to main
// use channel to communicate between threads

extern crate ffmpeg_next as ffmpeg;

use std::{io::Write, thread, time::Duration};

use cpal::traits::StreamTrait;
use minifb::{Key, KeyRepeat, Window};

use crate::{decode::Decoder, video::Video};

mod audio; // cpal
mod decode; // ffmpeg
mod video; // minifb

const USAGE: &'static str = "
Usage: media-player [options] <file>
       media-player --help

Play video and audio of a media file.

Options:
    --help, -h              Show this message.
    --repeat [n], -r [n]    Plays in repeat [n] times, repeat forever if [n] is 0.
    --no-window             Don't open a window -- plays audio only.
    --rotate [deg]          Rotate the input video by [deg] degree.
";

const WIDTH_LIMIT: usize = 15360;
const HEIGHT_LIMIT: usize = 8640;
const FPS_LIMIT: usize = 1000;

const DEFAULT_FPS: usize = 60;
const DEFAULT_WIDTH: u32 = 1920;
const DEFAULT_HEIGHT: u32 = 1080;

const DECODE_PACKET_QUEUE_LEN: usize = 2;
const DECODE_VIDEO_QUEUE_LEN: usize = 2;
const DECODE_AUDIO_QUEUE_LEN: usize = 2;

const DEBUG: bool = false;

#[allow(dead_code)]
enum Error {
    Log(&'static str),
    Decode(ffmpeg::Error),
    Audio(cpal::Error),
    Window(minifb::Error),

    /// Restart(secs), recall `run` with skip = secs
    Restart(i64),

    /// indicate that the app should exit
    Exit,
}

fn help() {
    print!("{USAGE}");
}

fn main() {
    let mut path: Option<String> = None;
    let mut repeat = 1;
    let mut no_window = false;
    let mut rotate_deg = 0f32;

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
                    eprintln!("Invalid argument. Expect a positive integer after {arg} flag");
                    std::process::exit(1);
                };
                let Ok(num) = v.parse::<u32>() else {
                    eprintln!("Invalid argument. Expect a positive integer after {arg} flag");
                    std::process::exit(1);
                };
                if num == 0 {
                    // repeat forever was a lie :)
                    repeat = usize::MAX;
                } else {
                    repeat = num as usize;
                }
            }
            "--no-window" => no_window = true,
            "--rotate" => {
                let Some(v) = args.next() else {
                    eprintln!("Invalid argument. Expect a value after {arg} flag");
                    std::process::exit(1);
                };
                let Ok(num) = v.parse::<f32>() else {
                    eprintln!("Invalid argument. Expect a value after {arg} flag");
                    std::process::exit(1);
                };
                rotate_deg = num;
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

    let mut skip = None;

    let mut i: isize = 0;
    while (i as usize) < repeat {
        match run(
            path.clone(),
            &mut window,
            no_window,
            skip.unwrap_or(0),
            rotate_deg,
        ) {
            Ok(()) => {}
            Err(err) => match err {
                Error::Exit => break,
                Error::Restart(secs) => {
                    skip = Some(secs);
                    continue;
                }
                err => {
                    eprintln!("{err:?}");
                    std::process::exit(1);
                }
            },
        };
        skip = None;
        i += 1;
    }
}

fn run(
    path: String,
    window: &mut Option<Window>,
    no_window: bool,
    skip: i64,
    rotate_deg: f32,
) -> Result<(), Error> {
    let (decoder, fps, width, height) = Decoder::new(&path, skip)?;

    let duration = decoder.duration();
    if duration == 0 {
        return Ok(());
    }

    let is_video = fps.is_some() && width.is_some() && height.is_some();

    let fps = if is_video {
        Some(fps.unwrap().round() as usize) // <- remember to round here
    } else {
        None
    };

    if is_video && DEBUG {
        println!("width: {}", width.unwrap());
        println!("height: {}", height.unwrap());
        println!("fps: {}", fps.unwrap());
    }

    // if we use bounded channel, the decoder would stuck waiting for
    // videos to be consumed while audio is empty
    let (video_prod, video_cons) = crossbeam_channel::bounded(DECODE_VIDEO_QUEUE_LEN);
    let (audio_prod, audio_cons) = crossbeam_channel::bounded(DECODE_AUDIO_QUEUE_LEN);

    // we'll run no audio if `audio_init` failed
    let (audio_stream, audio_config, audio_control) =
        match audio::audio_init(audio_cons.clone(), audio::InitOpts::Default) {
            Ok((a, b, c)) => (Some(a), Some(b), Some(c)),
            Err(_) => (None, None, None),
        };

    let is_audio = audio_stream.is_some() && audio_config.is_some() && audio_control.is_some();

    let (handle, decoder_control) =
        Decoder::decode(decoder, audio_config, Some(video_prod), Some(audio_prod));

    // waiting for the few first frame to be ready
    while (is_video && video_cons.len() < DECODE_VIDEO_QUEUE_LEN / 2)
        || (is_audio && audio_cons.len() < DECODE_AUDIO_QUEUE_LEN / 2)
    {
        thread::sleep(Duration::from_millis(10));
    }

    let fps = fps.unwrap_or(DEFAULT_FPS);
    let width = width.unwrap_or(DEFAULT_WIDTH);
    let height = height.unwrap_or(DEFAULT_HEIGHT);

    // if window is some, use the window,
    // else, create a new window,
    // if create new window fail, fallback to no video.
    if window.is_none() && !no_window {
        *window = video::new_window(path.as_str(), fps, width, height).ok();
    }

    if is_audio {
        audio_stream.as_ref().unwrap().play()?;
    }

    let mut video = Video::new(width, height, fps, video_cons.clone());
    video.set_rotation_angle(rotate_deg.to_radians());

    let mut current_frame = skip as usize * fps;

    // update will exit if window is close or
    // if `video_cons` can't `recv` any frames more
    while video.update(window.as_mut())? {
        if let Some(window) = window.as_ref() {
            for key in window.get_keys_pressed(KeyRepeat::No).iter() {
                match key {
                    Key::Space => {
                        video.toggle_pause();

                        if is_audio {
                            let ctrl = audio_control.as_ref().unwrap();
                            let mut task = ctrl.task.lock().unwrap();
                            *task = match *task {
                                audio::Task::Play => audio::Task::Pause,
                                audio::Task::Pause => audio::Task::Play,
                                audio::Task::Flush | audio::Task::FlushOk => {
                                    panic!("Not supposed to be here")
                                }
                            };
                        }
                    }

                    Key::Right | Key::Left => {
                        if matches!(
                            *decoder_control.status.lock().unwrap(),
                            decode::Status::Finish
                        ) {
                            // do nothing if decoder has finish
                            // with low `DECODE_PACKET_QUEUE_LEN`
                            // this should be the simpliest solution that works
                            continue;
                        }

                        let prev_audio_task = if is_audio {
                            let ctrl = audio_control.as_ref().unwrap();
                            let mut task = ctrl.task.lock().unwrap();
                            let prev = *task;
                            *task = audio::Task::Flush;
                            Some(prev)
                        } else {
                            None
                        };

                        let target_sec = {
                            let current_sec = (current_frame / fps) as i64;
                            match key {
                                Key::Right => (current_sec + 10).clamp(0, duration - 1),
                                Key::Left => (current_sec - 10).clamp(0, duration - 1),
                                _ => panic!("Unhandled key"),
                            }
                        };
                        current_frame = target_sec as usize * fps;

                        *decoder_control.task.lock().unwrap() = decode::Task::Seek(target_sec);

                        loop {
                            if let Ok(ctrl) = decoder_control.task.try_lock() {
                                if matches!(*ctrl, decode::Task::Play) {
                                    break;
                                }
                            }
                            thread::sleep(Duration::from_micros(10));
                        }

                        while (is_audio && !audio_cons.is_empty())
                            || (is_video && !video_cons.is_empty())
                        {
                            let _ = audio_cons.try_recv();
                            let _ = video_cons.try_recv();
                        }

                        if is_audio {
                            let ctrl = audio_control.as_ref().unwrap();
                            loop {
                                let cur_task = *ctrl.task.lock().unwrap();
                                match cur_task {
                                    audio::Task::FlushOk => break,
                                    _ => thread::sleep(Duration::from_micros(10)),
                                }
                            }
                            *ctrl.task.lock().unwrap() = prev_audio_task.unwrap();
                        }
                    }

                    Key::R => {
                        let current_sec = (current_frame / fps) as i64;
                        return Err(Error::Restart((current_sec).clamp(0, duration - 1)));
                    }

                    _ => {}
                }
            }
        }

        let current_sec = current_frame / fps;

        if current_frame % fps == 0 {
            print!("\x1B[2K\r");
            print!("{current_sec} / {duration} sec");
            std::io::stdout().flush().unwrap();
        }

        if current_sec == duration as usize {
            if window.is_none() || (window.is_some() && video_cons.is_empty()) {
                break;
            }
        }

        if !video.is_paused() {
            current_frame += 1;
            current_frame = current_frame.clamp(0, duration as usize * fps);
        }
    }

    // join decoder thread to check for errors
    *decoder_control.status.lock().unwrap() = decode::Status::Finish;
    handle
        .join()
        .expect("Couldn't join on the associated thread")?;

    if is_audio {
        let ctrl = audio_control.as_ref().unwrap();
        loop {
            let status = *ctrl.status.lock().unwrap();
            match status {
                audio::Status::Running => thread::sleep(Duration::from_millis(10)),
                audio::Status::Finish => break,
            }
        }
    }

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
            _ => panic!("This error kind is not supposed to print"),
        }
    }
}
