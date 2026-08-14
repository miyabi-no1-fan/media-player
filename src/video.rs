use std::path::Path;

#[derive(Debug)]
pub enum Error {
    #[allow(dead_code)]
    FFmpegError(ffmpeg::Error),
}

pub struct Decoder {
    context: ffmpeg::format::context::input::Input,
    decoder: ffmpeg::decoder::Video,
    video_stream_index: usize,
    fps: f64,
}

impl Decoder {
    pub fn new<P>(path: &P) -> Result<Self, Error>
    where
        P: AsRef<Path> + ?Sized,
    {
        ffmpeg::init()?;
        let context = ffmpeg::format::input(path)?;

        let input = context
            .streams()
            .best(ffmpeg::media::Type::Video)
            .ok_or(ffmpeg::Error::StreamNotFound)?;
        let video_stream_index = input.index();
        let fps = f64::from(input.avg_frame_rate());

        let context_decoder = ffmpeg::codec::context::Context::from_parameters(input.parameters())?;
        let decoder = context_decoder.decoder().video()?;

        Ok(Self {
            context,
            decoder,
            video_stream_index,
            fps,
        })
    }

    pub fn next(&mut self) -> Result<Option<Frame>, Error> {
        let mut decoded = ffmpeg::frame::Video::empty();
        match self.decoder.receive_frame(&mut decoded) {
            Ok(()) => Ok(Some(Frame { frame: decoded })),

            // out of frame
            Err(ffmpeg::Error::Other {
                errno: ffmpeg::util::error::EAGAIN,
            }) => {
                // loop until we find the video packet needed
                while let Some((stream, packet)) = self.context.packets().next() {
                    if stream.index() == self.video_stream_index {
                        self.decoder.send_packet(&packet)?; // send
                        return self.next(); // retry
                    }
                }
                // no packet = eof
                self.decoder.send_eof()?;
                self.next() // flush
            }

            Err(ffmpeg::Error::Eof) => Ok(None), // eof is not error
            Err(e) => Err(e.into()),
        }
    }

    pub fn width(&self) -> usize {
        self.decoder.width() as usize
    }

    pub fn height(&self) -> usize {
        self.decoder.height() as usize
    }

    pub fn frame_rate(&self) -> f64 {
        self.fps
    }
}

pub struct Scaler {
    scaler: ffmpeg_next::software::scaling::context::Context,
}

impl Scaler {
    pub fn new(decoder: &Decoder) -> Result<Self, Error> {
        Ok(Self {
            scaler: ffmpeg::software::scaling::context::Context::get(
                decoder.decoder.format(),
                decoder.decoder.width(),
                decoder.decoder.height(),
                ffmpeg::format::Pixel::RGBA,
                decoder.decoder.width(),
                decoder.decoder.height(),
                ffmpeg::software::scaling::flag::Flags::BILINEAR,
            )?,
        })
    }

    pub fn run(&mut self, frame: &mut Frame) -> Result<(), Error> {
        let mut scaled_frame = ffmpeg::frame::Video::empty();
        self.scaler.run(&frame.frame, &mut scaled_frame)?;
        frame.frame = scaled_frame;
        Ok(())
    }
}

pub struct Frame {
    frame: ffmpeg::frame::Video,
}

impl Frame {
    pub fn line(&self, height: usize) -> &[u8] {
        assert!(height < self.height());

        let start = height * self.frame.stride(0);
        let len = 4 * self.width();

        &self.frame.data(0)[start..start + len]
    }

    pub fn height(&self) -> usize {
        self.frame.height() as usize
    }

    pub fn width(&self) -> usize {
        self.frame.width() as usize
    }
}

impl From<ffmpeg::Error> for Error {
    fn from(value: ffmpeg::Error) -> Self {
        Self::FFmpegError(value)
    }
}
