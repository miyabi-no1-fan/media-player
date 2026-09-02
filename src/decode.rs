use std::sync::Arc;
use std::sync::Mutex;
use std::thread;
use std::thread::JoinHandle;
use std::time::Duration;

use crossbeam_channel::Receiver;
use crossbeam_channel::Sender;
use ffmpeg::ChannelLayout;
use ffmpeg::codec;
use ffmpeg::decoder;
use ffmpeg::format;
use ffmpeg::format::sample::Type as SampleType;
use ffmpeg::frame;
use ffmpeg::media;

use crate::DECODE_PACKET_QUEUE_LEN;
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
    Flush,
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
    pub fn new<P>(
        path: &P,
        secs: i64,
    ) -> Result<(Self, Option<f64>, Option<u32>, Option<u32>), Error>
    where
        P: AsRef<std::path::Path> + ?Sized,
    {
        ffmpeg::init()?;
        let mut ictx = ffmpeg::format::input(path)?;

        if 0 < secs
            && secs < ffmpeg::Rescale::rescale(&ictx.duration(), ffmpeg::rescale::TIME_BASE, (1, 1))
        {
            let position = ffmpeg::Rescale::rescale(&secs, (1, 1), ffmpeg::rescale::TIME_BASE);
            ictx.seek(position, position..position)?;
        }

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
        video_prod: Option<Sender<frame::Video>>,
        audio_prod: Option<Sender<frame::Audio>>,
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

                    let video_decode_task = Arc::new(Mutex::new(None));
                    let audio_decode_task = Arc::new(Mutex::new(None));

                    let video_packet_prod = if is_video {
                        let (video_decode_pod, video_decode_cons) = crossbeam_channel::unbounded();
                        Self::video_decode(
                            decoder.video_decoder.unwrap(),
                            video_prod.unwrap(),
                            video_decode_cons,
                            Arc::clone(&video_decode_task),
                        );
                        Some(video_decode_pod)
                    } else {
                        None
                    };

                    let audio_packet_prod = if is_audio {
                        let (audio_decode_prod, audio_decode_cons) = crossbeam_channel::unbounded();
                        Self::audio_decode(
                            decoder.audio_decoder.unwrap(),
                            audio_prod.unwrap(),
                            audio_decode_cons,
                            audio_config.unwrap(),
                            Arc::clone(&audio_decode_task),
                        );
                        Some(audio_decode_prod)
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
                                decoder.ictx.seek(position, position..position)?;

                                *video_decode_task.lock().unwrap() = Some(Task::Flush);
                                *audio_decode_task.lock().unwrap() = Some(Task::Flush);

                                *task = Task::Play;
                                drop(task);

                                loop {
                                    if let Ok(task) = video_decode_task.try_lock() {
                                        if matches!(*task, None) {
                                            break;
                                        }
                                    }
                                    thread::sleep(Duration::from_micros(10));
                                }

                                loop {
                                    if let Ok(task) = audio_decode_task.try_lock() {
                                        if matches!(*task, None) {
                                            break;
                                        }
                                    }
                                    thread::sleep(Duration::from_micros(10));
                                }

                                while (is_video && !video_packet_prod.as_ref().unwrap().is_empty())
                                    || (is_audio && !audio_packet_prod.as_ref().unwrap().is_empty())
                                {
                                    thread::sleep(Duration::from_micros(10));
                                }
                            }
                            _ => panic!("Unknown decode task"),
                        }

                        let Some((stream, packet)) = decoder.ictx.packets().next() else {
                            break; // <- natural break
                        };

                        if is_video && stream.index() == decoder.video_strm_index.unwrap() {
                            if let Some(prod) = video_packet_prod.as_ref() {
                                is_video = prod.send(packet).is_ok();
                            }
                        } else if is_audio && stream.index() == decoder.audio_strm_index.unwrap() {
                            if let Some(prod) = audio_packet_prod.as_ref() {
                                is_audio = prod.send(packet).is_ok();
                            }
                        }

                        if !is_video && !is_audio {
                            break;
                        }

                        loop {
                            let v = if is_video {
                                video_packet_prod.as_ref().unwrap().len()
                            } else {
                                usize::MAX
                            };

                            let a = if is_audio {
                                audio_packet_prod.as_ref().unwrap().len()
                            } else {
                                usize::MAX
                            };

                            if matches!(*control.status.lock().unwrap(), Status::Finish)
                                || !matches!(*control.task.lock().unwrap(), Task::Play)
                                || v <= DECODE_PACKET_QUEUE_LEN
                                || a <= DECODE_PACKET_QUEUE_LEN
                            {
                                break;
                            }

                            thread::sleep(Duration::from_micros(10));
                        }
                    }

                    *control.status.lock().unwrap() = Status::Finish;

                    Ok(())
                })
            },
            control,
        )
    }

    fn video_decode(
        mut decoder: decoder::Video,
        video_prod: Sender<frame::Video>,
        packet_cons: Receiver<ffmpeg::packet::Packet>,
        task: Arc<Mutex<Option<Task>>>,
    ) -> JoinHandle<Result<(), Error>> {
        thread::spawn(move || {
            let mut scaler = ffmpeg::software::scaling::Context::get(
                decoder.format(),
                decoder.width(),
                decoder.height(),
                if is_little_endian() {
                    format::Pixel::BGRZ
                } else {
                    format::Pixel::ZRGB
                }, // somehow, ZRGB32 does not work
                decoder.width(),
                decoder.height(),
                ffmpeg::software::scaling::flag::Flags::BILINEAR,
            )?;

            let receive_and_process_decoded_frames =
                |decoder: &mut ffmpeg::decoder::Video,
                 scaler: &mut ffmpeg::software::scaling::Context|
                 -> Result<(), Error> {
                    let mut decoded = frame::Video::empty();
                    while decoder.receive_frame(&mut decoded).is_ok() {
                        let mut frame = frame::Video::empty();
                        scaler.run(&decoded, &mut frame)?;
                        if video_prod.send(frame).is_err() {
                            break;
                        }

                        let mut task = task.lock().unwrap();
                        match *task {
                            Some(Task::Flush) => {
                                decoder.flush();
                                while !packet_cons.is_empty() {
                                    let _ = packet_cons.try_recv();
                                }
                                *task = None;
                                drop(task);
                                while !video_prod.is_empty() {
                                    thread::sleep(Duration::from_micros(10));
                                }
                                break;
                            }
                            Some(_) => panic!("Unknown video decode task"),
                            None => {}
                        }
                    }
                    Ok(())
                };

            while let Ok(packet) = packet_cons.recv() {
                decoder.send_packet(&packet)?;
                receive_and_process_decoded_frames(&mut decoder, &mut scaler)?;
            }

            decoder.send_eof()?;
            receive_and_process_decoded_frames(&mut decoder, &mut scaler)?;

            Ok(())
        })
    }

    fn audio_decode(
        mut decoder: decoder::Audio,
        audio_prod: Sender<frame::Audio>,
        packet_cons: Receiver<ffmpeg::packet::Packet>,
        audio_config: cpal::SupportedStreamConfig,
        task: Arc<Mutex<Option<Task>>>,
    ) -> JoinHandle<Result<(), Error>> {
        thread::spawn(move || {
            let mut resampler = ffmpeg::software::resampling::Context::get(
                decoder.format(),
                decoder.channel_layout(),
                decoder.rate(),
                audio_config.sample_format().as_ffmpeg_sample(),
                ChannelLayout::default(audio_config.channels().into()),
                audio_config.sample_rate(),
            )?;

            let receive_and_process_decoded_frames =
                |decoder: &mut ffmpeg::decoder::Audio,
                 resampler: &mut ffmpeg::software::resampling::Context|
                 -> Result<(), Error> {
                    let mut decoded = frame::Audio::empty();
                    while decoder.receive_frame(&mut decoded).is_ok() {
                        let mut frame = frame::Audio::empty();
                        resampler.run(&decoded, &mut frame)?;
                        if audio_prod.send(frame).is_err() {
                            break;
                        }

                        let mut task = task.lock().unwrap();
                        match *task {
                            Some(Task::Flush) => {
                                decoder.flush();
                                Self::audio_flush(resampler, None)?;
                                while !packet_cons.is_empty() {
                                    let _ = packet_cons.try_recv();
                                }
                                *task = None;
                                drop(task);
                                while !audio_prod.is_empty() {
                                    thread::sleep(Duration::from_micros(10));
                                }
                                break;
                            }
                            Some(_) => panic!("Unknown audio decode task"),
                            None => {}
                        }
                    }
                    Ok(())
                };

            while let Ok(packet) = packet_cons.recv() {
                decoder.send_packet(&packet)?;
                receive_and_process_decoded_frames(&mut decoder, &mut resampler)?;
            }

            decoder.send_eof()?;
            receive_and_process_decoded_frames(&mut decoder, &mut resampler)?;
            Self::audio_flush(&mut resampler, Some(&audio_prod))?;

            Ok(())
        })
    }

    fn audio_flush(
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: Option<&Sender<frame::Audio>>,
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

            if let Some(audio_prod) = audio_prod {
                if audio_prod.send(frame).is_err() {
                    break;
                }
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
