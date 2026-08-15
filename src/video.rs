use std::path::Path;

#[derive(Debug)]
pub enum Error {
    #[allow(dead_code)]
    FFmpegError(ffmpeg::Error),
}

pub struct Decoder {
    context: ffmpeg::format::context::input::Input,
    video_decoder: ffmpeg::decoder::Video,
    audio_decoder: ffmpeg::decoder::Audio,
    video_stream_index: usize,
    audio_stream_index: usize,
    fps: f64,
}

impl Decoder {
    pub fn new<P>(path: &P) -> Result<Self, Error>
    where
        P: AsRef<Path> + ?Sized,
    {
        ffmpeg::init()?;
        let context = ffmpeg::format::input(path)?;

        let video_stream = context
            .streams()
            .best(ffmpeg::media::Type::Video)
            .ok_or(ffmpeg::Error::StreamNotFound)?;
        let video_stream_index = video_stream.index();
        let fps = f64::from(video_stream.avg_frame_rate());
        let video_context_decoder =
            ffmpeg::codec::context::Context::from_parameters(video_stream.parameters())?;
        let video_decoder = video_context_decoder.decoder().video()?;

        let audio_stream = context
            .streams()
            .best(ffmpeg::media::Type::Audio)
            .ok_or(ffmpeg::Error::StreamNotFound)?;
        let audio_stream_index = audio_stream.index();
        let audio_context_decoder =
            ffmpeg::codec::context::Context::from_parameters(audio_stream.parameters())?;
        let audio_decoder = audio_context_decoder.decoder().audio()?;

        Ok(Self {
            context,
            video_decoder,
            audio_decoder,
            video_stream_index,
            audio_stream_index,
            fps,
        })
    }

    pub fn next(&mut self) -> Result<Option<Frame>, Error> {
        let video = match self.video_next()? {
            Some(v) => v,
            None => return Ok(None),
        };

        let audio = match self.audio_next()? {
            Some(v) => v,
            None => return Ok(None),
        };

        Ok(Some(Frame { video, audio }))
    }

    pub fn video_next(&mut self) -> Result<Option<ffmpeg::frame::Video>, Error> {
        let mut decoded = ffmpeg::frame::Video::empty();
        match self.video_decoder.receive_frame(&mut decoded) {
            Ok(()) => Ok(Some(decoded)),

            // out of frame
            Err(ffmpeg::Error::Other {
                errno: ffmpeg::util::error::EAGAIN,
            }) => {
                // loop until we find the video packet needed
                while let Some((stream, packet)) = self.context.packets().next() {
                    if stream.index() == self.video_stream_index {
                        self.video_decoder.send_packet(&packet)?; // send
                        return self.video_next(); // retry
                    }
                }
                // no packet = eof
                self.video_decoder.send_eof()?;
                self.video_next() // flush
            }

            Err(ffmpeg::Error::Eof) => Ok(None), // eof is not error
            Err(e) => Err(e.into()),
        }
    }

    pub fn audio_next(&mut self) -> Result<Option<ffmpeg::frame::Audio>, Error> {
        let mut decoded = ffmpeg::frame::Audio::empty();
        match self.audio_decoder.receive_frame(&mut decoded) {
            Ok(()) => Ok(Some(decoded)),

            // out of frame
            Err(ffmpeg::Error::Other {
                errno: ffmpeg::util::error::EAGAIN,
            }) => {
                // loop until we find the video packet needed
                while let Some((stream, packet)) = self.context.packets().next() {
                    if stream.index() == self.audio_stream_index {
                        self.audio_decoder.send_packet(&packet)?; // send
                        return self.audio_next(); // retry
                    }
                }
                // no packet = eof
                self.audio_decoder.send_eof()?;
                self.audio_next() // flush
            }

            Err(ffmpeg::Error::Eof) => Ok(None), // eof is not error
            Err(e) => Err(e.into()),
        }
    }

    pub fn width(&self) -> usize {
        self.video_decoder.width() as usize
    }

    pub fn height(&self) -> usize {
        self.video_decoder.height() as usize
    }

    pub fn frame_rate(&self) -> f64 {
        self.fps
    }
}

pub struct Frame {
    video: ffmpeg::frame::Video,
    audio: ffmpeg::frame::Audio,
}

impl Frame {
    pub fn line(&self, height: usize) -> &[u8] {
        assert!(height < self.height());

        let start = height * self.video.stride(0);
        let len = 4 * self.width();

        &self.video.data(0)[start..start + len]
    }

    pub fn audio_sample(&self) -> &[u8] {
        self.audio.data(0)
    }

    pub fn height(&self) -> usize {
        self.video.height() as usize
    }

    pub fn width(&self) -> usize {
        self.video.width() as usize
    }
}

pub struct Formatter {
    scaler: ffmpeg::software::scaling::Context,
}

impl Formatter {
    pub fn new(decoder: &Decoder) -> Result<Self, Error> {
        Ok(Self {
            scaler: ffmpeg::software::scaling::Context::get(
                decoder.video_decoder.format(),
                decoder.video_decoder.width(),
                decoder.video_decoder.height(),
                ffmpeg::format::Pixel::RGBA,
                decoder.video_decoder.width(),
                decoder.video_decoder.height(),
                ffmpeg::software::scaling::flag::Flags::BILINEAR,
            )?,
        })
    }

    pub fn run(&mut self, frame: &mut Frame) -> Result<(), Error> {
        let mut scaled_video = ffmpeg::frame::Video::empty();
        self.scaler.run(&frame.video, &mut scaled_video)?;
        frame.video = scaled_video;
        Ok(())
    }
}

impl From<ffmpeg::Error> for Error {
    fn from(value: ffmpeg::Error) -> Self {
        Self::FFmpegError(value)
    }
}
