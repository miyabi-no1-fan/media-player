extern crate ffmpeg_next as ffmpeg;

use minifb::{Window, WindowOptions};

pub fn new_window(path: &str, fps: usize, width: usize, height: usize) -> Window {
    let mut window = Window::new(
        format!("media-player -- Playing: {path}").as_str(),
        width,
        height,
        WindowOptions {
            /* There's a scale mode `AspectRatioStretch`
            From what tested, it did scale up
            but it didn't center, so we use `Center` then do the scale up by ourself. */
            scale: minifb::Scale::X1,
            scale_mode: minifb::ScaleMode::Center,
            resize: true,
            ..WindowOptions::default()
        },
    )
    .unwrap();
    window.set_target_fps(fps);
    window
}

pub fn video_frame_to_vec(video: &ffmpeg::frame::Video) -> Vec<u32> {
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

pub fn scale_to_fit(
    window: &Window,
    buf: &[u32],
    width: usize,
    height: usize,
) -> (Vec<u32>, usize, usize) {
    // scale the our buffer to fit the window dynamically
    let scalar = {
        let (target_width, target_height) = window.get_size();
        let width_ratio = target_width as f64 / width as f64;
        let height_ratio = target_height as f64 / height as f64;
        f64::min(width_ratio, height_ratio)
    };

    scaling(&buf, scalar, width, height)
}

pub fn scaling(buf: &[u32], scalar: f64, width: usize, height: usize) -> (Vec<u32>, usize, usize) {
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
