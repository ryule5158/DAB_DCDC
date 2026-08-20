#include <assert.h>
#include <stdbool.h>

#include "dab_mppt_phase_search.h"

static void step(dab_mppt_phase_search_t * s,
                 dab_mppt_phase_input_t * in,
                 uint32_t delta_ms,
                 float pin_w,
                 float efficiency_percent)
{
    in->now_ms += delta_ms;
    in->pin_w = pin_w;
    in->pout_w = pin_w * efficiency_percent / 100.0F;
    in->applied_primary_inner_ns = s->output.primary_inner_ns;
    in->applied_secondary_inner_ns = s->output.secondary_inner_ns;
    dab_mppt_phase_update(s, in);
}

static void run_mode_window(dab_mppt_phase_search_t * s,
                            dab_mppt_phase_input_t * in,
                            float pin_w,
                            float efficiency_percent,
                            uint32_t milliseconds)
{
    for (uint32_t elapsed = 0U; elapsed < milliseconds; elapsed += 100U)
    {
        step(s, in, 100U, pin_w, efficiency_percent);
    }
}

int main(void)
{
    dab_mppt_phase_search_t s;
    dab_mppt_phase_initialize(&s);
    assert(dab_mppt_phase_abs(s.config.vin_min_v - 17.5F) < 0.01F);
    assert(dab_mppt_phase_abs(s.config.vin_recovery_margin_v - 0.50F) < 0.01F);
    assert(dab_mppt_phase_abs(DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS -
                              480.0F) < 0.01F);
    assert(dab_mppt_phase_abs(dab_mppt_phase_generic_high_inner_ns(
                                  19.1F, 36.0F) - 0.0F) < 0.01F);
    assert(dab_mppt_phase_abs(dab_mppt_phase_generic_high_inner_ns(
                                  22.9F, 36.0F) -
                              DAB_MPPT_PHASE_EPS_BOOTSTRAP_NS) < 0.01F);
    dab_mppt_phase_input_t in =
    {
        .enabled = true,
        .valid = true,
        .now_ms = 0U,
        .vin_v = 22.0F,
        .vout_v = 33.0F,
        .pin_w = 20.0F,
        .pout_w = 18.0F,
        .current_reference_a = 1.0F,
        .current_active_a = 1.0F,
        .measured_output_current_a = 1.0F,
        .outer_saturated = false,
        .applied_mode = DAB_MPPT_PHASE_MODE_SPS,
        .applied_outer_phase_ns = 80.0F,
        .applied_primary_inner_ns = 0.0F,
        .applied_secondary_inner_ns = 0.0F
    };

    dab_mppt_phase_update(&s, &in);
    assert(DAB_MPPT_PHASE_MODE_SPS == s.output.mode);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == s.output.stage);
    assert(0.0F == s.output.primary_inner_ns);
    assert(0.0F == s.output.secondary_inner_ns);
    assert(DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS == s.output.phase_cap_ns);

    /* PRECHARGE -> CC must inherit the physically applied TPS waveform. If
     * it later saturates below the P&O current request, only one bounded
     * inner-phase step is released per second. */
    dab_mppt_phase_search_t inherited;
    dab_mppt_phase_initialize(&inherited);
    in.now_ms = 0U;
    in.applied_mode = DAB_MPPT_PHASE_MODE_TPS;
    in.applied_primary_inner_ns = 832.0F;
    in.applied_secondary_inner_ns = 180.0F;
    dab_mppt_phase_update(&inherited, &in);
    assert(DAB_MPPT_PHASE_MODE_TPS == inherited.output.mode);
    assert(832.0F == inherited.output.primary_inner_ns);
    assert(180.0F == inherited.output.secondary_inner_ns);
    in.current_reference_a = 1.5F;
    in.current_active_a = 1.5F;
    in.measured_output_current_a = 1.0F;
    in.outer_saturated = true;
    in.now_ms = 100U;
    dab_mppt_phase_update(&inherited, &in);
    in.now_ms = 1100U;
    dab_mppt_phase_update(&inherited, &in);
    assert(800.0F == inherited.output.primary_inner_ns);
    assert(180.0F == inherited.output.secondary_inner_ns);
    in.outer_saturated = false;
    in.measured_output_current_a = 1.5F;
    in.current_reference_a = 1.0F;
    in.current_active_a = 1.0F;
    in.applied_mode = DAB_MPPT_PHASE_MODE_SPS;
    in.applied_primary_inner_ns = 0.0F;
    in.applied_secondary_inner_ns = 0.0F;

    /* A nominal 20 V source can read about 19.2 V on the board under load.
     * It must remain eligible for local discovery instead of being forced
     * into the low-power recovery waveform. */
    dab_mppt_phase_search_t nominal_20;
    dab_mppt_phase_initialize(&nominal_20);
    in.vin_v = 19.2F;
    dab_mppt_phase_update(&nominal_20, &in);
    step(&nominal_20, &in, 100U, 20.0F, 90.0F);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == nominal_20.output.stage);
    assert(DAB_MPPT_PHASE_MODE_SPS == nominal_20.output.mode);
    assert(nominal_20.mode_best[0].valid);
    in.vin_v = 22.0F;

    /* A target reversal and actuator settling occur on different CPU0
     * updates. The reversal must remain latched until current_active catches
     * the CPU1 request, otherwise discovery remains in WAIT forever. */
    dab_mppt_phase_search_t reversal_latch;
    dab_mppt_phase_initialize(&reversal_latch);
    in.now_ms = 0U;
    in.vin_v = 21.0F;
    in.current_reference_a = 2.0F;
    in.current_active_a = 2.0F;
    dab_mppt_phase_update(&reversal_latch, &in);
    in.now_ms = 100U;
    in.current_reference_a = 1.8F;
    in.current_active_a = 2.2F;
    dab_mppt_phase_update(&reversal_latch, &in);
    assert(reversal_latch.source_reversal_seen);
    assert(!reversal_latch.source_peak_seen);
    in.now_ms = 2500U;
    in.vin_v = 20.9F;
    in.pin_w = 223.0F;
    in.pout_w = 200.0F;
    in.current_active_a = 1.8F;
    dab_mppt_phase_update(&reversal_latch, &in);
    assert(reversal_latch.source_peak_seen);
    assert(dab_mppt_phase_abs(reversal_latch.search_floor_vin_v -
                              (reversal_latch.source_best_vin_v -
                               DAB_MPPT_PHASE_LOCAL_FLOOR_MARGIN_V)) <
           0.01F);
    assert(DAB_MPPT_PHASE_MODE_SPS == reversal_latch.output.mode);
    assert(!reversal_latch.sequenced_recovery_inner);
    assert(0.0F == reversal_latch.output.primary_inner_ns);
    assert(0.0F == reversal_latch.output.secondary_inner_ns);

    /* A healthy P&O reversal is already the live MPP. It must not unload to
     * the recovery waveform before the current-regulated mode comparison. */
    in.now_ms = 4500U;
    dab_mppt_phase_update(&reversal_latch, &in);
    assert(DAB_MPPT_PHASE_SPS_OBSERVE == reversal_latch.output.stage);

    /* r136 recovery sequencing remains mandatory for a genuine source-edge
     * collapse: establish the best-Pin voltage, then cross the live floor. */
    dab_mppt_phase_search_t collapse_recovery;
    dab_mppt_phase_initialize(&collapse_recovery);
    in.now_ms = 0U;
    in.vin_v = 21.0F;
    in.pin_w = 20.0F;
    in.pout_w = 18.0F;
    in.current_reference_a = 2.0F;
    in.current_active_a = 2.0F;
    in.measured_output_current_a = 2.0F;
    dab_mppt_phase_update(&collapse_recovery, &in);
    in.now_ms = 100U;
    in.vin_v = 20.9F;
    in.pin_w = 223.0F;
    in.pout_w = 200.0F;
    dab_mppt_phase_update(&collapse_recovery, &in);
    in.now_ms = 200U;
    in.vin_v = 17.6F;
    in.pin_w = 200.0F;
    in.pout_w = 178.0F;
    dab_mppt_phase_update(&collapse_recovery, &in);
    assert(collapse_recovery.source_peak_seen);
    assert(collapse_recovery.sequenced_recovery_inner);
    assert(DAB_MPPT_PHASE_MODE_TPS == collapse_recovery.output.mode);
    assert(DAB_MPPT_PHASE_RECOVERY_HIGH_NS ==
           collapse_recovery.output.primary_inner_ns);
    assert(0.0F == collapse_recovery.output.secondary_inner_ns);

    /* Never command the 832/180 ns recovery legs together from zero. Even
     * with Vin recovered, WAIT persists until primary arrives, then releases
     * secondary, then requires both applied values before SPS observation. */
    in.now_ms = 3000U;
    in.vin_v = 22.0F;
    in.pin_w = 20.0F;
    in.pout_w = 18.0F;
    in.applied_primary_inner_ns = 400.0F;
    in.applied_secondary_inner_ns = 0.0F;
    dab_mppt_phase_update(&collapse_recovery, &in);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK ==
           collapse_recovery.output.stage);
    assert(0.0F == collapse_recovery.output.secondary_inner_ns);
    in.now_ms = 4000U;
    in.applied_primary_inner_ns = DAB_MPPT_PHASE_RECOVERY_HIGH_NS;
    dab_mppt_phase_update(&collapse_recovery, &in);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK ==
           collapse_recovery.output.stage);
    assert(DAB_MPPT_PHASE_RECOVERY_LOW_NS ==
           collapse_recovery.output.secondary_inner_ns);
    in.now_ms = 5000U;
    in.applied_secondary_inner_ns = DAB_MPPT_PHASE_RECOVERY_LOW_NS;
    dab_mppt_phase_update(&collapse_recovery, &in);
    assert(DAB_MPPT_PHASE_SPS_OBSERVE ==
           collapse_recovery.output.stage);
    in.vin_v = 22.0F;
    in.current_reference_a = 1.0F;
    in.current_active_a = 1.0F;
    in.applied_primary_inner_ns = 0.0F;
    in.applied_secondary_inner_ns = 0.0F;

    /* The source probe can encounter the true peak while an inner phase is
     * still slewing out of the previous waveform.  Preserve the real applied
     * phase as an EPS/TPS candidate before entering recovery. */
    dab_mppt_phase_search_t source_keep;
    dab_mppt_phase_initialize(&source_keep);
    dab_mppt_phase_update(&source_keep, &in);
    in.now_ms = 100U;
    in.vin_v = 17.6F;
    in.pin_w = 113.0F;
    in.pout_w = 102.0F;
    in.applied_primary_inner_ns = 388.0F;
    in.applied_secondary_inner_ns = 0.0F;
    in.applied_outer_phase_ns = 300.0F;
    dab_mppt_phase_update(&source_keep, &in);
    assert(source_keep.mode_best[1].valid);
    assert(source_keep.mode_best[1].pin_w == 113.0F);
    assert(source_keep.mode_best[1].primary_inner_ns == 388.0F);
    assert(source_keep.mode_best[1].outer_phase_ns == 300.0F);
    assert(DAB_MPPT_PHASE_MODE_TPS == source_keep.output.mode);
    assert(source_keep.source_peak_seen);

    /* r97 entered SPS observation after an early startup reversal, then the
     * still-slewing applied inner phase reached 5.47 A / 113.55 W.  Candidate
     * capture must remain active for that complete trajectory. */
    dab_mppt_phase_search_t late_peak;
    dab_mppt_phase_initialize(&late_peak);
    late_peak.output.active = true;
    late_peak.output.stage = DAB_MPPT_PHASE_SPS_OBSERVE;
    late_peak.output.mode = DAB_MPPT_PHASE_MODE_SPS;
    late_peak.stage_started_ms = in.now_ms;
    late_peak.mode_best[1].valid = true;
    late_peak.mode_best[1].pin_w = 51.0F;
    late_peak.mode_best[1].efficiency_percent = 87.0F;
    in.now_ms += 100U;
    in.vin_v = 20.8F;
    in.pin_w = 113.55F;
    in.pout_w = 103.0F;
    in.applied_primary_inner_ns = 400.0F;
    in.applied_secondary_inner_ns = 0.0F;
    in.applied_outer_phase_ns = 320.0F;
    dab_mppt_phase_update(&late_peak, &in);
    assert(late_peak.mode_best[1].valid);
    assert(late_peak.mode_best[1].pin_w == 113.55F);
    assert(late_peak.mode_best[1].primary_inner_ns == 400.0F);
    assert(late_peak.mode_best[1].outer_phase_ns == 320.0F);

    /* The live nominal-22 V knee is a tighter mode-trial floor than the
     * global nominal-20 V allowance.  Reject the unsafe candidate before it
     * can reproduce r133's 18 V excursion. */
    late_peak.search_floor_vin_v = 20.2F;
    in.now_ms += 100U;
    in.vin_v = 20.1F;
    dab_mppt_phase_update(&late_peak, &in);
    assert(late_peak.sps_infeasible);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == late_peak.output.stage);

    /* A later EPS trial may be skipped after crossing below the local search
     * floor, but it must not erase an earlier feasible EPS waveform. */
    late_peak.output.stage = DAB_MPPT_PHASE_EPS_SEARCH;
    late_peak.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    in.now_ms += 100U;
    in.vin_v = 17.0F;
    in.pin_w = 100.0F;
    in.pout_w = 90.0F;
    dab_mppt_phase_update(&late_peak, &in);
    assert(late_peak.eps_infeasible);
    assert(late_peak.mode_best[1].valid);
    assert(late_peak.mode_best[1].pin_w == 113.55F);

    in.now_ms = 0U;
    in.vin_v = 22.0F;
    in.pin_w = 20.0F;
    in.pout_w = 18.0F;
    in.applied_primary_inner_ns = 0.0F;
    in.applied_secondary_inner_ns = 0.0F;

    /* Crossing below the product range identifies the source edge, but the
     * mode-ranking window must not begin until the source has recovered.  The
     * waiting waveform is the proven low-duty TPS baseline, not SPS zero. */
    in.vin_v = 17.0F;
    run_mode_window(&s, &in, 55.0F, 90.0F, 4000U);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == s.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == s.output.mode);
    in.vin_v = 18.1F;
    step(&s, &in, 100U, 55.0F, 90.0F);
    assert(DAB_MPPT_PHASE_SPS_OBSERVE == s.output.stage);
    assert(DAB_MPPT_PHASE_MODE_SPS == s.output.mode);
    run_mode_window(&s, &in, 55.0F, 90.0F, 1600U);
    assert(DAB_MPPT_PHASE_MODE_EPS == s.output.mode);
    assert(0.0F == s.output.primary_inner_ns);

    run_mode_window(&s, &in, 55.0F, 90.0F, 1600U);
    assert(DAB_MPPT_PHASE_EPS_SEARCH == s.output.stage);
    run_mode_window(&s, &in, 55.5F, 92.0F, 1600U);
    assert(DAB_MPPT_PHASE_MODE_TPS == s.output.mode);
    run_mode_window(&s, &in, 55.0F, 91.0F, 1600U);
    assert(DAB_MPPT_PHASE_TPS_SEARCH == s.output.stage);
    run_mode_window(&s, &in, 54.5F, 91.0F, 1600U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == s.output.stage);
    assert(s.output.selection_complete);
    assert(DAB_MPPT_PHASE_MODE_SPS == s.output.mode);
    assert(s.output.selected_efficiency_percent > 91.5F);

    /* The hardware inner phase is slew-limited.  A bucket must remember the
     * phase actually applied while the useful power was measured, not the
     * far-away target which caused the ramp. */
    dab_mppt_phase_search_t replay_metric;
    dab_mppt_phase_initialize(&replay_metric);
    replay_metric.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    replay_metric.output.primary_inner_ns = 653.0F;
    replay_metric.output.secondary_inner_ns = 0.0F;
    in.applied_primary_inner_ns = 304.0F;
    in.applied_secondary_inner_ns = 0.0F;
    in.applied_outer_phase_ns = 276.0F;
    in.pin_w = 110.0F;
    in.pout_w = 100.0F;
    dab_mppt_phase_bucket_reset(&replay_metric, in.now_ms);
    dab_mppt_phase_bucket_add(&replay_metric, &in);
    assert(dab_mppt_phase_bucket_commit(&replay_metric,
                                         DAB_MPPT_PHASE_MODE_EPS));
    assert(replay_metric.mode_best[1].primary_inner_ns == 304.0F);
    assert(replay_metric.mode_best[1].outer_phase_ns == 276.0F);

    /* r110 found SPS near 448 ns, selected it, then incorrectly held the
     * 480 ns sweep endpoint because outer phase was not part of the metric.
     * The winning metric must now carry the exact applied outer phase into
     * HOLD so the platform layer can replay it as the closed-loop cap. */
    dab_mppt_phase_search_t outer_replay;
    dab_mppt_phase_initialize(&outer_replay);
    outer_replay.mode_best[0].valid = true;
    outer_replay.mode_best[0].pin_w = 161.2F;
    outer_replay.mode_best[0].efficiency_percent = 91.12F;
    outer_replay.mode_best[0].outer_phase_ns = 448.0F;
    outer_replay.mode_best[0].primary_inner_ns = 0.0F;
    outer_replay.mode_best[0].secondary_inner_ns = 0.0F;
    in.applied_outer_phase_ns = 480.0F;
    dab_mppt_phase_choose_best(&outer_replay, &in);
    assert(DAB_MPPT_PHASE_HOLD_BEST == outer_replay.output.stage);
    assert(DAB_MPPT_PHASE_MODE_SPS == outer_replay.output.mode);
    assert(outer_replay.output.selected_outer_phase_ns == 448.0F);
    assert(outer_replay.output.phase_cap_ns == 464.0F);

    /* The replay point is the initial cap, not a permanent lock. A sustained
     * current deficit at the cap releases only 8 ns per 250 ms. */
    in.current_reference_a = 3.0F;
    in.current_active_a = 3.0F;
    in.measured_output_current_a = 2.0F;
    in.outer_saturated = true;
    in.now_ms += 100U;
    dab_mppt_phase_update(&outer_replay, &in);
    in.now_ms += DAB_MPPT_PHASE_CAP_RELEASE_MS;
    dab_mppt_phase_update(&outer_replay, &in);
    assert(outer_replay.output.phase_cap_ns == 472.0F);
    in.outer_saturated = false;
    in.measured_output_current_a = 3.0F;

    /* A 0.1 V battery-CV step must run a lightweight MPPT retune immediately,
     * rather than waiting for the cumulative SPS/EPS/TPS mode audit. Falling
     * input power/current releases one bounded 8 ns authority step while the
     * selected topology and all physical slew limits remain unchanged. */
    dab_mppt_phase_search_t cv_step;
    dab_mppt_phase_initialize(&cv_step);
    cv_step.output.active = true;
    cv_step.output.selection_complete = true;
    cv_step.output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    cv_step.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    cv_step.output.primary_inner_ns = 390.0F;
    cv_step.output.phase_cap_ns = 220.0F;
    cv_step.trajectory_anchor_valid = true;
    cv_step.trajectory_anchor_vout_v = 33.0F;
    cv_step.trajectory_anchor_pin_w = 170.0F;
    in.now_ms = 0U;
    in.vin_v = 22.9F;
    in.vout_v = 33.10F;
    in.pin_w = 166.0F;
    in.pout_w = 148.0F;
    in.current_reference_a = 4.5F;
    in.current_active_a = 4.5F;
    in.measured_output_current_a = 4.0F;
    in.outer_saturated = false;
    in.applied_mode = DAB_MPPT_PHASE_MODE_EPS;
    in.applied_primary_inner_ns = 390.0F;
    in.applied_secondary_inner_ns = 0.0F;
    dab_mppt_phase_update(&cv_step, &in);
    assert(228.0F == cv_step.output.phase_cap_ns);
    assert(DAB_MPPT_PHASE_MODE_EPS == cv_step.output.mode);
    assert(DAB_MPPT_PHASE_HOLD_BEST == cv_step.output.stage);
    assert(cv_step.output.changed);

    /* The physical records are firmware-resident priors, not fixed commands.
     * At the r112 neighborhood they break only an equivalent live result in
     * favor of the already qualified EPS mode. */
    assert(8U == DAB_MPPT_PHASE_VALIDATED_RECORD_COUNT);
    assert(!g_dab_mppt_phase_validated_records[4].runtime_hint_eligible);
    dab_mppt_phase_search_t validated_tie;
    dab_mppt_phase_initialize(&validated_tie);
    validated_tie.mode_best[0] = (dab_mppt_phase_metric_t)
        {177.0F, 90.80F, 250.0F, 0.0F, 0.0F, true};
    validated_tie.mode_best[1] = (dab_mppt_phase_metric_t)
        {176.5F, 90.70F, 240.0F, 45.0F, 0.0F, true};
    in.vin_v = 20.6815F;
    in.vout_v = 36.0F;
    in.pin_w = 174.8499F;
    dab_mppt_phase_choose_best(&validated_tie, &in);
    assert(DAB_MPPT_PHASE_MODE_EPS == validated_tie.output.mode);
    assert(240.0F == validated_tie.output.selected_outer_phase_ns);

    /* A materially better live MPP must always override the historical
     * neighborhood hint. */
    dab_mppt_phase_search_t live_wins;
    dab_mppt_phase_initialize(&live_wins);
    live_wins.mode_best[0] = (dab_mppt_phase_metric_t)
        {190.0F, 89.0F, 300.0F, 0.0F, 0.0F, true};
    live_wins.mode_best[1] = (dab_mppt_phase_metric_t)
        {176.5F, 91.0F, 240.0F, 45.0F, 0.0F, true};
    dab_mppt_phase_choose_best(&live_wins, &in);
    assert(DAB_MPPT_PHASE_MODE_SPS == live_wins.output.mode);

    /* A large temporary power difference while returning from TPS to the
     * selected EPS point must not restart the search.  The lock anchor is
     * captured only after the 10 s grace and current-reference settling. */
    in.current_reference_a = 2.0F;
    in.current_active_a = 1.0F;
    run_mode_window(&s, &in, 30.0F, 90.0F, 9000U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == s.output.stage);
    assert(!s.hold_anchor_valid);
    in.current_active_a = 2.0F;
    run_mode_window(&s, &in, 55.0F, 90.0F, 1100U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == s.output.stage);
    assert(s.hold_anchor_valid);

    /* A selected point which cannot reproduce at least 90 percent of its
     * recorded Pin for 0.5 s must be searched again, never locked low. */
    dab_mppt_phase_search_t replay_fail;
    dab_mppt_phase_initialize(&replay_fail);
    replay_fail.output.active = true;
    replay_fail.output.selection_complete = true;
    replay_fail.output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    replay_fail.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    replay_fail.output.primary_inner_ns = 304.0F;
    replay_fail.output.secondary_inner_ns = 0.0F;
    replay_fail.output.selected_pin_w = 110.0F;
    replay_fail.stage_started_ms = 0U;
    in.now_ms = 10000U;
    in.vin_v = 22.0F;
    in.vout_v = 33.0F;
    in.current_reference_a = 3.0F;
    in.current_active_a = 3.0F;
    in.applied_primary_inner_ns = 304.0F;
    in.applied_secondary_inner_ns = 0.0F;
    step(&replay_fail, &in, 100U, 55.0F, 90.0F);
    assert(DAB_MPPT_PHASE_HOLD_BEST == replay_fail.output.stage);
    run_mode_window(&replay_fail, &in, 55.0F, 90.0F, 600U);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == replay_fail.output.stage);
    assert(!replay_fail.output.selection_complete);

    /* If a later mode trial pulls Vin below the configured range, the lower
     * controller must cancel the ranking trial and continuously unload using
     * the known low-duty TPS baseline. */
    dab_mppt_phase_search_t collapse_trial;
    dab_mppt_phase_initialize(&collapse_trial);
    collapse_trial.output.active = true;
    collapse_trial.output.stage = DAB_MPPT_PHASE_EPS_SEARCH;
    collapse_trial.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    in.vin_v = 17.0F;
    step(&collapse_trial, &in, 100U, 45.0F, 90.0F);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == collapse_trial.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == collapse_trial.output.mode);
    assert(DAB_MPPT_PHASE_RECOVERY_HIGH_NS ==
           collapse_trial.output.primary_inner_ns);
    assert(DAB_MPPT_PHASE_RECOVERY_LOW_NS ==
           collapse_trial.output.secondary_inner_ns);

    /* r120 crossed the normalized-voltage ratio while the source was already
     * collapsed.  Recovery must not swap the two physical bridge targets at
     * the lower Vin, because 180/832 ns can retain the previous high-power
     * PWM edge schedule and prevent the source from leaving CC. */
    dab_mppt_phase_search_t deep_collapse;
    dab_mppt_phase_initialize(&deep_collapse);
    deep_collapse.output.active = true;
    deep_collapse.output.stage = DAB_MPPT_PHASE_EPS_SEARCH;
    deep_collapse.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    in.vin_v = 14.2F;
    in.vout_v = 36.0F;
    step(&deep_collapse, &in, 100U, 148.0F, 86.0F);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == deep_collapse.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == deep_collapse.output.mode);
    assert(DAB_MPPT_PHASE_RECOVERY_HIGH_NS ==
           deep_collapse.output.primary_inner_ns);
    assert(DAB_MPPT_PHASE_RECOVERY_LOW_NS ==
           deep_collapse.output.secondary_inner_ns);

    /* Small battery-voltage drift stays in HOLD; a new voltage/power region
     * is re-ranked after 0.5 s of stable current tracking. The local
     * restart inherits the applied waveform and current reference. */
    dab_mppt_phase_search_t condition;
    dab_mppt_phase_initialize(&condition);
    condition.output.active = true;
    condition.output.selection_complete = true;
    condition.output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    condition.output.mode = DAB_MPPT_PHASE_MODE_SPS;
    condition.hold_anchor_valid = true;
    condition.hold_anchor_vin_v = 22.0F;
    condition.hold_anchor_vout_v = 33.0F;
    condition.hold_anchor_pin_w = 50.0F;
    condition.stage_started_ms = 0U;
    in.now_ms = 0U;
    in.vin_v = 22.0F;
    in.vout_v = 33.0F;
    in.current_reference_a = 2.0F;
    in.current_active_a = 2.0F;
    in.measured_output_current_a = 2.0F;
    in.applied_mode = DAB_MPPT_PHASE_MODE_SPS;
    in.applied_primary_inner_ns = 0.0F;
    in.applied_secondary_inner_ns = 0.0F;
    in.vout_v = 34.5F;
    run_mode_window(&condition, &in, 50.0F, 90.0F, 2100U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == condition.output.stage);
    in.vout_v = 35.2F;
    run_mode_window(&condition, &in, 50.0F, 90.0F, 400U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == condition.output.stage);
    run_mode_window(&condition, &in, 50.0F, 90.0F, 200U);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == condition.output.stage);
    assert(!condition.output.selection_complete);
    assert(condition.local_research);
    assert(DAB_MPPT_PHASE_MODE_SPS == condition.output.mode);

    /* If the selected topology reaches the full 480 ns authority and still
     * cannot follow the P&O current request, re-rank locally even though the
     * measured current is not yet settled. */
    dab_mppt_phase_search_t exhausted;
    dab_mppt_phase_initialize(&exhausted);
    exhausted.output.active = true;
    exhausted.output.selection_complete = true;
    exhausted.output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    exhausted.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    exhausted.output.primary_inner_ns = 120.0F;
    exhausted.output.secondary_inner_ns = 0.0F;
    exhausted.output.phase_cap_ns = DAB_MPPT_PHASE_SEARCH_PHASE_CAP_NS;
    exhausted.hold_anchor_valid = true;
    exhausted.hold_anchor_vin_v = 22.0F;
    exhausted.hold_anchor_vout_v = 36.0F;
    exhausted.hold_anchor_pin_w = 180.0F;
    in.now_ms = 0U;
    in.vin_v = 22.0F;
    in.vout_v = 36.0F;
    in.current_reference_a = 5.0F;
    in.current_active_a = 5.0F;
    in.measured_output_current_a = 3.0F;
    in.outer_saturated = true;
    in.applied_mode = DAB_MPPT_PHASE_MODE_EPS;
    in.applied_primary_inner_ns = 120.0F;
    in.applied_secondary_inner_ns = 0.0F;
    run_mode_window(&exhausted, &in, 120.0F, 90.0F, 400U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == exhausted.output.stage);
    run_mode_window(&exhausted, &in, 120.0F, 90.0F, 200U);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == exhausted.output.stage);
    assert(exhausted.local_research);
    assert(DAB_MPPT_PHASE_MODE_EPS == exhausted.output.mode);
    assert(120.0F == exhausted.output.primary_inner_ns);
    in.outer_saturated = false;

    /* A locked nominal-20 V point may dither by one ADC count around the
     * 17.5 V local floor.  HOLD must use the existing persistent condition
     * confirmation instead of the immediate mode-trial recovery branch. */
    dab_mppt_phase_search_t hold_floor;
    dab_mppt_phase_initialize(&hold_floor);
    hold_floor.output.active = true;
    hold_floor.output.selection_complete = true;
    hold_floor.output.stage = DAB_MPPT_PHASE_HOLD_BEST;
    hold_floor.output.mode = DAB_MPPT_PHASE_MODE_EPS;
    hold_floor.output.primary_inner_ns = 12.0F;
    hold_floor.output.secondary_inner_ns = 8.0F;
    hold_floor.output.selected_pin_w = 100.0F;
    hold_floor.hold_anchor_valid = true;
    hold_floor.hold_anchor_vin_v = 17.77F;
    hold_floor.hold_anchor_vout_v = 33.0F;
    hold_floor.hold_anchor_pin_w = 100.0F;
    in.now_ms = 0U;
    in.vin_v = 17.49F;
    in.vout_v = 33.0F;
    in.current_reference_a = 5.0F;
    in.current_active_a = 5.0F;
    in.measured_output_current_a = 5.0F;
    in.applied_primary_inner_ns = 12.0F;
    in.applied_secondary_inner_ns = 8.0F;
    step(&hold_floor, &in, 100U, 100.0F, 90.0F);
    assert(DAB_MPPT_PHASE_HOLD_BEST == hold_floor.output.stage);
    assert(hold_floor.output.selection_complete);
    in.vin_v = 16.6F;
    run_mode_window(&hold_floor, &in, 100.0F, 90.0F, 400U);
    assert(DAB_MPPT_PHASE_HOLD_BEST == hold_floor.output.stage);
    run_mode_window(&hold_floor, &in, 100.0F, 90.0F, 200U);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == hold_floor.output.stage);
    assert(!hold_floor.output.selection_complete);

    /* At the measured 22 V / CV33 point, SPS zero can itself collapse Vin.
     * Once that happens SPS is marked infeasible, recovery occurs in TPS,
     * and the next trial starts at EPS.  If EPS also collapses Vin, the
     * controller skips directly to the TPS search. */
    dab_mppt_phase_search_t skip;
    dab_mppt_phase_initialize(&skip);
    in.now_ms = 0U;
    in.vin_v = 22.0F;
    in.current_reference_a = 1.0F;
    in.current_active_a = 1.0F;
    dab_mppt_phase_update(&skip, &in);
    in.vin_v = 17.0F;
    run_mode_window(&skip, &in, 45.0F, 90.0F, 4000U);
    in.vin_v = 22.0F;
    step(&skip, &in, 100U, 45.0F, 90.0F);
    assert(DAB_MPPT_PHASE_SPS_OBSERVE == skip.output.stage);
    in.vin_v = 17.0F;
    step(&skip, &in, 100U, 45.0F, 90.0F);
    assert(skip.sps_infeasible);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == skip.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == skip.output.mode);
    in.vin_v = 22.0F;
    run_mode_window(&skip, &in, 45.0F, 90.0F, 2100U);
    assert(DAB_MPPT_PHASE_EPS_SETTLE == skip.output.stage);
    assert(DAB_MPPT_PHASE_MODE_EPS == skip.output.mode);
    in.vin_v = 17.0F;
    step(&skip, &in, 100U, 45.0F, 90.0F);
    assert(skip.eps_infeasible);
    assert(DAB_MPPT_PHASE_WAIT_SOURCE_PEAK == skip.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == skip.output.mode);
    in.vin_v = 22.0F;
    run_mode_window(&skip, &in, 45.0F, 90.0F, 2100U);
    assert(DAB_MPPT_PHASE_TPS_SETTLE == skip.output.stage);
    assert(DAB_MPPT_PHASE_MODE_TPS == skip.output.mode);

    in.enabled = false;
    dab_mppt_phase_update(&skip, &in);
    assert(DAB_MPPT_PHASE_DISABLED == skip.output.stage);
    assert(!skip.output.active);
    return 0;
}
