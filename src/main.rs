use std::{thread, time::Duration};

use cpal::traits::StreamTrait;
use ringbuf::traits::{Consumer, Split};

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

    let (audio_stream, audio_config) = audio::audio_init(audio_cons);
    audio_stream.play().unwrap();

    Decoder::decode(decoder, audio_config, video_prod, audio_prod);

    let mut buf: Vec<u32> = vec![0u32; width * height];

    while window.is_open() {
        loop {
            match video_cons.try_pop() {
                Some(video) => {
                    buf = video::video_frame_to_vec(&video);
                    break;
                }
                None => thread::sleep(Duration::from_millis(1)),
            }
        }

        let (scaled_buf, scaled_width, scaled_height) =
            video::scale_to_fit(&window, &buf, width, height);

        window
            .update_with_buffer(&scaled_buf, scaled_width, scaled_height)
            .unwrap();
    }
}
