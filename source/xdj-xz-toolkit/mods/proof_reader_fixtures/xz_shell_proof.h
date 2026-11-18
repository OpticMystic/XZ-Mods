/* Source snapshot: four-deck-shell 6dc3230. */
#include <stdint.h>
struct xz_shell_proof {
 uint32_t version, size, sequence;
 uint32_t enabled, load_enabled, owns, handed_off, caution_yield, caution, screen, capabilities;
 /* The last whole second. */
 uint32_t second_frames, second_render_calls, second_stock_flips, second_skipped, second_busy, second_pump, second_ticks;
 uint32_t second_render_avg_us, second_render_max_us, second_refresh_avg_us, second_refresh_max_us;
 /* Since start. */
 uint32_t frames, render_calls, stock_flips, keys_swallowed, keys_passed, key_hand_offs, touches, encoder_events, actions;
 uint32_t hand_offs, resumes, back_hold_resumes, commands_run, commands_refused, commands_dropped, command_latency_max_ms;
 uint32_t last_command, last_command_result;
 uint32_t guards;                     /* bit per verified contract, enum guard */
 uint32_t model_guard_failures[4];    /* R3, R4, R5, R6 */
 uint32_t wave_loads, wave_failures, grid_loads, library_tracks[2], library_errors, list_builds, list_errors, list_build_ms;
 /* Added for the 60 Hz pipeline (the struct reports its own size). Last whole second: */
 uint32_t second_refreshes, second_gap_max_us, second_late, second_tick_busy, second_clock_reads, second_moving;
 /* Since start: */
 uint32_t model_holds, fx_guard_failures, caution_filtered, ui_requests, menu_passes;
};
