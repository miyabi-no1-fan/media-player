use std::path::Path;

#[derive(Debug)]
pub enum Error {
    #[allow(dead_code)]
    FFmpegError(ffmpeg::Error),
    UnsupportedConfig,
}

pub struct Decoder {
    context: ffmpeg::format::context::input::Input,

    video_decoder: ffmpeg::decoder::Video,
    video_stream_index: usize,

    audio_decoder: ffmpeg::decoder::Audio,
    audio_stream_index: usize,

    fps: f64,

    eof: bool,
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

        let video_context =
            ffmpeg::codec::context::Context::from_parameters(video_stream.parameters())?;
        let video_decoder = video_context.decoder().video()?;

        let audio_stream = context
            .streams()
            .best(ffmpeg::media::Type::Audio)
            .ok_or(ffmpeg::Error::StreamNotFound)?;
        let audio_stream_index = audio_stream.index();

        let audio_context = ffmpeg::codec::Context::from_parameters(audio_stream.parameters())?;
        let audio_decoder = audio_context.decoder().audio()?;

        Ok(Self {
            context,
            video_decoder,
            video_stream_index,
            audio_decoder,
            audio_stream_index,
            fps,
            eof: false,
        })
    }

    pub fn next(&mut self) -> Result<Option<Frame>, Error> {
        {
            let mut video = ffmpeg::frame::Video::empty();
            match self.video_decoder.receive_frame(&mut video) {
                Ok(()) => return Ok(Some(Frame::Video(VideoFrame { video }))),
                Err(ffmpeg::Error::Other {
                    errno: ffmpeg::util::error::EAGAIN,
                }) => {}
                Err(ffmpeg::Error::Eof) => {}
                Err(e) => return Err(e.into()),
            }
        }

        {
            let mut audio = ffmpeg::frame::Audio::empty();
            match self.audio_decoder.receive_frame(&mut audio) {
                Ok(()) => return Ok(Some(Frame::Audio(AudioFrame { audio }))),
                Err(ffmpeg::Error::Other {
                    errno: ffmpeg::util::error::EAGAIN,
                }) => {}
                Err(ffmpeg::Error::Eof) => {}
                Err(e) => return Err(e.into()),
            }
        }

        if self.eof {
            return Ok(None);
        }

        while let Some((stream, packet)) = self.context.packets().next() {
            match stream.index() {
                v if v == self.video_stream_index => {
                    self.video_decoder.send_packet(&packet)?;
                    return self.next();
                }
                v if v == self.audio_stream_index => {
                    self.audio_decoder.send_packet(&packet)?;
                    return self.next();
                }
                _ => {}
            }
        }

        self.video_decoder.send_eof()?;
        self.audio_decoder.send_eof()?;
        self.eof = true;
        self.next()
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

pub enum Frame {
    Video(VideoFrame),
    Audio(AudioFrame),
}

pub struct VideoFrame {
    video: ffmpeg::frame::Video,
}

pub struct AudioFrame {
    audio: ffmpeg::frame::Audio,
}

impl VideoFrame {
    pub fn line(&self, height: usize) -> &[u8] {
        assert!(height < self.height());

        let start = height * self.video.stride(0);
        let len = 4 * self.width();

        &self.video.data(0)[start..start + len]
    }

    pub fn height(&self) -> usize {
        self.video.height() as usize
    }

    pub fn width(&self) -> usize {
        self.video.width() as usize
    }
}

impl AudioFrame {
    pub fn data(&self) -> &[u8] {
        self.audio.data(0)
    }
}

pub struct Formatter {
    scaler: ffmpeg::software::scaling::Context,
    resampler: ffmpeg::software::resampling::Context,
}

impl Formatter {
    pub fn new(
        decoder: &Decoder,
        audio_config: cpal::SupportedStreamConfig,
    ) -> Result<Self, Error> {
        if audio_config.sample_format() != cpal::SampleFormat::F32 {
            return Err(Error::UnsupportedConfig);
        }

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
            resampler: ffmpeg::software::resampling::Context::get(
                decoder.audio_decoder.format(),
                decoder.audio_decoder.channel_layout(),
                decoder.audio_decoder.rate(),
                ffmpeg::format::Sample::F32(ffmpeg::util::format::sample::Type::Packed),
                if audio_config.channels() == 1 {
                    ffmpeg::channel_layout::ChannelLayout::MONO
                } else {
                    ffmpeg::channel_layout::ChannelLayout::STEREO
                },
                audio_config.sample_rate(),
            )?,
        })
    }

    pub fn make_rgba8(&mut self, video: &mut VideoFrame) -> Result<(), Error> {
        let mut scaled_video = ffmpeg::frame::Video::empty();
        self.scaler.run(&video.video, &mut scaled_video)?;
        video.video = scaled_video;
        Ok(())
    }

    pub fn resample(&mut self, audio: &mut AudioFrame) -> Result<(), Error> {
        let mut resampled_audio = ffmpeg::frame::Audio::empty();
        self.resampler.run(&audio.audio, &mut resampled_audio)?;
        audio.audio = resampled_audio;
        Ok(())
    }
}

impl From<ffmpeg::Error> for Error {
    fn from(value: ffmpeg::Error) -> Self {
        Self::FFmpegError(value)
    }
}
