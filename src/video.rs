use std::{
    thread,
    time::{Duration, Instant},
};

use crossbeam_channel::{Receiver, RecvTimeoutError};
use ffmpeg::frame;
use minifb::{Window, WindowOptions};
use rayon::prelude::*;

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

    frame_time: Duration,
    prev_time: Option<Instant>,

    consumer: Receiver<frame::Video>,

    is_paused: bool,

    /// angle of rotation in radians
    rotation: f64,
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
            frame_time: Duration::from_secs_f64(1.0 / fps as f64),
            prev_time: None,
            consumer,
            is_paused: false,
            rotation: 0.0,
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

            let rotation_mat = [
                [f64::cos(self.rotation), -f64::sin(self.rotation)],
                [f64::sin(self.rotation), f64::cos(self.rotation)],
            ];

            // scalar have to scale the video **after rotated** thus we need rotated width and height
            let (rotated_width, rotated_height) =
                get_linear_transform_size(self.width, self.height, rotation_mat);

            // scale dynamically to fit the current window
            let scalar = {
                let (target_width, target_height) = window.get_size();
                let width_ratio = target_width.clamp(0, WIDTH_LIMIT) as f64 / rotated_width as f64;
                let height_ratio =
                    target_height.clamp(0, HEIGHT_LIMIT) as f64 / rotated_height as f64;
                f64::min(width_ratio, height_ratio)
            };

            // merge matrix together
            // we're running rotation first then scale as planned
            let mut mat = [[scalar, 0.0], [0.0, scalar]];
            mat = matrix_mul(mat, rotation_mat);

            let (buf, width, height) = linear_transform(&self.buf, self.width, self.height, mat);
            window.update_with_buffer(&buf, width as usize, height as usize)?;

            if window.is_open() {
                Ok(true)
            } else {
                Err(Error::Exit)
            }
        } else {
            let delta = if let Some(prev_time) = self.prev_time {
                prev_time.elapsed()
            } else {
                Duration::from_secs(0)
            };

            if delta < self.frame_time {
                thread::sleep(self.frame_time - delta);
            }

            self.prev_time = Some(Instant::now());

            Ok(true)
        }
    }

    /// Return None if there's no more frame to pull
    fn pull_frame(&mut self) -> Option<()> {
        match self.consumer.recv_timeout(self.frame_time / 2) {
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

    pub fn is_paused(&self) -> bool {
        self.is_paused
    }

    pub fn set_rotation_angle(&mut self, radians: f64) {
        self.rotation = radians;
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
            // our ffmpeg decoder already chooses
            // 0rgb or bgr0 format based on, native endian at runtime
            // we should uses native endian here
            *dst = u32::from_ne_bytes(src);
        }
    }

    return buf;
}

/// Apply the linear transform.
///
/// ## Example:
/// ```rust
/// let (new_buf, new_width, new_height) = linear_transform(&buf, width, height, transformation_matrix);
/// ```
fn linear_transform(
    buf: &[u32],
    width: u32,
    height: u32,
    mat: [[f64; 2]; 2],
) -> (Vec<u32>, u32, u32) {
    let det = mat[0][0] * mat[1][1] - mat[0][1] * mat[1][0];

    if det == 0.0 {
        return (Vec::new(), 0, 0);
    }

    // calculate new size, through the 4 corners
    // 0, (H-1) -> -(H-1)/2, (H-1)/2
    // 0, (W-1) -> -(W-1)/2, (W-1)/2
    // half width half height is to center the image
    let half_width = (width - 1) as f64 / 2.0;
    let half_height = (height - 1) as f64 / 2.0;

    // 1 2
    // 3 4
    let x1 = -half_width * mat[0][0] + half_height * mat[0][1];
    let x2 = half_width * mat[0][0] + half_height * mat[0][1];
    let x3 = -half_width * mat[0][0] - half_height * mat[0][1];
    let x4 = half_width * mat[0][0] - half_height * mat[0][1];

    let y1 = -half_width * mat[1][0] + half_height * mat[1][1];
    let y2 = half_width * mat[1][0] + half_height * mat[1][1];
    let y3 = -half_width * mat[1][0] - half_height * mat[1][1];
    let y4 = half_width * mat[1][0] - half_height * mat[1][1];

    let xmax = x1.max(x2).max(x3).max(x4);
    let xmin = x1.min(x2).min(x3).min(x4);
    let ymax = y1.max(y2).max(y3).max(y4);
    let ymin = y1.min(y2).min(y3).min(y4);

    let new_width = (xmax - xmin + 1.0).clamp(0.0, WIDTH_LIMIT as f64) as u32;
    let new_height = (ymax - ymin + 1.0).clamp(0.0, HEIGHT_LIMIT as f64) as u32;

    let mut new_buf = vec![0u32; new_width as usize * new_height as usize];

    let inverse_mat = {
        let inv = [
            [mat[1][1] / det, -mat[0][1] / det],
            [-mat[1][0] / det, mat[0][0] / det],
        ];
        [
            [
                (inv[0][0] * 2f64.powi(32)) as i64,
                (inv[0][1] * 2f64.powi(32)) as i64,
            ],
            [
                (inv[1][0] * 2f64.powi(32)) as i64,
                (inv[1][1] * 2f64.powi(32)) as i64,
            ],
        ]
    };

    // We're using inverse mapping and incremental stepping here
    // Fixed-point numbers are 32x32 -- don't over complicate it, it's just an i64 multiply by 2^32

    // Starting from our top-left corner
    // This is simply inverse_mat * [xmin, ymax]

    let base_src_x = xmin as i64 * inverse_mat[0][0]
        + ymax as i64 * inverse_mat[0][1]
        + (half_width * 2f64.powi(32)) as i64;

    // NOTICE: y is inverted vertically
    // The Euclidean space assume y going **upwards**,
    // whereas our image has y going **downwards** so invert y is needed here
    // -- you'll notice that y consistently having the opposite sign to x in the code.
    let base_src_y = -xmin as i64 * inverse_mat[1][0] - ymax as i64 * inverse_mat[1][1]
        + (half_height * 2f64.powi(32)) as i64;

    new_buf
        .par_chunks_exact_mut(new_width as usize)
        .enumerate()
        .for_each(|(line, dst_line)| {
            let src_x = base_src_x - line as i64 * inverse_mat[0][1];
            let src_y = base_src_y + line as i64 * inverse_mat[1][1];

            // This is just a result from solving equations from the brute-force loop
            let (start_x, end_x) = if inverse_mat[0][0] > 0 {
                (
                    -src_x / inverse_mat[0][0],
                    (((width as i64) << 32) - src_x) / inverse_mat[0][0],
                )
            } else if inverse_mat[0][0] < 0 {
                (
                    (((width as i64) << 32) - src_x) / inverse_mat[0][0],
                    -src_x / inverse_mat[0][0],
                )
            } else {
                (0, new_width as i64)
            };
            let (start_y, end_y) = if inverse_mat[1][0] > 0 {
                (
                    (src_y - ((height as i64) << 32)) / inverse_mat[1][0],
                    src_y / inverse_mat[1][0],
                )
            } else if inverse_mat[1][0] < 0 {
                (
                    src_y / inverse_mat[1][0],
                    (src_y - ((height as i64) << 32)) / inverse_mat[1][0],
                )
            } else {
                (0, new_width as i64)
            };

            // `start` = first index where both x and y are valid
            // `end` = first index where either x or y invalid after start
            let mut start = start_x.max(start_y).clamp(0, new_width as i64);
            let mut end = end_x.min(end_y).clamp(0, new_width as i64);

            // `start` and `end` might be off by 1
            // calculations like `src_y / inverse_mat[1][0]`,
            // do a `floor` on `start` -- `start` supposed to be `ceil` instead
            // So we should run the brute-force loop
            // this would strongly ensure that `start` and `end` are valid
            while start < end {
                let x = start * inverse_mat[0][0] + src_x;
                let y = src_y - start * inverse_mat[1][0];
                if x >= 0 && x < ((width as i64) << 32) && y >= 0 && y < ((height as i64) << 32) {
                    break;
                }
                start += 1;
            }
            while start < end {
                let x = (end - 1) * inverse_mat[0][0] + src_x;
                let y = src_y - (end - 1) * inverse_mat[1][0];
                if x >= 0 && x < ((width as i64) << 32) && y >= 0 && y < ((height as i64) << 32) {
                    break;
                }
                end -= 1;
            }

            if start < end {
                let mut x = start * inverse_mat[0][0] + src_x;
                let mut y = src_y - start * inverse_mat[1][0];

                for dst in &mut dst_line[start as usize..end as usize] {
                    *dst = buf[(y >> 32) as usize * width as usize + (x >> 32) as usize];
                    x += inverse_mat[0][0];
                    y -= inverse_mat[1][0];
                }
            }
        });

    return (new_buf, new_width, new_height);
}

/// This is `a * b`.
///
/// **Ordering** in matrix multiplication does **matter**.
fn matrix_mul(a: [[f64; 2]; 2], b: [[f64; 2]; 2]) -> [[f64; 2]; 2] {
    [
        [
            a[0][0] * b[0][0] + a[0][1] * b[1][0],
            a[0][0] * b[0][1] + a[0][1] * b[1][1],
        ],
        [
            a[1][0] * b[0][0] + a[1][1] * b[1][0],
            a[1][0] * b[0][1] + a[1][1] * b[1][1],
        ],
    ]
}

/// Calculate the new size after linear transform without running the linear transform.
/// This meant to help with planning up the final transform matrix to use.
///
/// ## Example
/// ```
/// let (new_width, new_height) = get_linear_transform_size(width, height, mat);
/// ```
fn get_linear_transform_size(width: u32, height: u32, mat: [[f64; 2]; 2]) -> (u32, u32) {
    // This is just a copy-paste from `linear_transform`
    // `linear_transform` won't call this because it also need xmax ymin..

    // calculate new size, through the 4 corners
    // 0, (H-1) -> -(H-1)/2, (H-1)/2
    // 0, (W-1) -> -(W-1)/2, (W-1)/2
    // half width half height is to center the image
    let half_width = (width - 1) as f64 / 2.0;
    let half_height = (height - 1) as f64 / 2.0;

    // 1 2
    // 3 4
    let x1 = -half_width * mat[0][0] + half_height * mat[0][1];
    let x2 = half_width * mat[0][0] + half_height * mat[0][1];
    let x3 = -half_width * mat[0][0] - half_height * mat[0][1];
    let x4 = half_width * mat[0][0] - half_height * mat[0][1];

    let y1 = -half_width * mat[1][0] + half_height * mat[1][1];
    let y2 = half_width * mat[1][0] + half_height * mat[1][1];
    let y3 = -half_width * mat[1][0] - half_height * mat[1][1];
    let y4 = half_width * mat[1][0] - half_height * mat[1][1];

    let xmax = x1.max(x2).max(x3).max(x4);
    let xmin = x1.min(x2).min(x3).min(x4);
    let ymax = y1.max(y2).max(y3).max(y4);
    let ymin = y1.min(y2).min(y3).min(y4);

    let new_width = (xmax - xmin + 1.0).clamp(0.0, WIDTH_LIMIT as f64) as u32;
    let new_height = (ymax - ymin + 1.0).clamp(0.0, HEIGHT_LIMIT as f64) as u32;

    return (new_width, new_height);
}
