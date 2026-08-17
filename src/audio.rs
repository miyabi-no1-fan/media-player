use std::{
    collections::VecDeque,
    sync::{Arc, Mutex},
    time::Duration,
};

use cpal::{
    SampleFormat,
    traits::{DeviceTrait, HostTrait},
};
use crossbeam_channel::{Receiver, RecvTimeoutError};
use ffmpeg::frame;

#[allow(dead_code)]
pub enum InitOpts {
    /// Stable, and mostly the best opts
    Default,
    /// Unstable.
    /// Usually is just a higher sampling rate
    Best,
}

/// `InitOpts::Default` will use `cpal`'s default device and config
///
/// `InitOpts::Best` will try to find the best device
/// with the best config using `cpal`'s `cmp_default_heuristics`
/// ### Usage
/// ```rust
/// let (audio_stream, audio_config) = audio_init(audio_consumer, InitOpts::Default);
/// ```
pub fn audio_init(
    consumer: Receiver<frame::Audio>,
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
    consumer: Receiver<frame::Audio>,
) -> cpal::Stream
where
    T: frame::audio::Sample + cpal::SizedSample + std::marker::Send + 'static + From<u8>,
{
    let mut queue = VecDeque::new();

    let stream = device
        .build_output_stream(
            config,
            move |data: &mut [T], _| {
                while queue.len() < data.len() {
                    match consumer.recv_timeout(Duration::from_millis(1)) {
                        Ok(audio) => queue.extend(unsafe {
                            std::slice::from_raw_parts(
                                audio.data(0).as_ptr() as *const T,
                                audio.samples() * audio.channels() as usize,
                            )
                        }),
                        Err(RecvTimeoutError::Timeout) => {
                            eprintln!("Audio: A buffer underrun occurred.");
                            break;
                        }
                        _ => break,
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

    let mut best_device = host.default_output_device().unwrap();
    let default_conf = best_device.default_output_config().unwrap();

    match opts {
        InitOpts::Default => return (best_device, default_conf),
        _ => {}
    }

    let devices = host.output_devices().unwrap();
    let mut best_conf_range = best_device
        .supported_output_configs()
        .unwrap()
        .next()
        .unwrap();
    for device in devices {
        if let Ok(configs) = device.supported_output_configs() {
            for config in configs {
                if config.cmp_default_heuristics(&best_conf_range).is_gt() {
                    if try_build_audio(&device, config.with_max_sample_rate()) {
                        best_device = device.clone();
                        best_conf_range = config;
                    }
                }
            }
        };
    }

    (best_device, best_conf_range.with_max_sample_rate())
}

fn try_build_audio(device: &cpal::Device, config: cpal::SupportedStreamConfig) -> bool {
    let err = Arc::new(Mutex::new(false));
    let res = {
        let err = err.clone();
        let errfn = move |_| *err.lock().unwrap() = false;
        match config.sample_format() {
            SampleFormat::U8 => device
                .build_output_stream(config.into(), |_: &mut [u8], _| {}, errfn, None)
                .ok(),
            SampleFormat::I16 => device
                .build_output_stream(config.into(), |_: &mut [i16], _| {}, errfn, None)
                .ok(),
            SampleFormat::I32 => device
                .build_output_stream(config.into(), |_: &mut [i32], _| {}, errfn, None)
                .ok(),
            SampleFormat::I64 => device
                .build_output_stream(config.into(), |_: &mut [i64], _| {}, errfn, None)
                .ok(),
            SampleFormat::F32 => device
                .build_output_stream(config.into(), |_: &mut [f32], _| {}, errfn, None)
                .ok(),
            SampleFormat::F64 => device
                .build_output_stream(config.into(), |_: &mut [f64], _| {}, errfn, None)
                .ok(),
            _ => None,
        }
    };
    if res.is_some() && !*err.lock().unwrap() {
        true
    } else {
        false
    }
}
