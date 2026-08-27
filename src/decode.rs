use std::sync::Arc;
use std::sync::Mutex;
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

use crate::DECODE_QUEUE_LEN;
use crate::Error;
use crate::FPS_LIMIT;
use crate::HEIGHT_LIMIT;
use crate::WIDTH_LIMIT;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Status {
    /// Decoder is still running
    Running,
    /// Decoder has finish, no more decode frames.
    Finish,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Task {
    Play,
    Seek(i64),
}

#[derive(Debug)]
pub struct Control {
    pub task: Mutex<Task>,
    pub status: Mutex<Status>,
}

pub struct Decoder {
    ictx: format::context::Input,

    video_decoder: Option<decoder::Video>,
    video_strm_index: Option<usize>,

    audio_decoder: Option<decoder::Audio>,
    audio_strm_index: Option<usize>,
}

impl Decoder {
    /// ### Usage:
    /// ```rust
    /// let (decoder, fps, width, height) = Decoder::new(&path)?;
    ///
    /// if fps.is_some() && width.is_some() && height.is_some() {
    ///     let (fps, width, height) = (fps.unwrap(), width.unwrap(), height.unwrap());
    /// }
    /// ```
    pub fn new<P>(path: &P) -> Result<(Self, Option<f64>, Option<u32>, Option<u32>), Error>
    where
        P: AsRef<std::path::Path> + ?Sized,
    {
        ffmpeg::init()?;
        let ictx = ffmpeg::format::input(path)?;

        let audio_strm = ictx.streams().best(media::Type::Audio);
        let (audio_strm_index, audio_decoder) = match audio_strm.as_ref() {
            Some(strm) => (
                Some(strm.index()),
                Some(
                    codec::Context::from_parameters(strm.parameters())?
                        .decoder()
                        .audio()?,
                ),
            ),
            None => (None, None),
        };

        let video_strm = ictx.streams().best(media::Type::Video);
        let (video_strm_index, video_decoder) = match video_strm.as_ref() {
            Some(strm) => (
                Some(strm.index()),
                Some(
                    codec::Context::from_parameters(strm.parameters())?
                        .decoder()
                        .video()?,
                ),
            ),
            None => (None, None),
        };

        let (fps, width, height) = match video_strm.as_ref() {
            Some(strm) => {
                let (fps, width, height) = (
                    f64::from(strm.avg_frame_rate()),
                    video_decoder.as_ref().unwrap().width(),
                    video_decoder.as_ref().unwrap().height(),
                );

                if width > WIDTH_LIMIT as u32 {
                    return Err(Error::Log("Error: Video exceed width limit."));
                }

                if height > HEIGHT_LIMIT as u32 {
                    return Err(Error::Log("Error: Video exceed height limit."));
                }

                if fps + 1.0 > FPS_LIMIT as f64 {
                    return Err(Error::Log("Error: Video exceed fps limit."));
                }

                (Some(fps), Some(width), Some(height))
            }
            None => (None, None, None),
        };

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

    pub fn duration(&self) -> i64 {
        ffmpeg::Rescale::rescale(&self.ictx.duration(), ffmpeg::rescale::TIME_BASE, (1, 1))
    }

    /// Spawn 1 thread to start decoding.
    ///
    /// ## Notice
    /// Decoder will just don't send any `video frame` if input
    /// file does not have video **or** `video_prod` is `None`
    ///
    /// Decoder will just don't send any `audio frame` if input
    /// file does not have audio **or**, `audio_config` or `audio_prod` is `None`
    pub fn decode(
        mut decoder: Decoder,
        audio_config: Option<cpal::SupportedStreamConfig>,
        mut video_prod: Option<Sender<frame::Video>>,
        mut audio_prod: Option<Sender<frame::Audio>>,
    ) -> (thread::JoinHandle<Result<(), Error>>, Arc<Control>) {
        let control = Arc::new(Control {
            task: Mutex::new(Task::Play),
            status: Mutex::new(Status::Running),
        });

        (
            {
                let control = control.clone();
                thread::spawn(move || -> Result<(), Error> {
                    let mut is_video = decoder.video_decoder.is_some()
                        && decoder.video_strm_index.is_some()
                        && video_prod.is_some();

                    let mut is_audio = decoder.audio_decoder.is_some()
                        && decoder.audio_strm_index.is_some()
                        && audio_config.is_some()
                        && audio_prod.is_some();

                    let mut scaler = if is_video {
                        Some(ffmpeg::software::scaling::Context::get(
                            decoder.video_decoder.as_ref().unwrap().format(),
                            decoder.video_decoder.as_ref().unwrap().width(),
                            decoder.video_decoder.as_ref().unwrap().height(),
                            if is_little_endian() {
                                format::Pixel::BGRZ
                            } else {
                                format::Pixel::ZRGB
                            }, // somehow, ZRGB32 does not work
                            decoder.video_decoder.as_ref().unwrap().width(),
                            decoder.video_decoder.as_ref().unwrap().height(),
                            ffmpeg::software::scaling::flag::Flags::BILINEAR,
                        )?)
                    } else {
                        None
                    };

                    let mut resampler = if is_audio {
                        Some(ffmpeg::software::resampling::Context::get(
                            decoder.audio_decoder.as_ref().unwrap().format(),
                            decoder.audio_decoder.as_ref().unwrap().channel_layout(),
                            decoder.audio_decoder.as_ref().unwrap().rate(),
                            audio_config.unwrap().sample_format().as_ffmpeg_sample(),
                            ChannelLayout::default(audio_config.unwrap().channels().into()),
                            audio_config.unwrap().sample_rate(),
                        )?)
                    } else {
                        None
                    };

                    loop {
                        if matches!(*control.status.lock().unwrap(), Status::Finish) {
                            break;
                        }

                        let mut task = control.task.lock().unwrap();
                        match *task {
                            Task::Play => drop(task),
                            Task::Seek(seconds) => {
                                let position = ffmpeg::Rescale::rescale(
                                    &seconds,
                                    (1, 1),
                                    ffmpeg::rescale::TIME_BASE,
                                );
                                decoder.ictx.seek(position, ..position)?;

                                if is_video {
                                    decoder.video_decoder.as_mut().unwrap().flush();
                                }

                                if is_audio {
                                    decoder.audio_decoder.as_mut().unwrap().flush();
                                    Self::audio_flush(
                                        resampler.as_mut().unwrap(),
                                        audio_prod.as_mut().unwrap(),
                                    )?;
                                }

                                *task = Task::Play;
                                drop(task);

                                while (is_audio && !audio_prod.as_ref().unwrap().is_empty())
                                    || (is_video && !video_prod.as_ref().unwrap().is_empty())
                                {
                                    thread::sleep(Duration::from_micros(10));
                                }
                            }
                        }

                        let Some((stream, packet)) = decoder.ictx.packets().next() else {
                            break; // <- natural break
                        };

                        // VIDEO
                        if is_video && stream.index() == decoder.video_strm_index.unwrap() {
                            decoder
                                .video_decoder
                                .as_mut()
                                .unwrap()
                                .send_packet(&packet)?;

                            is_video = Self::video_decode(
                                decoder.video_decoder.as_mut().unwrap(),
                                scaler.as_mut().unwrap(),
                                &mut video_prod.as_mut().unwrap(),
                            )?;
                        }
                        // AUDIO
                        else if is_audio && stream.index() == decoder.audio_strm_index.unwrap() {
                            decoder
                                .audio_decoder
                                .as_mut()
                                .unwrap()
                                .send_packet(&packet)?;

                            is_audio = Self::audio_decode(
                                decoder.audio_decoder.as_mut().unwrap(),
                                resampler.as_mut().unwrap(),
                                audio_prod.as_mut().unwrap(),
                            )?;
                        }

                        if !is_video && !is_audio {
                            break;
                        }

                        loop {
                            let v = if is_video {
                                video_prod.as_ref().unwrap().len()
                            } else {
                                usize::MAX
                            };

                            let a = if is_audio {
                                audio_prod.as_ref().unwrap().len()
                            } else {
                                usize::MAX
                            };

                            if matches!(*control.status.lock().unwrap(), Status::Finish)
                                || !matches!(*control.task.lock().unwrap(), Task::Play)
                                || v <= DECODE_QUEUE_LEN
                                || a <= DECODE_QUEUE_LEN
                            {
                                break;
                            }

                            thread::sleep(Duration::from_micros(50)); // just a random value that works
                        }
                    }

                    *control.status.lock().unwrap() = Status::Finish;

                    if is_video {
                        decoder.video_decoder.as_mut().unwrap().send_eof()?;
                        Self::video_decode(
                            decoder.video_decoder.as_mut().unwrap(),
                            scaler.as_mut().unwrap(),
                            video_prod.as_mut().unwrap(),
                        )?;
                    }
                    if is_audio {
                        decoder.audio_decoder.as_mut().unwrap().send_eof()?;
                        Self::audio_decode(
                            decoder.audio_decoder.as_mut().unwrap(),
                            resampler.as_mut().unwrap(),
                            audio_prod.as_mut().unwrap(),
                        )?;
                        Self::audio_flush(
                            resampler.as_mut().unwrap(),
                            audio_prod.as_mut().unwrap(),
                        )?;
                    }

                    Ok(())
                })
            },
            control,
        )
    }

    /// return `Ok(is_video)`
    fn video_decode(
        decoder: &mut decoder::Video,
        scaler: &mut ffmpeg::software::scaling::Context,
        video_prod: &mut Sender<frame::Video>,
    ) -> Result<bool, Error> {
        let mut is_video = true;
        loop {
            let mut decoded = frame::Video::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Video::empty();
            scaler.run(&decoded, &mut frame)?;

            is_video = video_prod.send(frame).is_ok();
        }
        Ok(is_video)
    }

    /// return `Ok(is_audio)`
    fn audio_decode(
        decoder: &mut decoder::Audio,
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: &mut Sender<frame::Audio>,
    ) -> Result<bool, Error> {
        let mut is_audio = true;
        loop {
            let mut decoded = frame::Audio::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Audio::empty();
            resampler.run(&decoded, &mut frame)?;

            is_audio = audio_prod.send(frame).is_ok();
        }
        Ok(is_audio)
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
