#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <assert.h>
#include <string.h>
#include <fftw3.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>
#include <bits/pthreadtypes.h>
#include <stdlib.h>
#include <string.h>

int running = 1;

typedef struct
{
    int freq;
    double freq_level;
    int freq_progress;
    double prev_level;
} FrequencyRange;

typedef struct
{
    double freq_level;
    int freq_progress;
} FreqInfo;

typedef struct
{
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
    pa_stream *stream;
    fftw_complex *in, *out;
    fftw_plan p;
    int buf_size;
    int n;
    int sample_rate;
    double decay_rate;
    double sensitivity;
    int num_ranges;
    FrequencyRange *ranges;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char *sink_name;
} Listener;

pthread_t pulseAudioThread;

// juice
Listener *listener_create(int buf_size, int n, int sample_rate, double decay_rate, double sensitivity, int num_ranges, int *frequencies, const char *sink_name)
{
    if (num_ranges < 2)
    {
        fprintf(stderr, "Error: At least two frequency ranges must be specified\n");
        return NULL;
    }

    Listener *listener = malloc(sizeof(Listener));
    if (!listener)
    {
        fprintf(stderr, "Error: Unable to allocate memory for listener\n");
        return NULL;
    }
    pthread_mutex_init(&listener->mutex, NULL);
    pthread_cond_init(&listener->cond, NULL);
    
    printf("Creating Listener: buf_size=%d, sample_rate=%d, n=%d\n",
        buf_size, sample_rate, n); 

    listener->buf_size = buf_size;       // specified buffer size for defined listener
    listener->n = n;                     // samples for processing for defined listener
    listener->sample_rate = sample_rate; // sample rate for defined listener
    listener->decay_rate = decay_rate;   // decay rate for defined listener
    listener->sensitivity = sensitivity; // defined sensitivity (none: 1.0)
    listener->num_ranges = num_ranges;   // amount of ranges (10hz-100hz => 2 ranges listening in between)
    listener->ranges = malloc(num_ranges * sizeof(FrequencyRange));
    if (!listener->ranges)
    {
        fprintf(stderr, "Error: Unable to allocate memory for frequency ranges\n");
        free(listener);
        return NULL;
    }

    for (int i = 0; i < num_ranges; i++)
    {
        listener->ranges[i].freq = frequencies[i];
        listener->ranges[i].freq_level = 0.0;
        listener->ranges[i].freq_progress = 0;
        listener->ranges[i].prev_level = 0.0;
    }

    listener->in = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * n);
    listener->out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * n);
    if (!listener->in || !listener->out)
    {
        fprintf(stderr, "Error: Unable to allocate memory for FFTW arrays\n");
        if (listener->in)
            fftw_free(listener->in);
        if (listener->out)
            fftw_free(listener->out);
        free(listener->ranges);
        free(listener);
        return NULL;
    }

    listener->p = fftw_plan_dft_1d(n, listener->in, listener->out, FFTW_FORWARD, FFTW_ESTIMATE);
    listener->sink_name = strdup(sink_name);
    return listener;
}

int listener_destroy(Listener *listener)
{
    if (!listener)
        return -1;

    pthread_mutex_lock(&listener->mutex);
    running = 0;
    pthread_cond_signal(&listener->cond);
    pthread_mutex_unlock(&listener->mutex);

    if (listener->mainloop)
    {
        pa_mainloop_quit(listener->mainloop, 0);
        pthread_join(pulseAudioThread, NULL); // wait for finish
    }

    // cleanup fftw, pulse and listener ranges
    if (listener->p)
        fftw_destroy_plan(listener->p);
    if (listener->in)
        fftw_free(listener->in);
    if (listener->out)
        fftw_free(listener->out);

    if (listener->ranges)
        free(listener->ranges);
    if (listener->context)
        pa_context_unref(listener->context);
    if (listener->mainloop)
        pa_mainloop_free(listener->mainloop);

    pthread_mutex_destroy(&listener->mutex);
    pthread_cond_destroy(&listener->cond);
    free(listener);

    printf("Detached and freed. Quitting\n");

    return 0;
}

// frequency info
FreqInfo get_freq_info(Listener *listener, int range_index)
{
    FreqInfo info;
    pthread_mutex_lock(&listener->mutex); // lock before access
    if (range_index >= 0 && range_index < listener->num_ranges)
    {
        info.freq_level = listener->ranges[range_index].freq_level;
        info.freq_progress = listener->ranges[range_index].freq_progress;
    }
    else
    {
        fprintf(stderr, "Error: Invalid range index\n");
        info.freq_level = 0.0;
        info.freq_progress = 0;
    }
    pthread_mutex_unlock(&listener->mutex); // unlock after access
    return info;
}

// callback stream data
static void read_callback(pa_stream *s, size_t length, void *userdata)
{
    Listener *listener = (Listener *)userdata;
    const void *data;

    pthread_mutex_lock(&listener->mutex); // lock before access
    if (!running)
    {
        pa_stream_drop(s);
        pthread_mutex_unlock(&listener->mutex); // unlock after dropping
        return;
    }

    if (pa_stream_peek(s, &data, &length) < 0)
    {
        fprintf(stderr, "Failed to read data from stream\n");
        pthread_mutex_unlock(&listener->mutex); // unlock on error
        return;
    }

    if (!data)
    {
        if (length)
            pa_stream_peek(s, &data, &length), pa_stream_drop(s);
        pthread_mutex_unlock(&listener->mutex); // unlock when no data
        return;
    }

    assert(length > 0);
    assert(length % sizeof(float) == 0);

    if (length > listener->n * sizeof(fftw_complex))
    {
        fprintf(stderr, "Error: length is greater than buffer size\n");
        pthread_mutex_unlock(&listener->mutex); // unlock on error
        return;
    }

    memmove(listener->in, data, length);
    fftw_execute(listener->p);

    for (int j = 0; j < listener->num_ranges - 1; ++j)
    {
        double freq_level = 0.0;
        int freq_lower_index = listener->ranges[j].freq * listener->n / listener->sample_rate;
        int freq_upper_index = listener->ranges[j + 1].freq * listener->n / listener->sample_rate;

        for (int i = freq_lower_index; i < freq_upper_index; ++i)
        {
            double abs_val = listener->out[i][0] * listener->out[i][0] + listener->out[i][1] * listener->out[i][1];
            freq_level += abs_val;
        }

        freq_level = sqrt(freq_level);
        if (freq_level < listener->ranges[j].prev_level)
        {
            freq_level = listener->decay_rate > 0.0 ? listener->ranges[j].prev_level * listener->decay_rate : listener->ranges[j].prev_level;
        }
        listener->ranges[j].prev_level = freq_level;

        freq_level = fmin(freq_level * listener->sensitivity, 4.0); // may make customizable
        listener->ranges[j].freq_level = freq_level;
        listener->ranges[j].freq_progress = (int)(freq_level * 100);
    }

    pthread_cond_signal(&listener->cond);
    pthread_mutex_unlock(&listener->mutex);
    pa_stream_drop(s);
}

// callback stream state changes
static void stream_state_callback(pa_stream *s, void *userdata)
{
    Listener *listener = (Listener *)userdata;
    switch (pa_stream_get_state(s))
    {
    case PA_STREAM_READY:
        pa_stream_set_read_callback(s, read_callback, listener);
        break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        pa_mainloop_quit(listener->mainloop, 0);
        break;
    default:
        break;
    }
}

// callback context state changes
static void context_state_callback(pa_context *c, void *userdata)
{
    Listener *listener = (Listener *)userdata;
    
    char *devicename = NULL;
    switch (pa_context_get_state(c))
    {
    case PA_CONTEXT_READY:
    {
        pa_sample_spec ss;
        ss.format = PA_SAMPLE_FLOAT32;
        ss.rate = listener->sample_rate;
        ss.channels = 2;
        pa_buffer_attr buffer_attr;
        buffer_attr.maxlength = (uint32_t)-1;
        buffer_attr.tlength = pa_usec_to_bytes(listener->buf_size * PA_USEC_PER_MSEC, &ss);
        buffer_attr.prebuf = (uint32_t)-1;
        buffer_attr.minreq = (uint32_t)-1;
        buffer_attr.fragsize = pa_usec_to_bytes(listener->buf_size * PA_USEC_PER_MSEC, &ss);
        const char *sink_name = listener->sink_name;
        if (sink_name)
        {
            size_t len = strlen(sink_name) + strlen(".monitor") + 1;
            devicename = malloc(len);
            if (!devicename)
            {
                fprintf(stderr, "Error: Unable to allocate memory for sink\n");
                pa_mainloop_quit(listener->mainloop, 0);
                if (devicename)
                    free(devicename);
                return;
            }
            snprintf(devicename, len, "%s.monitor", sink_name);
        }

        listener->stream = pa_stream_new(c, "Peak detect", &ss, NULL);
        if (!listener->stream)
        {
            fprintf(stderr, "Error: Unable to create PulseAudio stream\n");
            if (devicename)
                free(devicename);
            pa_mainloop_quit(listener->mainloop, 0);
            return;
        }

        pa_stream_set_state_callback(listener->stream, stream_state_callback, listener);
        pa_stream_connect_record(listener->stream, devicename, &buffer_attr, PA_STREAM_PEAK_DETECT | PA_STREAM_ADJUST_LATENCY);

        if (devicename)
            free(devicename);
        break;
    }
    case PA_CONTEXT_FAILED:
        pa_mainloop_quit(listener->mainloop, 0);
        if (devicename)
            free(devicename);
        break;
    case PA_CONTEXT_TERMINATED:
        pa_mainloop_quit(listener->mainloop, 0);
        if (devicename)
            free(devicename);
        break;
    default:
        break;
    }
}

// [threaded] pulse mainloop
void *pulse_audio_thread(void *listenerArg)
{
    Listener *listener = (Listener *)listenerArg;
    pa_mainloop_run(listener->mainloop, NULL);
    return NULL;
}



// start listening to the pulse stream
int start_listening(Listener *listener)
{
    listener->mainloop = pa_mainloop_new();
    if (!listener->mainloop)
    {
        fprintf(stderr, "Error: Unable to create PulseAudio mainloop\n");
        return 1;
    }

    listener->mainloop_api = pa_mainloop_get_api(listener->mainloop);
    listener->context = pa_context_new(listener->mainloop_api, "Peak detect");
    if (!listener->context)
    {
        fprintf(stderr, "Error: Unable to create PulseAudio context\n");
        pa_mainloop_free(listener->mainloop);
        return 1;
    }

    pa_context_set_state_callback(listener->context, context_state_callback, listener);

    pa_context_set_state_callback(listener->context, context_state_callback, listener);
    if (pa_context_connect(listener->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
        fprintf(stderr, "Error: Unable to connect PulseAudio context\n");
        pa_context_unref(listener->context);
        pa_mainloop_free(listener->mainloop);
        return 1;
    }
    pthread_create(&pulseAudioThread, NULL, pulse_audio_thread, listener);
    return 0;
}