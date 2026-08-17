use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use crossbeam_channel::RecvTimeoutError;

use crate::decode::Decoder;

mod audio; // cpal
mod decode; // ffmpeg
mod video; // video proccessing

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

    let mut buf: Vec<u32> = vec![0u32; width * height];

    while window.is_open() {
        match video_cons.recv_timeout(Duration::from_millis(1)) {
            Ok(video) => buf = video::video_frame_to_vec(&video),
            Err(RecvTimeoutError::Timeout) => {} // play the old frame if timeout
            _ => break,
        }

        let (scaled_buf, scaled_width, scaled_height) =
            video::scale_to_fit(&window, &buf, width, height);

        window
            .update_with_buffer(&scaled_buf, scaled_width, scaled_height)
            .unwrap();
    }
}
