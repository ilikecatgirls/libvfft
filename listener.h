#ifndef LISTENER_H
#define LISTENER_H

#include <pulse/pulseaudio.h>
#include <fftw3.h>

typedef struct {
    int freq;
    double freq_level;
    int freq_progress;
    double prev_level;
} FrequencyRange;

typedef struct {
    double freq_level;
    int freq_progress;
} FreqInfo;

typedef struct {
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
    pa_stream *stream;
    fftw_complex *in, *out;
    fftw_plan p;
    double prev_low_freq_level;
    int buf_size;
    int n;
    int sample_rate;
    double sensitivity;
    double decay_rate;
    int num_ranges;
    FrequencyRange *ranges;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char *sink_name;
} Listener;

/* listener.c Functions */
extern Listener *listener_create(int buf_size, int n, int sample_rate, double decay_rate, double sensitivity, int num_ranges, int *frequencies, char *sink_name);
extern FreqInfo get_freq_info(Listener *listener, int range_index);
extern int listener_destroy(Listener *listener);
extern int start_listening(Listener *listener);
extern void listener_get_freq_info(Listener *listener, int range_index, double *freq_level, int *freq_progress);
#endif /* LISTENER_H */
