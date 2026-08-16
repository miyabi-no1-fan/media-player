extern crate ffmpeg_next as ffmpeg;

use std::collections::VecDeque;

use cpal::{
    SampleFormat,
    traits::{DeviceTrait, HostTrait},
};
use ffmpeg::frame;
use ringbuf::traits::Consumer;

pub fn audio_init(
    consumer: ringbuf::HeapCons<frame::Audio>,
) -> (cpal::Stream, cpal::SupportedStreamConfig) {
    let device = cpal::default_host().default_output_device().unwrap();
    let config = device.default_output_config().unwrap();

    assert_eq!(config.sample_format(), SampleFormat::F32);

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
                if queue.len() < data.len() {
                    for audio in consumer.pop_iter() {
                        queue.extend(unsafe {
                            std::slice::from_raw_parts(
                                audio.data(0).as_ptr() as *const T,
                                audio.samples() * audio.channels() as usize,
                            )
                        });
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
