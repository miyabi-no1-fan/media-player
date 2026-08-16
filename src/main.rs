use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use ringbuf::traits::{Consumer, Observer, Split};

use crate::decode::Decoder;

mod audio;
mod decode;
mod video;

fn main() {
    let path = std::env::args().nth(1).expect("Cannot open file.");
    let (decoder, fps, width, height) = Decoder::new(&path);

    let fps = fps.round() as usize; // <- remember to round here
    let (width, height) = (width as usize, height as usize);

    let mut window = video::new_window(path.as_str(), fps, width, height);

    let vbuf = ringbuf::HeapRb::new(65536);
    let (video_prod, mut video_cons) = vbuf.split();

    let abuf = ringbuf::HeapRb::new(65536);
    let (audio_prod, audio_cons) = abuf.split();

    let (audio_stream, audio_config) = audio::audio_init(audio_cons, audio::InitOpts::Best);
    Decoder::decode(decoder, audio_config, video_prod, audio_prod);

    thread::sleep(Duration::from_millis(100)); // waiting for decoder to init
    audio_stream.play().unwrap();
    let mut buf: Vec<u32> = vec![0u32; width * height];

    while window.is_open() && (!video_cons.is_empty() || video_cons.write_is_held()) {
        while !video_cons.is_empty() || video_cons.write_is_held() {
            if let Some(video) = video_cons.try_pop() {
                buf = video::video_frame_to_vec(&video);
                break;
            } else {
                thread::sleep(Duration::from_micros(10));
            }
        }

        let (scaled_buf, scaled_width, scaled_height) =
            video::scale_to_fit(&window, &buf, width, height);

        window
            .update_with_buffer(&scaled_buf, scaled_width, scaled_height)
            .unwrap();
    }
}
