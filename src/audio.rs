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

use crate::Error;

#[allow(dead_code)]
#[derive(Debug)]
pub enum InitOpts {
    /// Stable, and mostly the best opts
    Default,
    /// Unstable.
    /// Usually is just a higher sampling rate.
    /// Too high sampling rate may cause some jiter.
    Best,
}

#[derive(Debug)]
pub enum Status {
    /// Audio is still running
    Running,
    /// Audio has finish, no more audio to play.
    Finish,
}

#[derive(Debug)]
pub enum Task {
    Pause,
    Play,
}

#[derive(Debug)]
pub struct Control {
    pub task: Mutex<Task>,
    pub status: Mutex<Status>,
}

/// ## Notice
/// Audio required decoder to send resampled data that matches `audio_config`.
///
/// Audio will play nothing if `recv` fail.
///
/// Internally, audio has its own batching queue so, you should check if `status` is false which indicates all queue is empty to exit safely.
///
/// If `status` is false, `audio_stream` **will play nothing** until `status` is set back to true.
///
/// ## Arguments
/// `InitOpts::Default` will use `cpal`'s default device and config
///
/// `InitOpts::Best` will try to find the best device
/// with the best config using `cpal`'s `cmp_default_heuristics`.
///
/// ## Usage
/// ```rust
/// let (audio_stream, audio_config, audio_control) = audio_init(audio_consumer, InitOpts::Default);
/// ```
pub fn audio_init(
    consumer: Receiver<frame::Audio>,
    init_opts: InitOpts,
) -> Result<(cpal::Stream, cpal::SupportedStreamConfig, Arc<Control>), Error> {
    let (device, config) = cpal_init(init_opts)?;

    let (stream, ctrl) = match config.sample_format() {
        SampleFormat::U8 => build_audio::<u8>(device, config.into(), consumer),
        SampleFormat::I16 => build_audio::<i16>(device, config.into(), consumer),
        SampleFormat::I32 => build_audio::<i32>(device, config.into(), consumer),
        // SampleFormat::I64 => build_audio::<i64>(device, config.into(), consumer),
        SampleFormat::F32 => build_audio::<f32>(device, config.into(), consumer),
        SampleFormat::F64 => build_audio::<f64>(device, config.into(), consumer),
        _ => {
            return Err(Error::Audio(cpal::ErrorKind::UnsupportedConfig.into()));
        }
    }?;

    Ok((stream, config, ctrl))
}

fn build_audio<T>(
    device: cpal::Device,
    config: cpal::StreamConfig,
    consumer: Receiver<frame::Audio>,
) -> Result<(cpal::Stream, Arc<Control>), Error>
where
    T: frame::audio::Sample + cpal::SizedSample + std::marker::Send + 'static + From<u8>,
{
    let mut queue = VecDeque::new();

    // `sample_rate` is like `fps` but for audio.
    // So `1 / sample_rate` is audio equivalent of `frame_time`.
    // We have at most `avail_dur` to process before data got underrun.
    let avail_dur = 1.0 / config.sample_rate as f64;

    let ctrl = Arc::new(Control {
        task: Mutex::new(Task::Play),
        status: Mutex::new(Status::Running),
    });

    let stream = {
        let ctrl = ctrl.clone();
        device.build_output_stream(
            config,
            move |data: &mut [T], _| {
                let mut play = || {
                    if let Status::Finish = *ctrl.status.lock().unwrap() {
                        data.fill(T::from(0));
                        return;
                    }

                    while queue.len() < data.len() {
                        match consumer.recv_timeout(Duration::from_secs_f64(avail_dur / 2.0)) {
                            Ok(audio) => {
                                let mut len = audio.samples() * audio.channels() as usize;

                                if len > audio.data(0).len() {
                                    len = audio.data(0).len();
                                }

                                let misalignment = len % size_of::<T>();
                                if misalignment > 0 {
                                    len -= misalignment;
                                }

                                // SAFETY: assertions for safety
                                assert!(len <= audio.data(0).len());
                                assert!(len % size_of::<T>() == 0);
                                queue.extend(unsafe {
                                    std::slice::from_raw_parts(
                                        audio.data(0).as_ptr() as *const T,
                                        len,
                                    )
                                });
                            }
                            Err(RecvTimeoutError::Timeout) => {
                                eprintln!("Audio: A buffer underrun occurred.");
                                break;
                            }
                            Err(_) if queue.is_empty() => {
                                *ctrl.status.lock().unwrap() = Status::Finish;
                                return;
                            }
                            Err(_) => break,
                        }
                    }

                    let n = usize::min(queue.len(), data.len());

                    // unwrap is safe here. We already check for `n`
                    for dst in data[..n].iter_mut() {
                        *dst = queue.pop_front().unwrap();
                    }

                    data[n..].fill(T::from(0));
                };

                match *ctrl.task.lock().unwrap() {
                    Task::Pause => data.fill(T::from(0)),
                    Task::Play => play(),
                }
            },
            |e| eprintln!("Audio: {e}"),
            None,
        )
    }?;

    Ok((stream, ctrl))
}

fn cpal_init(opts: InitOpts) -> Result<(cpal::Device, cpal::SupportedStreamConfig), Error> {
    let host = cpal::default_host();

    let mut best_device = host
        .default_output_device()
        .ok_or(Error::Log("No audio output device."))?;
    let default_conf = best_device.default_output_config()?;

    match opts {
        InitOpts::Default => return Ok((best_device, default_conf)),
        _ => {}
    }

    let devices = host.output_devices()?;
    let Some(mut best_conf_range) = best_device.supported_output_configs()?.next() else {
        return Ok((best_device, default_conf));
    };

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

    Ok((best_device, best_conf_range.with_max_sample_rate()))
}

fn try_build_audio(device: &cpal::Device, config: cpal::SupportedStreamConfig) -> bool {
    let err = Arc::new(Mutex::new(false));
    let res = {
        let err = err.clone();
        let errfn = move |_| *err.lock().unwrap() = false; // lock is not poisoned
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
    // lock is not poisoned
    if res.is_some() && !*err.lock().unwrap() {
        true
    } else {
        false
    }
}
