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

pub struct Decoder {
    ictx: format::context::Input,

    video_decoder: decoder::Video,
    video_strm_index: usize,

    audio_decoder: decoder::Audio,
    audio_strm_index: usize,
}

impl Decoder {
    pub fn new<P>(path: &P) -> (Self, f64, u32, u32)
    where
        P: AsRef<std::path::Path> + ?Sized,
    {
        ffmpeg::init().unwrap();
        let ictx = ffmpeg::format::input(path).unwrap();

        let audio_strm = ictx.streams().best(media::Type::Audio).unwrap();
        let audio_strm_index = audio_strm.index();
        let audio_decoder = codec::Context::from_parameters(audio_strm.parameters())
            .unwrap()
            .decoder()
            .audio()
            .unwrap();

        let video_strm = ictx.streams().best(media::Type::Video).unwrap();
        let video_strm_index = video_strm.index();
        let video_decoder = codec::Context::from_parameters(video_strm.parameters())
            .unwrap()
            .decoder()
            .video()
            .unwrap();

        let fps = f64::from(video_strm.avg_frame_rate());
        let (width, height) = (video_decoder.width(), video_decoder.height());

        (
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
        )
    }

    /// Spawn 1 thread to start decoding.
    pub fn decode(
        mut decoder: Decoder,
        audio_config: cpal::SupportedStreamConfig,
        mut video_prod: Sender<frame::Video>,
        mut audio_prod: Sender<frame::Audio>,
    ) -> thread::JoinHandle<()> {
        thread::spawn(move || {
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
            )
            .unwrap();

            let mut resampler = ffmpeg::software::resampling::Context::get(
                decoder.audio_decoder.format(),
                decoder.audio_decoder.channel_layout(),
                decoder.audio_decoder.rate(),
                audio_config.sample_format().as_ffmpeg_sample(),
                ChannelLayout::default(audio_config.channels().into()),
                audio_config.sample_rate(),
            )
            .unwrap();

            for (stream, packet) in decoder.ictx.packets() {
                match stream.index() {
                    i if i == decoder.video_strm_index => {
                        decoder.video_decoder.send_packet(&packet).unwrap();
                        if Self::video_decode(
                            &mut decoder.video_decoder,
                            &mut scaler,
                            &mut video_prod,
                        )
                        .is_err()
                        {
                            break;
                        }
                    }
                    i if i == decoder.audio_strm_index => {
                        decoder.audio_decoder.send_packet(&packet).unwrap();
                        if Self::audio_decode(
                            &mut decoder.audio_decoder,
                            &mut resampler,
                            &mut audio_prod,
                        )
                        .is_err()
                        {
                            break;
                        }
                    }
                    _ => {}
                }

                while video_prod.len() > 10 && audio_prod.len() > 10 {
                    thread::sleep(Duration::from_micros(100));
                }
            }

            decoder.video_decoder.send_eof().unwrap();
            decoder.audio_decoder.send_eof().unwrap();
            let _ = Self::video_decode(&mut decoder.video_decoder, &mut scaler, &mut video_prod);
            let _ = Self::audio_decode(&mut decoder.audio_decoder, &mut resampler, &mut audio_prod);
            Self::audio_flush(&mut resampler, &mut audio_prod);
        })
    }

    fn video_decode(
        decoder: &mut decoder::Video,
        scaler: &mut ffmpeg::software::scaling::Context,
        video_prod: &mut Sender<frame::Video>,
    ) -> Result<(), ()> {
        loop {
            let mut decoded = frame::Video::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Video::empty();
            scaler.run(&decoded, &mut frame).unwrap();

            if video_prod.send(frame).is_err() {
                return Err(());
            };
        }
        Ok(())
    }

    fn audio_decode(
        decoder: &mut decoder::Audio,
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: &mut Sender<frame::Audio>,
    ) -> Result<(), ()> {
        loop {
            let mut decoded = frame::Audio::empty();

            if decoder.receive_frame(&mut decoded).is_err() {
                break;
            }

            let mut frame = frame::Audio::empty();
            let _ = resampler.run(&decoded, &mut frame).unwrap();

            if audio_prod.send(frame).is_err() {
                return Err(());
            }
        }
        Ok(())
    }

    fn audio_flush(
        resampler: &mut ffmpeg::software::resampling::Context,
        audio_prod: &mut Sender<frame::Audio>,
    ) {
        loop {
            let mut frame = frame::Audio::new(
                resampler.output().format,
                0, // flush will allocate
                resampler.output().channel_layout,
            );
            let _ = resampler.flush(&mut frame).unwrap();

            if frame.samples() == 0 {
                break;
            }

            if audio_prod.send(frame).is_err() {
                break;
            }
        }
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
