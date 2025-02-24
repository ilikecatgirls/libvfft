# libvfft

A FFTW3 Audio library for Visualization use

#
Rather barebones at the moment, not clean at all.
Needs many improvements, see Goals further below.

## Simple example usage
### print when activity in between 50-500Hz
```c
#include "listener.h"
#include <unistd.h>
#include <stdio.h>

int main(void){
        // first, create listener
        int buf_size = 1;
        int n = 8192; // more complex numbers, essentially more processing for audio, and more memory.
        int sample_rate = 44100;
        double decay_rate = 0.992; // choose what you prefer, more => slower but smoother drop (in returned level), less => faster but more rigid drop (in returned level), 0 for no effect.
        int frequencies[] = {50, 500}; // frequency range to listen in between
        double sensitivity = 1.0; // bigger => (more) artificially multiplied returned level to essentially boost the output in any case. can be 0. 
        int num_ranges = sizeof(frequencies) / sizeof(frequencies[0]); // size of ranges in frequencies
        
        Listener *bassListener = listener_create(buf_size, n, sample_rate, decay_rate, sensitivity, num_ranges, frequencies, "Sink #57");

        printf("Listener initialized: buf_size=%d, sample_rate=%d, n=%d, decay_rate=%f, sensitivity=%f\n",
            bassListener->buf_size, bassListener->sample_rate, bassListener->n, bassListener->decay_rate,   bassListener->sensitivity);
 
        if (!bassListener)
            return 1;
        if (start_listening(bassListener) != 0)
            return 1;
        printf("Ready!");
        for (int i = 0; i < 10000; i += 100)
        {
            FreqInfo bass_info = get_freq_info(bassListener, 0);
            printf("%f\n", bass_info.freq_level);
            usleep(100000);
        }
    }
```
and compile it like so
```bash
gcc -o example example.c listener.c -lm -lfftw3 -lpulse
```

# Goals
The code as it is at the moment, is in a real bad state. It needs great refinement, optimization, and to be safer. I am hoping to turn this into a real library, and the current description is a little misleading. Current goals include:
 
[ ] Different compilation (currently you have to copy the file to the working directory or add it to the include PATH, and compile it like so `.. -o example example.c listener.c ..`).

[ ] More, better examples (example.c is different from the code provided above, and uses ncurses. The code in that file results in the program hanging upon receiving SIGINT/CTRL+C).

[ ] Lower latency if possible, though, the buffer is customizable as it is at the moment.

[ ] More options, more choices other than  just PulseAudio.

[ ] Perhaps something else in the future.
# Contributing

This started initially as a concept I wanted to commit to making a real thing solely because I was bored. It took many hours and procrastination to get it working, especially threading, but I finally got it to a fine, working state and decided to release it to the public in hopes of learning of what can be further done to improve it. Any contribution is greatly appreciated.

### little background
 As of the commit date involving this message here, I am still learning C and I am not good with optimization and safe code in general, so the code will look horrible.
