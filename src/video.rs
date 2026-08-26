use std::{thread, time::Duration};

use crossbeam_channel::{Receiver, RecvTimeoutError};
use ffmpeg::frame;
use minifb::{Window, WindowOptions};

use crate::{Error, FPS_LIMIT, HEIGHT_LIMIT, WIDTH_LIMIT};

pub fn new_window(path: &str, fps: usize, width: u32, height: u32) -> Result<Window, Error> {
    assert!(width <= WIDTH_LIMIT as u32);
    assert!(height <= HEIGHT_LIMIT as u32);
    assert!(fps <= FPS_LIMIT);
    let mut window = Window::new(
        format!("media-player -- Playing: {path}").as_str(),
        width as usize,
        height as usize,
        WindowOptions {
            scale: minifb::Scale::X1,
            /* There's a scale mode `AspectRatioStretch`
            From what tested, it did scale up
            but it didn't center, so we use `Center` then do the `scale_to_fit` by ourself. */
            scale_mode: minifb::ScaleMode::Center,
            resize: true,
            ..WindowOptions::default()
        },
    )?;
    window.set_target_fps(fps);
    Ok(window)
}

#[derive(Debug, Clone)]
pub struct Video {
    buf: Vec<u32>,
    width: u32,
    height: u32,

    frame_time: f64,

    consumer: Receiver<frame::Video>,

    is_paused: bool,
}

impl Video {
    /// Create a new `Video` to handle video rendering.
    /// ## Usage
    /// ```rust
    /// let mut video = Video::new(width, height, fps, video_cons);
    /// ```
    pub fn new(width: u32, height: u32, fps: usize, consumer: Receiver<frame::Video>) -> Self {
        assert!(width <= WIDTH_LIMIT as u32);
        assert!(height <= HEIGHT_LIMIT as u32);
        Self {
            buf: vec![0u32; width as usize * height as usize],
            width,
            height,
            frame_time: 1.0 / fps as f64,
            consumer,
            is_paused: false,
        }
    }

    /// pull frame from decoder -> scale the frame -> display -> return `Ok(true)`
    /// ## Notice
    /// `update` will not update anything if it's paused or `recv` timeout.
    ///
    /// return `Ok(false)` if `recv` return error.
    ///
    /// return `Err(Error::Exit)` if `window.is_open()` is false.
    /// ## Usage
    /// ```rust
    /// while video.update(&mut window)? {
    ///     /* handle keyboard inputs */
    /// }
    /// ```
    pub fn update(&mut self, window: Option<&mut Window>) -> Result<bool, Error> {
        if let Some(window) = window {
            if self.should_update() {
                if self.pull_frame().is_none() {
                    return Ok(false);
                }
            }

            let (scaled_buf, scaled_width, scaled_height) =
                scale_to_fit(window, &self.buf, self.width, self.height);
            window.update_with_buffer(
                &scaled_buf,
                scaled_width as usize,
                scaled_height as usize,
            )?;

            if window.is_open() {
                Ok(true)
            } else {
                Err(Error::Exit)
            }
        } else {
            thread::sleep(Duration::from_secs_f64(self.frame_time));
            Ok(true)
        }
    }

    /// Return None if there's no more frame to pull
    fn pull_frame(&mut self) -> Option<()> {
        match self
            .consumer
            .recv_timeout(Duration::from_secs_f64(self.frame_time / 2.0))
        {
            Ok(video) => self.buf = video_frame_to_vec(&video),
            Err(RecvTimeoutError::Timeout) => {}
            _ => return None,
        }
        Some(())
    }

    fn should_update(&self) -> bool {
        !self.is_paused
    }

    pub fn toggle_pause(&mut self) {
        self.is_paused = !self.is_paused;
    }
}

/// Assume frame is 0RGB.
///
/// Return **0RGB32 packed** Vec (required by minifb)
fn video_frame_to_vec(video: &ffmpeg::frame::Video) -> Vec<u32> {
    let mut buf = vec![0u32; video.height() as usize * video.width() as usize];

    for (dst_row, src_row) in buf
        .chunks_exact_mut(video.width() as usize)
        .zip(video.data(0).chunks_exact(video.stride(0)))
    {
        for (dst, src) in dst_row
            .iter_mut()
            .zip(src_row.as_chunks::<4>().0.iter().copied())
        {
            *dst = u32::from_ne_bytes(src);
        }
    }

    return buf;
}

/// Scale dynamically to fit the window.
///
/// ## Example:
/// ```rust
/// let (scaled_buf, new_width, new_height) = scale_to_fit(window, &buf, width, height);
/// ```
fn scale_to_fit(window: &Window, buf: &[u32], width: u32, height: u32) -> (Vec<u32>, u32, u32) {
    assert!(width <= WIDTH_LIMIT as u32);
    assert!(height <= HEIGHT_LIMIT as u32);

    // Use stander 16x16 fixed-point number

    let scalar = {
        let (target_width, target_height) = window.get_size();
        let width_ratio = target_width.clamp(0, WIDTH_LIMIT) as f64 / width as f64;
        let height_ratio = target_height.clamp(0, HEIGHT_LIMIT) as f64 / height as f64;
        (f64::min(width_ratio, height_ratio) * 2f64.powi(16)) as u32
    };

    let new_width = (width * scalar) >> 16;
    let new_height = (height * scalar) >> 16;

    if new_width == 0 || new_height == 0 {
        return (Vec::new(), 0, 0);
    }

    let mut scaled_buf = vec![0u32; new_width as usize * new_height as usize];
    let step = (2f64.powi(32) / scalar as f64) as u32;

    let mut i = 0u32;
    for dst_row in scaled_buf.chunks_exact_mut(new_width as usize) {
        let src_row = &buf[(i >> 16) as usize * width as usize..];

        let mut j = 0u32;
        for pixel in dst_row.iter_mut() {
            *pixel = src_row[(j >> 16) as usize];
            j += step;
        }

        i += step;
    }

    return (scaled_buf, new_width, new_height);
}
