#include "common.h"

#include "apu.h"
#include "audio.h"
#include "controller.h"
#include "cpu.h"
#include "input.h"
#include "ppu.h"
#include "mapper.h"
#include "save_states.h"

// Number of seconds of rewind to support. The rewind buffer is a ring buffer
// where a new state will overwrite the oldest state when the buffer is full.
unsigned const   n_rewind_seconds = 30;

static bool      has_save;
static size_t    state_size;
static uint8_t  *state;

unsigned const   n_rewind_frames = 60*n_rewind_seconds;
static uint8_t  *rewind_buf;
static unsigned  rewind_buf_i;
// frame_len[n] is the length of frame n in CPU ticks. Used to cleanly reverse
// audio.
static unsigned *frame_len;
static unsigned  n_recorded_frames;
bool             is_backwards_frame;

template<bool calculating_size, bool is_save>
static size_t transfer_system_state(uint8_t *buf) {
    uint8_t *tmp = buf;

    transfer_apu_state<calculating_size, is_save>(buf);
    transfer_cpu_state<calculating_size, is_save>(buf);
    transfer_ppu_state<calculating_size, is_save>(buf);
    transfer_controller_state<calculating_size, is_save>(buf);
    transfer_input_state<calculating_size, is_save>(buf);

    if (calculating_size)
        mapper_state_size(buf);
    else {
        if (is_save)
            mapper_save_state(buf);
        else
            mapper_load_state(buf);
    }

    // Return size of state in bytes
    return buf - tmp;
}

//
// Save states
//

void save_state() {
    transfer_system_state<false, true>(state);
    has_save = true;
}

void load_state() {
    if (has_save) {
        // Clear rewind
        n_recorded_frames = 0;

        transfer_system_state<false, false>(state);
    }
}

//
// Rewinding
//

// Saves the length of the most recently finished frame in CPU ticks. Used when
// reversing audio for rewind.
void save_audio_frame_length(unsigned len) {
    frame_len[rewind_buf_i] = len;
}

// Saves the current state to the rewind buffer. New states overwrite old if
// the buffer becomes full.
static void push_state() {
    if (n_recorded_frames < n_rewind_frames)
        ++n_recorded_frames;

    rewind_buf_i = (rewind_buf_i + 1) % n_rewind_frames;
    transfer_system_state<false, true>(rewind_buf + state_size*rewind_buf_i);
}

// Removes the most recently pushed state from the rewind buffer
static void pop_state() {
    assert(n_recorded_frames > 0);
    rewind_buf_i = (rewind_buf_i == 0) ? n_rewind_frames - 1 : rewind_buf_i - 1;
    --n_recorded_frames;
}

// Loads the most recently pushed state from the rewind buffer
static void load_top_state() {
    transfer_system_state<false, false>(rewind_buf + state_size*rewind_buf_i);
    audio_frame_len = frame_len[rewind_buf_i];
}

static void handle_forwards_frame() {
    if (is_backwards_frame) {
        // We just stopped rewinding. To get a clean transition in the sound,
        // load the same frame we just rewound and run it forwards.
        load_top_state();
        is_backwards_frame = false;
    }
    else
        // Save the state to the rewind buffer. Rewind is always enabled for
        // now.
        push_state();
}

static void handle_backwards_frame() {
    assert(n_recorded_frames > 0);
    // Do not pop the top state if we just started rewinding (i.e., if
    // !is_backwards_frame). We want to run it again backwards first to
    // get a clean transition in the sound.
    if (is_backwards_frame && n_recorded_frames > 1)
        pop_state();
    load_top_state();

    is_backwards_frame = true;
}

void handle_rewind(bool do_rewind) {
    if (do_rewind && n_recorded_frames > 0)
        handle_backwards_frame();
    else
        handle_forwards_frame();
}

void init_save_states_for_rom() {
    state_size = transfer_system_state<true, false>(0);
    size_t const rewind_buf_size = state_size*n_rewind_frames;
#ifndef RUN_TESTS
    printf("Save state size: %zu bytes\nRewind buffer size: %zu bytes\n",
           state_size, rewind_buf_size);
#endif
    fail_if(!(state = new (std::nothrow) uint8_t[state_size]),
      "failed to allocate %zu-byte buffer for save state", state_size);
    fail_if(!(rewind_buf = new (std::nothrow) uint8_t[rewind_buf_size]),
      "failed to allocate %zu-byte rewind buffer", rewind_buf_size);
    fail_if(!(frame_len = new (std::nothrow) unsigned[n_rewind_frames]),
      "failed to allocate %zu-byte buffer for frame lengths",
      sizeof(unsigned)*n_rewind_frames);

    rewind_buf_i = 0;
}

void deinit_save_states_for_rom() {
    free_array_set_null(state);
    free_array_set_null(rewind_buf);
    free_array_set_null(frame_len);
    n_recorded_frames = 0;
    has_save = false;
}
