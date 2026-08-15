extern crate ffmpeg_next as ffmpeg;

use std::{sync::mpsc, thread};

use minifb::{Window, WindowOptions};

// ffmpeg wrapper
mod video;

#[allow(dead_code)]
#[derive(Debug)]
enum Error {
    DecodeError(video::Error),
    WindowError(minifb::Error),
}

fn main() -> Result<(), Error> {
    let file = std::env::args().nth(1).expect("Cannot open file.");

    // init a new decoder
    let mut decoder = video::Decoder::new(&file)?;

    let fps = decoder.frame_rate().round() as usize; // <- remember to round here
    let width = decoder.width();
    let height = decoder.height();

    // create new window
    let mut window = Window::new(
        format!("media-player -- Playing: {file}").as_str(),
        width,
        height,
        WindowOptions {
            scale: minifb::Scale::X1,
            scale_mode: minifb::ScaleMode::Center,
            resize: true,
            ..WindowOptions::default()
        },
    )?;
    /* There's a scale mode `AspectRatioStretch`
    From what tested, it did scale up
    but it didn't center, so we use `Center` then do the scale up by ourself. */

    window.set_target_fps(fps);

    // channel between decoder thread and the main thread
    // limit to only decode 4 frames ahead
    let (sender, receiver) = mpsc::sync_channel::<video::Frame>(8);

    // spawn decoder thread
    let handle = thread::spawn(move || -> Result<(), Error> {
        // initialize the scaler (converting other color formats into RGBA32)
        let mut scaler = video::Formatter::new(&decoder)?;

        while let Some(mut frame) = decoder.next()? {
            scaler.run(&mut frame)?;
            if sender.send(frame).is_err() {
                // error happens because the main thread `drop(receiver)`
                // this is not an error so we just need to return ok
                return Ok(());
            }
        }

        Ok(())
    });

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

        // pull the next decoded frame if not paused
        if !is_paused {
            match receiver.recv() {
                Ok(frame) => buf = frame.to_xrgb_vec(),
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

    drop(receiver); // drop the receiver so the decoder thread will quit
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

impl video::Frame {
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

impl From<video::Error> for Error {
    fn from(value: video::Error) -> Self {
        Self::DecodeError(value)
    }
}

impl From<minifb::Error> for Error {
    fn from(value: minifb::Error) -> Self {
        Self::WindowError(value)
    }
}
