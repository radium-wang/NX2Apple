package com.limelight.binding.input;

import android.media.AudioAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.vibrator.VibratorEnvelopeEffectInfo;
import android.os.vibrator.VibratorFrequencyProfile;
import android.util.Log;

/** Plays the strongest supported source band on a single frequency-capable actuator. */
final class RetroidDuoFrequencySink implements RawHapticsReceiver.Sink {
    private static final String TAG = "RpRawHaptics";
    private static final AudioAttributes GAME_AUDIO = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME).build();
    private final Vibrator vibrator;
    private final VibratorEnvelopeEffectInfo limits;
    private final VibratorFrequencyProfile profile;
    private boolean playing;
    private boolean reportedMultiple;
    private boolean reportedUnsupported;

    static RetroidDuoFrequencySink create(Vibrator vibrator) {
        if (!vibrator.areEnvelopeEffectsSupported()) {
            Log.w(TAG, "Duo system vibrator does not expose frequency envelopes");
            return null;
        }
        VibratorEnvelopeEffectInfo limits = vibrator.getEnvelopeEffectInfo();
        VibratorFrequencyProfile profile = vibrator.getFrequencyProfile();
        if (limits == null || profile == null || limits.getMaxSize() < 3) {
            Log.w(TAG, "Duo vibrator has no usable frequency profile");
            return null;
        }
        Log.i(TAG, "Duo frequency playback range Hz: " + profile.getMinFrequencyHz()
                + "–" + profile.getMaxFrequencyHz());
        return new RetroidDuoFrequencySink(vibrator, limits, profile);
    }

    private RetroidDuoFrequencySink(Vibrator vibrator, VibratorEnvelopeEffectInfo limits,
                                     VibratorFrequencyProfile profile) {
        this.vibrator = vibrator;
        this.limits = limits;
        this.profile = profile;
    }

    @Override public void apply(RawHapticsReceiver.Frame frame) {
        if (frame.player != 0) return;
        float[] bands = frame.getBands();
        float amplitude = 0;
        float frequency = 0;
        int activeBands = 0;
        for (int i = 0; i < bands.length; i += 2) {
            if (bands[i] == 0) continue;
            activeBands++;
            float hz = bands[i + 1];
            if (hz >= profile.getMinFrequencyHz() && hz <= profile.getMaxFrequencyHz()
                    && bands[i] > amplitude) {
                amplitude = bands[i];
                frequency = hz;
            }
        }
        if (amplitude == 0) {
            if (activeBands > 0 && !reportedUnsupported) {
                Log.w(TAG, "No active source frequency is in the Duo actuator range");
                reportedUnsupported = true;
            }
            stop();
            return;
        }
        if (activeBands > 1 && !reportedMultiple) {
            Log.i(TAG, "Single actuator: playing the strongest supported source band");
            reportedMultiple = true;
        }
        long ramp = Math.max(4, limits.getMinControlPointDurationMillis());
        long budget = Math.min(frame.holdMicros / 1000L, limits.getMaxDurationMillis());
        long sustain = Math.min(70, Math.min(budget - 2 * ramp, limits.getMaxControlPointDurationMillis()));
        if (ramp > limits.getMaxControlPointDurationMillis() || sustain < limits.getMinControlPointDurationMillis()) {
            stop();
            return;
        }
        try {
            VibrationEffect effect = new VibrationEffect.WaveformEnvelopeBuilder()
                    .setInitialFrequencyHz(frequency)
                    .addControlPoint(amplitude, frequency, ramp)
                    .addControlPoint(amplitude, frequency, sustain)
                    .addControlPoint(0, frequency, ramp)
                    .build();
            vibrator.vibrate(effect, GAME_AUDIO);
            playing = true;
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "Vibrator rejected the frequency envelope", e);
            stop();
        }
    }

    @Override public String mode() { return "frequency"; }

    @Override public void stop() {
        if (playing) {
            vibrator.cancel();
            playing = false;
        }
    }
}
