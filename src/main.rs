extern crate ffmpeg_next as ffmpeg;

use std::{
    sync::mpsc::{self, Receiver},
    thread,
    time::Duration,
};

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use minifb::{Window, WindowOptions};

// ffmpeg wrapper
mod decoder;

#[allow(dead_code)]
#[derive(Debug)]
enum Error {
    DecodeError(decoder::Error),
    AudioError(cpal::ErrorKind),
    WindowError(minifb::Error),
}

pub struct Audio {
    stream: cpal::Stream,
    config: cpal::SupportedStreamConfig,
}

fn main() -> Result<(), Error> {
    let file = std::env::args().nth(1).expect("Cannot open file.");

    let mut decoder = decoder::Decoder::new(&file)?;

    let fps = decoder.frame_rate().round() as usize; // <- remember to round here
    let width = decoder.width();
    let height = decoder.height();

    let mut window = Window::new(
        format!("media-player -- Playing: {file}").as_str(),
        width,
        height,
        WindowOptions {
            scale: minifb::Scale::X1,
            /* There's a scale mode `AspectRatioStretch`
            From what tested, it did scale up
            but it didn't center, so we use `Center` then do the scale up by ourself. */
            scale_mode: minifb::ScaleMode::Center,
            resize: true,
            ..WindowOptions::default()
        },
    )?;
    window.set_target_fps(fps);

    let (video_producer, video_consumer) = mpsc::sync_channel(60);
    let (audio_producer, audio_consumer) = mpsc::sync_channel(60);

    let audio = audio_init(audio_consumer).ok();
    if let Some(audio) = audio.as_ref() {
        let e = audio.stream.play();
        if e.is_err() {
            eprintln!("{e:#?}");
        }
    }

    let handle = {
        let audio_config = if let Some(audio) = audio.as_ref() {
            Some(audio.config.clone())
        } else {
            None
        };

        thread::spawn(move || -> Result<(), Error> {
            let mut formatter = decoder::Formatter::new(&decoder, audio_config)?;

            while let Some(frame) = decoder.next()? {
                match frame {
                    decoder::Frame::Video(mut video) => {
                        formatter.make_rgba8(&mut video)?;
                        if video_producer.send(video).is_err() {
                            // error happens because the main thread `drop(receiver)`
                            // this is not an error so we just need to return ok
                            return Ok(());
                        }
                    }
                    decoder::Frame::Audio(mut audio) => {
                        formatter.resample(&mut audio)?;
                        if audio_producer.send(audio).is_err() {
                            return Ok(());
                        }
                    }
                }
            }

            Ok(())
        })
    };

    // the current screen buffer -- needed for the pause feature to work
    let mut buf: Vec<u32> = vec![0u32; width * height];

    let mut is_paused = false; // for the pause video feature

    while window.is_open() {
        for key in window.get_keys_pressed(minifb::KeyRepeat::No).iter() {
            match key {
                minifb::Key::Space => is_paused = !is_paused,
                _ => (),
            }
        }

        if !is_paused {
            match video_consumer.recv_timeout(Duration::from_millis(1)) {
                Ok(frame) => buf = frame.to_xrgb_vec(),
                Err(mpsc::RecvTimeoutError::Timeout) => {}
                Err(_) => break, // err = end of video
            }
        }

        // scale the our buffer to fit the window dynamically
        let scalar = {
            let (target_width, target_height) = window.get_size();
            let width_ratio = target_width as f64 / width as f64;
            let height_ratio = target_height as f64 / height as f64;
            f64::min(width_ratio, height_ratio)
        };
        let (scaled_buf, width, height) = scaling(&buf, scalar, width, height);

        window.update_with_buffer(&scaled_buf, width, height)?;
        /* `update_with_buffer` already handle the `thread::sleep`.
        It also calculate a delta internally to wake up early,
        which give us time to do the work without hurting the fps.
        Since each loop is doing the same work,
        the delta method is optimized in this case */
    }

    drop(video_consumer);
    handle.join().expect("decoder thread should not panic")?; // join to check for errors

    /* When exit, it always print:
    `warning: queue 0x55c327978cc0 destroyed while proxies still attached:
        wl_buffer#22 still attached
        wl_buffer#21 still attached
        wl_shm_pool#19 still attached
        wl_shm_pool#17 still attached
        wl_surface#14 still attached
        wl_shm_pool#16 still attached
        zxdg_toplevel_decoration_v1#15 still attached
        xdg_toplevel#13 still attached
        xdg_surface#12 still attached
        xdg_wm_base#11 still attached
        wl_buffer#10 still attached
        wl_shm_pool#9 still attached
        wl_surface#8 still attached
        wl_shm#7 still attached
        wl_compositor#6 still attached
        wl_pointer#5 still attached
        wl_keyboard#4 still attached
        wl_seat#3 still attached
        wl_registry#2 still attached`
    We have no idea what it is.
    But we do know that it was from `minifb`
    --- TODO: fix the bug --- */

    Ok(())
}

impl decoder::VideoFrame {
    fn to_xrgb_vec(&self) -> Vec<u32> {
        let mut buf = vec![0u32; self.height() * self.width()]; // RGBA8
        let mut buf_it = buf.iter_mut();

        for i in 0..self.height() {
            let src_row = self.line(i).as_chunks::<4>().0.iter();

            for src in src_row {
                let dst = buf_it.next().expect("size of buf were calculated for this");

                let r = src[0] as u32;
                let g = src[1] as u32;
                let b = src[2] as u32;

                *dst = r << 16 | g << 8 | b;
            }
        }

        return buf;
    }
}

fn scaling(buf: &[u32], scalar: f64, width: usize, height: usize) -> (Vec<u32>, usize, usize) {
    /* TODO: use fixed-point scalar.
    fixed-point would make this vectorizable. */

    let new_width = (width as f64 * scalar) as usize;
    let new_height = (height as f64 * scalar) as usize;

    if new_width == 0 || new_height == 0 {
        return (Vec::new(), 0, 0);
    }

    let mut scaled_buf = vec![0u32; new_width * new_height];
    let step = 1.0 / scalar;

    let mut i = 0f64;
    for dst_row in scaled_buf.chunks_exact_mut(new_width) {
        let src_row = &buf[i as usize * width..];

        let mut j = 0f64;
        for pixel in dst_row.iter_mut() {
            *pixel = src_row[j as usize];
            j += step;
        }

        i += step;
    }

    return (scaled_buf, new_width, new_height);
}

fn audio_init(audio_consumer: Receiver<decoder::AudioFrame>) -> Result<Audio, Error> {
    let host = cpal::default_host();
    let audio_device = host
        .default_output_device()
        .ok_or(Error::AudioError(cpal::ErrorKind::DeviceNotAvailable))?;
    let audio_config = audio_device.default_output_config()?;

    if audio_config.sample_format() != cpal::SampleFormat::F32 {
        return Err(Error::AudioError(cpal::ErrorKind::UnsupportedConfig));
    }

    let audio_stream = audio_device.build_output_stream(
        audio_config.into(),
        move |data: &mut [f32], _| {
            data.fill(0.0);
            if let Ok(src) = audio_consumer.recv() {
                let src = src.data().as_chunks::<4>().0.iter().copied();
                for (dst, src) in data.iter_mut().zip(src) {
                    *dst = f32::from_ne_bytes(src);
                }
            }
        },
        |err| eprintln!("{err}"),
        None,
    )?;

    Ok(Audio {
        stream: audio_stream,
        config: audio_config,
    })
}

impl From<decoder::Error> for Error {
    fn from(value: decoder::Error) -> Self {
        Self::DecodeError(value)
    }
}

impl From<minifb::Error> for Error {
    fn from(value: minifb::Error) -> Self {
        Self::WindowError(value)
    }
}

impl From<cpal::Error> for Error {
    fn from(value: cpal::Error) -> Self {
        Self::AudioError(value.kind())
    }
}
