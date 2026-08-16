extern crate ffmpeg_next as ffmpeg;

use std::{collections::VecDeque, thread, time::Duration};

use cpal::{
    SampleFormat,
    traits::{DeviceTrait, HostTrait},
};
use ffmpeg::frame;
use ringbuf::traits::{Consumer, Observer};

#[allow(dead_code)]
pub enum InitOpts {
    Default,
    Best,
}

pub fn audio_init(
    consumer: ringbuf::HeapCons<frame::Audio>,
    init_opts: InitOpts,
) -> (cpal::Stream, cpal::SupportedStreamConfig) {
    let (device, config) = cpal_init(init_opts);

    let stream = match config.sample_format() {
        SampleFormat::U8 => build_audio::<u8>(device, config.into(), consumer),
        SampleFormat::I16 => build_audio::<i16>(device, config.into(), consumer),
        SampleFormat::I32 => build_audio::<i32>(device, config.into(), consumer),
        // SampleFormat::I64 => build_audio::<i64>(device, config.into(), consumer),
        SampleFormat::F32 => build_audio::<f32>(device, config.into(), consumer),
        SampleFormat::F64 => build_audio::<f64>(device, config.into(), consumer),
        _ => panic!("Audio: Unsupported Sample Format"),
    };

    (stream, config)
}

fn build_audio<T>(
    device: cpal::Device,
    config: cpal::StreamConfig,
    mut consumer: ringbuf::HeapCons<frame::Audio>,
) -> cpal::Stream
where
    T: frame::audio::Sample + cpal::SizedSample + std::marker::Send + 'static + From<u8>,
{
    let mut queue = VecDeque::new();

    let stream = device
        .build_output_stream(
            config,
            move |data: &mut [T], _| {
                while queue.len() < data.len() && (!consumer.is_empty() || consumer.write_is_held())
                {
                    for audio in consumer.pop_iter() {
                        queue.extend(unsafe {
                            std::slice::from_raw_parts(
                                audio.data(0).as_ptr() as *const T,
                                audio.samples() * audio.channels() as usize,
                            )
                        });
                    }
                    if queue.len() < data.len() {
                        thread::sleep(Duration::from_micros(10));
                    }
                }

                let n = usize::min(queue.len(), data.len());

                for dst in data[..n].iter_mut() {
                    *dst = queue.pop_front().unwrap();
                }

                data[n..].fill(T::from(0));
            },
            |e| eprintln!("Audio: {e}"),
            None,
        )
        .unwrap();

    stream
}

fn cpal_init(opts: InitOpts) -> (cpal::Device, cpal::SupportedStreamConfig) {
    let host = cpal::default_host();

    let mut best: (cpal::Device, cpal::SupportedStreamConfig) = {
        let dev = host.default_output_device().unwrap();
        let conf = dev.default_output_config().unwrap();
        (dev, conf)
    };

    match opts {
        InitOpts::Default => return best,
        _ => {}
    }

    let devices = host.output_devices().unwrap();
    for device in devices {
        if let Ok(configs) = device.supported_output_configs() {
            for config in configs {
                let config = config.with_max_sample_rate();
                if config_score(config) > config_score(best.1) {
                    if try_build_audio(&device, config).is_some() {
                        best = (device.to_owned(), config);
                    }
                }
            }
        };
    }

    best
}

fn config_score(config: cpal::SupportedStreamConfig) -> (u32, u16, u32) {
    (
        config.sample_rate(),
        config.channels(),
        match config.sample_format() {
            SampleFormat::U8 => 1,
            SampleFormat::I16 => 2,
            SampleFormat::I32 => 3,
            // SampleFormat::I64 => 4,
            SampleFormat::F32 => 5,
            SampleFormat::F64 => 6,
            _ => 0,
        },
    )
}

fn try_build_audio(
    device: &cpal::Device,
    config: cpal::SupportedStreamConfig,
) -> Option<cpal::Stream> {
    match config.sample_format() {
        SampleFormat::U8 => device
            .build_output_stream(config.into(), move |_: &mut [u8], _| {}, |_| {}, None)
            .ok(),
        SampleFormat::I16 => device
            .build_output_stream(config.into(), move |_: &mut [i16], _| {}, |_| {}, None)
            .ok(),
        SampleFormat::I32 => device
            .build_output_stream(config.into(), move |_: &mut [i32], _| {}, |_| {}, None)
            .ok(),
        SampleFormat::I64 => device
            .build_output_stream(config.into(), move |_: &mut [i64], _| {}, |_| {}, None)
            .ok(),
        SampleFormat::F32 => device
            .build_output_stream(config.into(), move |_: &mut [f32], _| {}, |_| {}, None)
            .ok(),
        SampleFormat::F64 => device
            .build_output_stream(config.into(), move |_: &mut [f64], _| {}, |_| {}, None)
            .ok(),
        _ => None,
    }
}
