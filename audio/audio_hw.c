// Copyright (C) 2026 apkreader
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "MarieAudioHAL"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sound/asound.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <system/audio.h>
#include <time.h>
#include <unistd.h>

#define MARIE_PCM_PLAYBACK_DEV "/dev/snd/pcmC0D0p"
#define MARIE_PCM_CAPTURE_DEV  "/dev/snd/pcmC0D0c"
#define MARIE_MIXER_DEV        "/dev/snd/controlC0"

#define MARIE_HW_RATE          48000U
#define MARIE_HW_CHANNELS      2U
#define MARIE_PERIOD_FRAMES    960U
#define MARIE_PERIOD_COUNT     4U
#define MARIE_BUFFER_FRAMES    (MARIE_PERIOD_FRAMES * MARIE_PERIOD_COUNT)

enum {
    MARIE_OUT_SPEAKER    = 0,
    MARIE_OUT_HEADPHONES = 1,
    MARIE_OUT_EARPIECE   = 2,
    MARIE_OUT_SPK_HP     = 3,
};

enum {
    MARIE_IN_MAIN_MIC    = 0,
    MARIE_IN_HEADSET_MIC = 1,
};

struct marie_audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    audio_mode_t mode;
    audio_devices_t out_devices;
    audio_devices_t in_device;
    bool mic_mute;
    float voice_volume;
    audio_patch_handle_t next_patch_handle;
};

struct marie_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    struct marie_audio_device *dev;
    int pcm_fd;
    bool standby;
    audio_devices_t devices;
    audio_output_flags_t flags;
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    uint64_t written_frames;
};

struct marie_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;
    struct marie_audio_device *dev;
    int pcm_fd;
    bool standby;
    audio_devices_t device;
    uint32_t sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    int64_t read_frames;
    int16_t hw_buf[MARIE_PERIOD_FRAMES * MARIE_HW_CHANNELS * 4];
};

static void ctl_set_enum(const char *name, unsigned int item) {
    int fd = open(MARIE_MIXER_DEV, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    struct snd_ctl_elem_value ev;
    memset(&ev, 0, sizeof(ev));
    ev.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    strncpy((char *)ev.id.name, name, sizeof(ev.id.name) - 1);
    ev.value.enumerated.item[0] = item;
    if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev) < 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                            "ctl_set_enum(%s=%u) failed: %s", name, item, strerror(errno));
    }
    close(fd);
}

static void ctl_set_int(const char *name, long val) {
    int fd = open(MARIE_MIXER_DEV, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    struct snd_ctl_elem_value ev;
    memset(&ev, 0, sizeof(ev));
    ev.id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
    strncpy((char *)ev.id.name, name, sizeof(ev.id.name) - 1);
    ev.value.integer.value[0] = val;
    ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &ev);
    close(fd);
}

static void apply_output_route_locked(struct marie_audio_device *adev, audio_devices_t devs) {
    if (devs == AUDIO_DEVICE_NONE) {
        return;
    }
    adev->out_devices = devs;
    bool spk = (devs & AUDIO_DEVICE_OUT_SPEAKER) != 0;
    bool hp = (devs & (AUDIO_DEVICE_OUT_WIRED_HEADSET |
                       AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
                       AUDIO_DEVICE_OUT_LINE)) != 0;
    bool ear = (devs & AUDIO_DEVICE_OUT_EARPIECE) != 0;

    unsigned int route = MARIE_OUT_SPEAKER;
    if (spk && hp) {
        route = MARIE_OUT_SPK_HP;
    } else if (hp) {
        route = MARIE_OUT_HEADPHONES;
    } else if (ear && !spk) {
        route = MARIE_OUT_EARPIECE;
    } else {
        route = MARIE_OUT_SPEAKER;
    }

    ctl_set_enum("Output Path", route);
}

static void apply_input_route_locked(struct marie_audio_device *adev, audio_devices_t dev) {
    uint32_t d = dev & ~AUDIO_DEVICE_BIT_IN;
    if (d == 0) {
        return;
    }
    adev->in_device = dev;
    unsigned int mic = (d & (AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN))
                               ? MARIE_IN_HEADSET_MIC
                               : MARIE_IN_MAIN_MIC;
    ctl_set_enum("Capture Mic", mic);
}

static void hw_param_mask_set(struct snd_pcm_hw_params *p, int var, unsigned int bit) {
    struct snd_mask *m = &p->masks[var - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m, 0, sizeof(*m));
    m->bits[bit / 32U] = (1U << (bit % 32U));
}

static void hw_param_interval_set(struct snd_pcm_hw_params *p, int var, unsigned int val) {
    struct snd_interval *iv = &p->intervals[var - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    iv->min = val;
    iv->max = val;
    iv->openmin = 0;
    iv->openmax = 0;
    iv->integer = 1;
    iv->empty = 0;
}

static int open_alsa_pcm(const char *path, bool capture) {
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "open(%s) failed: %s",
                            path, strerror(errno));
        return -1;
    }

    struct snd_pcm_hw_params hw;
    memset(&hw, 0, sizeof(hw));
    hw.rmask = ~0U;
    hw.cmask = 0;
    hw.info = ~0U;
    for (size_t i = 0; i < sizeof(hw.masks) / sizeof(hw.masks[0]); ++i) {
        memset(hw.masks[i].bits, 0xff, sizeof(hw.masks[i].bits));
    }
    for (size_t i = 0; i < sizeof(hw.intervals) / sizeof(hw.intervals[0]); ++i) {
        hw.intervals[i].min = 0;
        hw.intervals[i].max = ~0U;
    }

    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_REFINE, &hw) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "HW_REFINE(%s) failed: %s",
                            path, strerror(errno));
        close(fd);
        return -1;
    }

    hw_param_mask_set(&hw, SNDRV_PCM_HW_PARAM_ACCESS,
                      (unsigned int)SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    hw_param_mask_set(&hw, SNDRV_PCM_HW_PARAM_FORMAT,
                      (unsigned int)SNDRV_PCM_FORMAT_S16_LE);
    hw_param_mask_set(&hw, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                      (unsigned int)SNDRV_PCM_SUBFORMAT_STD);
    hw_param_interval_set(&hw, SNDRV_PCM_HW_PARAM_CHANNELS, MARIE_HW_CHANNELS);
    hw_param_interval_set(&hw, SNDRV_PCM_HW_PARAM_RATE, MARIE_HW_RATE);
    hw_param_interval_set(&hw, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, MARIE_PERIOD_FRAMES);
    hw_param_interval_set(&hw, SNDRV_PCM_HW_PARAM_PERIODS, MARIE_PERIOD_COUNT);
    hw_param_interval_set(&hw, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, MARIE_BUFFER_FRAMES);
    hw.flags = 0;
    hw.rmask = ~0U;

    if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hw) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "HW_PARAMS(%s) failed: %s",
                            path, strerror(errno));
        close(fd);
        return -1;
    }

    struct snd_pcm_sw_params sw;
    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_ENABLE;
    sw.period_step = 1;
    sw.avail_min = MARIE_PERIOD_FRAMES;
    sw.start_threshold = capture ? 1 : MARIE_PERIOD_FRAMES;
    sw.stop_threshold = MARIE_BUFFER_FRAMES;
    sw.silence_threshold = 0;
    sw.silence_size = 0;
    sw.boundary = MARIE_BUFFER_FRAMES;
    while (sw.boundary * 2 <= (snd_pcm_uframes_t)LONG_MAX / 2) {
        sw.boundary *= 2;
    }

    if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0) {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "SW_PARAMS(%s) failed: %s",
                            path, strerror(errno));
    }

    if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "PREPARE(%s) failed: %s",
                            path, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static bool parse_int_param(const char *kv_pairs, const char *key, long *out_val) {
    if (!kv_pairs || !key) {
        return false;
    }
    size_t klen = strlen(key);
    const char *p = kv_pairs;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char *end = NULL;
            long v = strtol(p + klen + 1, &end, 10);
            if (end != p + klen + 1) {
                *out_val = v;
                return true;
            }
        }
        const char *semi = strchr(p, ';');
        if (!semi) {
            break;
        }
        p = semi + 1;
    }
    return false;
}

/* ==================== Output Stream ==================== */

static uint32_t out_get_sample_rate(const struct audio_stream *stream) {
    const struct marie_stream_out *out = (const struct marie_stream_out *)stream;
    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate) {
    return (rate == MARIE_HW_RATE) ? 0 : -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream) {
    return MARIE_PERIOD_FRAMES * MARIE_HW_CHANNELS * sizeof(int16_t);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream) {
    const struct marie_stream_out *out = (const struct marie_stream_out *)stream;
    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream) {
    const struct marie_stream_out *out = (const struct marie_stream_out *)stream;
    return out->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format) {
    return (format == AUDIO_FORMAT_PCM_16_BIT) ? 0 : -ENOSYS;
}

static int out_standby(struct audio_stream *stream) {
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    if (!out->standby) {
        if (out->pcm_fd >= 0) {
            ioctl(out->pcm_fd, SNDRV_PCM_IOCTL_DROP, 0);
            close(out->pcm_fd);
            out->pcm_fd = -1;
        }
        out->standby = true;
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd) {
    return 0;
}

static audio_devices_t out_get_device(const struct audio_stream *stream) {
    const struct marie_stream_out *out = (const struct marie_stream_out *)stream;
    return out->devices;
}

static int out_set_device(struct audio_stream *stream, audio_devices_t device) {
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    pthread_mutex_lock(&out->dev->lock);
    out->devices = device;
    apply_output_route_locked(out->dev, device);
    pthread_mutex_unlock(&out->dev->lock);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kv_pairs) {
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    long val = 0;
    if (parse_int_param(kv_pairs, AUDIO_PARAMETER_STREAM_ROUTING, &val) && val != 0) {
        pthread_mutex_lock(&out->dev->lock);
        out->devices = (audio_devices_t)val;
        apply_output_route_locked(out->dev, (audio_devices_t)val);
        pthread_mutex_unlock(&out->dev->lock);
    }
    return 0;
}

static char *out_get_parameters(const struct audio_stream *stream, const char *keys) {
    return strdup("");
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream) {
    return (MARIE_BUFFER_FRAMES * 1000U) / MARIE_HW_RATE;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right) {
    return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes) {
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    const size_t frame_bytes = MARIE_HW_CHANNELS * sizeof(int16_t);
    const size_t frames_total = bytes / frame_bytes;
    if (frames_total == 0) {
        return 0;
    }

    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        pthread_mutex_lock(&out->dev->lock);
        apply_output_route_locked(out->dev, out->devices ? out->devices : out->dev->out_devices);
        pthread_mutex_unlock(&out->dev->lock);

        out->pcm_fd = open_alsa_pcm(MARIE_PCM_PLAYBACK_DEV, false);
        out->standby = false;
    }

    if (out->pcm_fd < 0) {
        out->written_frames += frames_total;
        pthread_mutex_unlock(&out->lock);
        usleep((useconds_t)((frames_total * 1000000ULL) / MARIE_HW_RATE));
        return (ssize_t)bytes;
    }

    const uint8_t *ptr = (const uint8_t *)buffer;
    size_t rem = frames_total;
    while (rem > 0) {
        struct snd_xferi xfer;
        memset(&xfer, 0, sizeof(xfer));
        xfer.buf = (void *)ptr;
        xfer.frames = (snd_pcm_uframes_t)rem;

        if (ioctl(out->pcm_fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &xfer) < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            if (err == EPIPE || err == ESTRPIPE) {
                ioctl(out->pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0);
                continue;
            }
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                                "WRITEI_FRAMES failed (%s), pacing fallback", strerror(err));
            close(out->pcm_fd);
            out->pcm_fd = -1;
            usleep((useconds_t)((rem * 1000000ULL) / MARIE_HW_RATE));
            break;
        }
        size_t done = (xfer.result > 0) ? (size_t)xfer.result : rem;
        if (done > rem) {
            done = rem;
        }
        ptr += done * frame_bytes;
        rem -= done;
    }

    out->written_frames += frames_total;
    pthread_mutex_unlock(&out->lock);
    return (ssize_t)bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames) {
    if (!dsp_frames) {
        return -EINVAL;
    }
    const struct marie_stream_out *out = (const struct marie_stream_out *)stream;
    *dsp_frames = (uint32_t)out->written_frames;
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream, int64_t *timestamp) {
    return -ENOSYS;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp) {
    if (!frames || !timestamp) {
        return -EINVAL;
    }
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    *frames = out->written_frames;
    clock_gettime(CLOCK_MONOTONIC, timestamp);
    pthread_mutex_unlock(&out->lock);
    return 0;
}

/* ==================== Input Stream ==================== */

static uint32_t in_get_sample_rate(const struct audio_stream *stream) {
    const struct marie_stream_in *in = (const struct marie_stream_in *)stream;
    return in->sample_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate) {
    return (rate == ((struct marie_stream_in *)stream)->sample_rate) ? 0 : -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream) {
    const struct marie_stream_in *in = (const struct marie_stream_in *)stream;
    size_t ch = audio_channel_count_from_in_mask(in->channel_mask);
    if (ch == 0) ch = 1;
    size_t frames = (MARIE_PERIOD_FRAMES * in->sample_rate) / MARIE_HW_RATE;
    if (frames == 0) frames = 160;
    return frames * ch * sizeof(int16_t);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream) {
    const struct marie_stream_in *in = (const struct marie_stream_in *)stream;
    return in->channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream) {
    const struct marie_stream_in *in = (const struct marie_stream_in *)stream;
    return in->format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format) {
    return (format == AUDIO_FORMAT_PCM_16_BIT) ? 0 : -ENOSYS;
}

static int in_standby(struct audio_stream *stream) {
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    pthread_mutex_lock(&in->lock);
    if (!in->standby) {
        if (in->pcm_fd >= 0) {
            ioctl(in->pcm_fd, SNDRV_PCM_IOCTL_DROP, 0);
            close(in->pcm_fd);
            in->pcm_fd = -1;
        }
        in->standby = true;
    }
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd) {
    return 0;
}

static audio_devices_t in_get_device(const struct audio_stream *stream) {
    const struct marie_stream_in *in = (const struct marie_stream_in *)stream;
    return in->device;
}

static int in_set_device(struct audio_stream *stream, audio_devices_t device) {
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    pthread_mutex_lock(&in->dev->lock);
    in->device = device;
    apply_input_route_locked(in->dev, device);
    pthread_mutex_unlock(&in->dev->lock);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kv_pairs) {
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    long val = 0;
    if (parse_int_param(kv_pairs, AUDIO_PARAMETER_STREAM_ROUTING, &val) && val != 0) {
        pthread_mutex_lock(&in->dev->lock);
        in->device = (audio_devices_t)val;
        apply_input_route_locked(in->dev, (audio_devices_t)val);
        pthread_mutex_unlock(&in->dev->lock);
    }
    return 0;
}

static char *in_get_parameters(const struct audio_stream *stream, const char *keys) {
    return strdup("");
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect) {
    return 0;
}

static int in_set_gain(struct audio_stream_in *stream, float gain) {
    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer, size_t bytes) {
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    size_t out_ch = audio_channel_count_from_in_mask(in->channel_mask);
    if (out_ch == 0) out_ch = 1;
    const size_t out_frame_bytes = out_ch * sizeof(int16_t);
    const size_t out_frames_req = bytes / out_frame_bytes;
    if (out_frames_req == 0) {
        return 0;
    }

    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        pthread_mutex_lock(&in->dev->lock);
        apply_input_route_locked(in->dev, in->device ? in->device : in->dev->in_device);
        pthread_mutex_unlock(&in->dev->lock);

        in->pcm_fd = open_alsa_pcm(MARIE_PCM_CAPTURE_DEV, true);
        in->standby = false;
    }

    if (in->pcm_fd < 0) {
        memset(buffer, 0, bytes);
        in->read_frames += (int64_t)out_frames_req;
        pthread_mutex_unlock(&in->lock);
        usleep((useconds_t)((out_frames_req * 1000000ULL) / in->sample_rate));
        return (ssize_t)bytes;
    }

    int16_t *dst = (int16_t *)buffer;
    size_t out_done = 0;
    while (out_done < out_frames_req) {
        size_t chunk_out = out_frames_req - out_done;
        size_t chunk_hw = (chunk_out * MARIE_HW_RATE + in->sample_rate - 1) / in->sample_rate;
        const size_t max_hw = sizeof(in->hw_buf) / (MARIE_HW_CHANNELS * sizeof(int16_t));
        if (chunk_hw > max_hw) {
            chunk_hw = max_hw;
            chunk_out = (chunk_hw * in->sample_rate) / MARIE_HW_RATE;
            if (chunk_out == 0) chunk_out = 1;
        }

        struct snd_xferi xfer;
        memset(&xfer, 0, sizeof(xfer));
        xfer.buf = in->hw_buf;
        xfer.frames = (snd_pcm_uframes_t)chunk_hw;

        if (ioctl(in->pcm_fd, SNDRV_PCM_IOCTL_READI_FRAMES, &xfer) < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            if (err == EPIPE || err == ESTRPIPE) {
                ioctl(in->pcm_fd, SNDRV_PCM_IOCTL_PREPARE, 0);
                continue;
            }
            memset(dst + out_done * out_ch, 0, (out_frames_req - out_done) * out_frame_bytes);
            close(in->pcm_fd);
            in->pcm_fd = -1;
            usleep((useconds_t)(((out_frames_req - out_done) * 1000000ULL) / in->sample_rate));
            out_done = out_frames_req;
            break;
        }

        size_t got_hw = (xfer.result > 0) ? (size_t)xfer.result : chunk_hw;
        size_t prod_out = (got_hw * in->sample_rate) / MARIE_HW_RATE;
        if (prod_out == 0) prod_out = 1;
        if (out_done + prod_out > out_frames_req) {
            prod_out = out_frames_req - out_done;
        }

        if (in->dev->mic_mute) {
            memset(dst + out_done * out_ch, 0, prod_out * out_frame_bytes);
        } else if (in->sample_rate == MARIE_HW_RATE && got_hw >= prod_out) {
            for (size_t i = 0; i < prod_out; ++i) {
                int16_t l = in->hw_buf[i * 2];
                int16_t r = in->hw_buf[i * 2 + 1];
                if (out_ch == 1) {
                    dst[out_done + i] = (int16_t)(((int32_t)l + (int32_t)r) / 2);
                } else {
                    dst[(out_done + i) * 2] = l;
                    dst[(out_done + i) * 2 + 1] = r;
                }
            }
        } else {
            for (size_t i = 0; i < prod_out; ++i) {
                uint64_t pos_fp = ((uint64_t)i * (uint64_t)got_hw << 16) / prod_out;
                size_t idx0 = (size_t)(pos_fp >> 16);
                size_t idx1 = (idx0 + 1 < got_hw) ? (idx0 + 1) : (got_hw - 1);
                int32_t frac = (int32_t)(pos_fp & 0xffffU);
                int32_t l0 = in->hw_buf[idx0 * 2];
                int32_t r0 = in->hw_buf[idx0 * 2 + 1];
                int32_t l1 = in->hw_buf[idx1 * 2];
                int32_t r1 = in->hw_buf[idx1 * 2 + 1];
                int16_t l = (int16_t)((l0 * (65536 - frac) + l1 * frac) >> 16);
                int16_t r = (int16_t)((r0 * (65536 - frac) + r1 * frac) >> 16);
                if (out_ch == 1) {
                    dst[out_done + i] = (int16_t)(((int32_t)l + (int32_t)r) / 2);
                } else {
                    dst[(out_done + i) * 2] = l;
                    dst[(out_done + i) * 2 + 1] = r;
                }
            }
        }
        out_done += prod_out;
    }

    in->read_frames += (int64_t)out_frames_req;
    pthread_mutex_unlock(&in->lock);
    return (ssize_t)bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream) {
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                   int64_t *frames, int64_t *time) {
    if (!frames || !time) {
        return -EINVAL;
    }
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    struct timespec ts;
    pthread_mutex_lock(&in->lock);
    *frames = in->read_frames;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *time = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    pthread_mutex_unlock(&in->lock);
    return 0;
}

/* ==================== Audio HW Device ==================== */

static int adev_init_check(const struct audio_hw_device *dev) {
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->voice_volume = volume;
    long ear_vol = (long)(volume * 15.0f + 0.5f);
    if (ear_vol < 1) ear_vol = 1;
    if (ear_vol > 15) ear_vol = 15;
    ctl_set_int("Earpiece Volume", ear_vol);
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume) {
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mode = mode;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mic_mute = state;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state) {
    if (!state) return -EINVAL;
    const struct marie_audio_device *adev = (const struct marie_audio_device *)dev;
    *state = adev->mic_mute;
    return 0;
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kv_pairs) {
    return 0;
}

static char *adev_get_parameters(const struct audio_hw_device *dev, const char *keys) {
    return strdup("");
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config) {
    if (!config) {
        return MARIE_PERIOD_FRAMES * MARIE_HW_CHANNELS * sizeof(int16_t);
    }
    size_t ch = audio_channel_count_from_in_mask(config->channel_mask);
    if (ch == 0) ch = 2;
    uint32_t rate = config->sample_rate ? config->sample_rate : MARIE_HW_RATE;
    size_t frames = (MARIE_PERIOD_FRAMES * rate) / MARIE_HW_RATE;
    if (frames == 0) frames = 160;
    return frames * ch * sizeof(int16_t);
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    struct marie_stream_out *out = calloc(1, sizeof(*out));
    if (!out) {
        return -ENOMEM;
    }

    pthread_mutex_init(&out->lock, NULL);
    out->dev = adev;
    out->pcm_fd = -1;
    out->standby = true;
    out->devices = devices ? devices : AUDIO_DEVICE_OUT_SPEAKER;
    out->flags = flags;
    out->sample_rate = MARIE_HW_RATE;
    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    out->format = AUDIO_FORMAT_PCM_16_BIT;

    if (config) {
        config->sample_rate = out->sample_rate;
        config->channel_mask = out->channel_mask;
        config->format = out->format;
    }

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.get_device = out_get_device;
    out->stream.common.set_device = out_set_device;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;

    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    pthread_mutex_lock(&adev->lock);
    apply_output_route_locked(adev, out->devices);
    pthread_mutex_unlock(&adev->lock);

    *stream_out = &out->stream;
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream) {
    if (!stream) return;
    out_standby(&stream->common);
    struct marie_stream_out *out = (struct marie_stream_out *)stream;
    pthread_mutex_destroy(&out->lock);
    free(out);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address,
                                  audio_source_t source) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    struct marie_stream_in *in = calloc(1, sizeof(*in));
    if (!in) {
        return -ENOMEM;
    }

    pthread_mutex_init(&in->lock, NULL);
    in->dev = adev;
    in->pcm_fd = -1;
    in->standby = true;
    in->device = devices ? devices : AUDIO_DEVICE_IN_BUILTIN_MIC;
    in->sample_rate = (config && config->sample_rate) ? config->sample_rate : MARIE_HW_RATE;
    in->channel_mask = (config && config->channel_mask) ? config->channel_mask
                                                        : AUDIO_CHANNEL_IN_STEREO;
    in->format = AUDIO_FORMAT_PCM_16_BIT;

    if (config) {
        config->sample_rate = in->sample_rate;
        config->channel_mask = in->channel_mask;
        config->format = in->format;
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.get_device = in_get_device;
    in->stream.common.set_device = in_set_device;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;

    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;

    pthread_mutex_lock(&adev->lock);
    apply_input_route_locked(adev, in->device);
    pthread_mutex_unlock(&adev->lock);

    *stream_in = &in->stream;
    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                    struct audio_stream_in *stream) {
    if (!stream) return;
    in_standby(&stream->common);
    struct marie_stream_in *in = (struct marie_stream_in *)stream;
    pthread_mutex_destroy(&in->lock);
    free(in);
}

static int adev_dump(const struct audio_hw_device *dev, int fd) {
    return 0;
}

static int adev_create_audio_patch(struct audio_hw_device *dev,
                                   unsigned int num_sources,
                                   const struct audio_port_config *sources,
                                   unsigned int num_sinks,
                                   const struct audio_port_config *sinks,
                                   audio_patch_handle_t *handle) {
    struct marie_audio_device *adev = (struct marie_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);

    audio_devices_t out_devs = AUDIO_DEVICE_NONE;
    for (unsigned int i = 0; i < num_sinks; ++i) {
        if (sinks[i].type == AUDIO_PORT_TYPE_DEVICE) {
            out_devs |= sinks[i].ext.device.type;
        }
    }
    if (out_devs != AUDIO_DEVICE_NONE) {
        apply_output_route_locked(adev, out_devs);
    }

    for (unsigned int i = 0; i < num_sources; ++i) {
        if (sources[i].type == AUDIO_PORT_TYPE_DEVICE) {
            apply_input_route_locked(adev, sources[i].ext.device.type);
        }
    }

    if (handle) {
        if (*handle == AUDIO_PATCH_HANDLE_NONE) {
            *handle = ++adev->next_patch_handle;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_release_audio_patch(struct audio_hw_device *dev,
                                    audio_patch_handle_t handle) {
    return 0;
}

static int adev_set_audio_port_config(struct audio_hw_device *dev,
                                      const struct audio_port_config *config) {
    return 0;
}

static int adev_close(hw_device_t *device) {
    if (!device) return 0;
    struct marie_audio_device *adev = (struct marie_audio_device *)device;
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name, hw_device_t **device) {
    if (!name || strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        return -EINVAL;
    }

    struct marie_audio_device *adev = calloc(1, sizeof(*adev));
    if (!adev) {
        return -ENOMEM;
    }

    pthread_mutex_init(&adev->lock, NULL);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_devices = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    adev->voice_volume = 1.0f;
    adev->next_patch_handle = 1;

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->device.common.module = (struct hw_module_t *)module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = NULL;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;
    adev->device.set_master_mute = NULL;
    adev->device.get_master_mute = NULL;
    adev->device.create_audio_patch = adev_create_audio_patch;
    adev->device.release_audio_patch = adev_release_audio_patch;
    adev->device.get_audio_port = NULL;
    adev->device.set_audio_port_config = adev_set_audio_port_config;

    ctl_set_int("Speaker Volume", 100);
    ctl_set_int("Headphone Volume", 15);
    ctl_set_int("Earpiece Volume", 14);
    ctl_set_int("Mic Volume", 2);
    apply_output_route_locked(adev, AUDIO_DEVICE_OUT_SPEAKER);
    apply_input_route_locked(adev, AUDIO_DEVICE_IN_BUILTIN_MIC);

    *device = &adev->device.common;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Marie primary audio HAL opened");
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM __attribute__((visibility("default"))) = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Marie Kirin 710 Primary Audio HAL",
        .author = "apkreader",
        .methods = &hal_module_methods,
    },
};
