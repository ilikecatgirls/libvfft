#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include "listener.h"

#define BAR_LENGTH 32

static Listener *bassListener;
static Listener *trebleListener;

pthread_t thread_id1;
pthread_t thread_id2;

static int running = 1;

void *start_listening_thread(void *arg)
{
    Listener *listener = (Listener *)arg;
    if (start_listening(listener) != 0)
    {
        fprintf(stderr, "Error: Failed to start listening\n");
        return NULL;
    }
    return NULL;
}

void signal_handler(/*int sig*/)
{
    printf("Received exit");
    running = 0;
    usleep(100);
    if (pthread_join(thread_id1, NULL) != 0)
    {
        fprintf(stderr, "Error: Failed to join thread_id1\n");
    }
    if (pthread_join(thread_id2, NULL) != 0)
    {
        fprintf(stderr, "Error: Failed to join thread_id2\n");
    }
    if (listener_destroy(bassListener) != 0)
    {
        fprintf(stderr, "Error: Failed to destroy bassListener\n");
    }
    if (listener_destroy(trebleListener) != 0)
    {
        fprintf(stderr, "Error: Failed to destroy trebleListener\n");
    }
    exit(0);
}

void bar()
{
    FreqInfo bass_info = get_freq_info(bassListener, 0);
    FreqInfo treble_info = get_freq_info(trebleListener, 0);

    int bass_level = (int)(bass_info.freq_level * BAR_LENGTH);
    int treble_level = (int)(treble_info.freq_level * BAR_LENGTH);

    bass_level = bass_level > BAR_LENGTH ? BAR_LENGTH : bass_level;
    treble_level = treble_level > BAR_LENGTH ? BAR_LENGTH : treble_level;

    mvprintw(0, 0, "[Bass]   - {");
    mvprintw(1, 0, "[Other+] - {");

    mvprintw(0, 12, "--------------------------------");
    mvprintw(0, 12 + BAR_LENGTH, "}");

    mvprintw(1, 12, "--------------------------------");
    mvprintw(1, 12 + BAR_LENGTH, "}");

    for (int i = 0; i < bass_level; i++)
    {
        mvprintw(0, 12 + i, "#");
    }

    for (int i = 0; i < treble_level; i++)
    {
        mvprintw(1, 12 + i, "#");
    }


    refresh();
}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);

    int buf_size = 1;
    int n = 8192;
    int sample_rate = 44100;
    double decay_rate = 0.992;
    double decay_rate2 = 0.992;
    int frequencies[] = {0, 500};
    int frequencies2[] = {1000, 15000};
    double sensitivity1 = 2.0;
    double sensitivity2 = 2.0;
    int num_ranges = sizeof(frequencies) / sizeof(frequencies[0]);
    int num_ranges2 = sizeof(frequencies2) / sizeof(frequencies2[0]);

    bassListener = listener_create(buf_size, n, sample_rate, decay_rate, sensitivity1, num_ranges, frequencies, "Sink #57");
    trebleListener = listener_create(buf_size, n, sample_rate, decay_rate2, sensitivity2, num_ranges2, frequencies2, "Sink #57");

    if (!bassListener || !trebleListener)
    {
        fprintf(stderr, "Failed to create listeners\n");
        return 1;
    }

    pthread_create(&thread_id1, NULL, start_listening_thread, bassListener);
    pthread_create(&thread_id2, NULL, start_listening_thread, trebleListener);

    initscr();
    noecho();
    curs_set(0);

    while (running)
    {
        bar();
        usleep(1000);
    }
    printf("Quitting...\n");
    endwin();
    return 0;
}