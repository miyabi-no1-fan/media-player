use std::thread;
use std::time::Duration;

use crossbeam_channel::Sender;
use ffmpeg::ChannelLayout;
use ffmpeg::codec;
use ffmpeg::decoder;
use ffmpeg::format;
use ffmpeg::format::sample::Type as SampleType;
use ffmpeg::frame;
use ffmpeg::media;

use crate::Error;
use crate::FPS_LIMIT;
use crate::HEIGHT_LIMIT;
use crate::WIDTH_LIMIT;

pub struct Decoder {
    ictx: format::context::Input,

    video_decoder: decoder::Video,
    video_strm_index: usize,

    audio_decoder: decoder::Audio,
    audio_strm_index: usize,
}

impl Decoder {
    /// ### Usage:
    /// ```rust
    /// let (decoder, fps, width, height) = Decoder::new(&path)?;
    /// ```
    pub fn new<P>(path: &P) -> Result<(Self, f64, u32, u32), Error>
    where
        P: AsRef<std::path::Path> + ?Sized,
    {
        ffmpeg::init()?;
        let ictx = ffmpeg::format::input(path)?;

        let audio_strm = ictx
            .streams()
            .best(media::Type::Audio)
            .ok_or(Error::Log("File does not have audio"))?;
        let audio_strm_index = audio_strm.index();
        let audio_decoder = codec::Context::from_parameters(audio_strm.parameters())?
            .decoder()
            .audio()?;

        let video_strm = ictx
            .streams()
            .best(media::Type::Video)
            .ok_or(Error::Log("File does not have video"))?;
        let video_strm_index = video_strm.index();
        let video_decoder = codec::Context::from_parameters(video_strm.parameters())?
            .decoder()
            .video()?;

        let fps = f64::from(video_strm.avg_frame_rate());
        let (width, height) = (video_decoder.width(), video_decoder.height());

        if width > WIDTH_LIMIT as u32 {
            return Err(Error::Log("Error: Video exceed width limit."));
        }

        if height > HEIGHT_LIMIT as u32 {
            return Err(Error::Log("Error: Video exceed height limit."));
        }

        if fps + 1.0 > FPS_LIMIT as f64 {
            return Err(Error::Log("Error: Video exceed fps limit."));
        }

        Ok((
            Self {
                ictx,
                video_decoder,
                video_strm_index,
                audio_decoder,
                audio_strm_index,
            },
            fps,
            width,
            height,
        ))
    }

    /// Spawn 1 thread to start decoding.
    ///
    /// If decoder got error, it'd return.
    ///
    /// When return `drop(video_prod)` will be called.
    ///
    /// You should check for the consumer status
    /// and quit if `recv` fail.
    pub fn decode(
        mut decoder: Decoder,
        audio_config: cpal::SupportedStreamConfig,
        mut video_prod: Sender<frame::Video>,
        mut audio_prod: Sender<frame::Audio>,
    ) -> thread::JoinHandle<Result<(), Error>> {
        thread::spawn(move || -> Result<(), Error> {
            let mut no_audio = false;
            let mut no_video = false;

            let mut scaler = ffmpeg::software::scaling::Context::get(
                decoder.video_decoder.format(),
                decoder.video_decoder.width(),
                decoder.video_decoder.height(),
                if is_little_endian() {
                    format::Pixel::BGRZ
                } else {
                    format::Pixel::ZRGB
                }, // somehow, ZRGB32 does not work
                decoder.video_decoder.width(),
                decoder.video_decoder.height(),
                ffmpeg::software::scaling::flag::Flags::BILINEAR,
            )?;

            let mut resampler = ffmpeg::software::resampling::Context::get(
                decoder.audio_decoder.format(),
                decoder.audio_decoder.channel_layout(),
                decoder.audio_decoder.rate(),
                audio_config.sample_format().as_ffmpeg_sample(),
                ChannelLayout::default(audio_config.channels().into()),
                audio_config.sample_rate(),
            )?;

            for (stream, packet) in decoder.ictx.packets() {
                match stream.index() {
                    i if i == decoder.video_strm_index => {
                        decoder.video_decoder.send_packet(&packet)?;
                        no_video = Self::video_decode(
                            &mut decoder.video_decoder,
                            &mut scaler,
                            &mut video_prod,
                        )?
                        .is_none();
                    }
                    i if i == decoder.audio_strm_index => {
                        decoder.audio_decoder.send_packet(&packet)?;
                        no_audio = Self::audio_decode(
                            &mut decoder.audio_decoder,
                            &mut resampler,
                            &mut audio_prod,
                        )?
                        .is_none();
                    }
                    _ => {}
                }

                if no_audio && no_video {
                    break;
                }

                while video_prod.len() > 10 && audio_prod.len() > 10 {
                    thread::sleep(Duration::from_micros(100));
                }
            }

            decoder.video_decoder.send_eof()?;
            decoder.audio_decoder.send_eof()?;
            Self::video_decode(&mut decoder.video_decoder, &mut scaler, &mut video_prod)?;
            Self::audio_decode(&mut decoder.audio_decoder, &mut resampler, &mut audio_prod)?;
            Self::audio_flush(&mut resampler, &mut audio_prod)?;

            Ok(())
        })
    }

    /// `Ok(Some(()))` => Success
    /// `Ok(None)` => Decode is ok but cannot `send` anymore
    /// `Err(_)` => Error
    fn video_decode(
        decoder: &mut decoder::Video,
        scaler: &mut ffmpeg::software::scaling::Context,
        video_prod: &mut Sender<frame::Video>,
    ) -> Result<Option<()>, Error> {
        let mut no_video = false;
        loop {
            let mut decoded = frame::Video::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Video::empty();
            scaler.run(&decoded, &mut frame)?;

            no_video = video_prod.send(frame).is_err();
        }
        if no_video { Ok(None) } else { Ok(Some(())) }
    }

    /// `Ok(Some(()))` => Success
    /// `Ok(None)` => Decode is ok but cannot `send` anymore
    /// `Err(_)` => Error
    fn audio_decode(
        decoder: &mut decoder::Audio,
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: &mut Sender<frame::Audio>,
    ) -> Result<Option<()>, Error> {
        let mut no_audio = false;
        loop {
            let mut decoded = frame::Audio::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Audio::empty();
            resampler.run(&decoded, &mut frame)?;

            no_audio = audio_prod.send(frame).is_err();
        }
        if no_audio { Ok(None) } else { Ok(Some(())) }
    }

    fn audio_flush(
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: &mut Sender<frame::Audio>,
    ) -> Result<(), Error> {
        loop {
            let mut frame = frame::Audio::new(
                resampler.output().format,
                0, // flush will allocate
                resampler.output().channel_layout,
            );
            resampler.flush(&mut frame)?;

            if frame.samples() == 0 {
                break;
            }

            if audio_prod.send(frame).is_err() {
                break;
            }
        }
        Ok(())
    }
}

trait SampleFormatConversion {
    fn as_ffmpeg_sample(&self) -> format::Sample;
}

impl SampleFormatConversion for cpal::SampleFormat {
    fn as_ffmpeg_sample(&self) -> format::Sample {
        match self {
            Self::U8 => format::Sample::U8(SampleType::Packed),
            Self::I16 => format::Sample::I16(SampleType::Packed),
            Self::I32 => format::Sample::I32(SampleType::Packed),
            Self::I64 => format::Sample::I64(SampleType::Packed),
            Self::F32 => format::Sample::F32(SampleType::Packed),
            Self::F64 => format::Sample::F64(SampleType::Packed),
            _ => panic!("Unsupported Sample Format"),
        }
    }
}

fn is_little_endian() -> bool {
    let v: u16 = 1;
    v.to_ne_bytes()[0] != 0
}
