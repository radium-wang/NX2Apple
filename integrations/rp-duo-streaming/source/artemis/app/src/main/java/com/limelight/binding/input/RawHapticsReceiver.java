package com.limelight.binding.input;

import android.os.SystemClock;
import android.util.Log;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Retains original motor bands; deliberately does not label amplitude-only rumble as HD. */
public final class RawHapticsReceiver {
    private static final long MAX_ADDED_TRANSIT_MICROS = 75_000;
    private static final Frame[] latest = new Frame[4];
    private static final Frame[] observed = new Frame[4];
    private static long minimumClockOffsetMicros = Long.MAX_VALUE;
    private static long received;
    private static long stale;
    private static Sink sink;

    interface Sink {
        void apply(Frame frame);
        void stop();
        default String mode() { return "droidplay-intensity"; }
    }

    static synchronized void setSink(Sink next) {
        reset();
        sink = next;
    }
    private RawHapticsReceiver() {}

    public static final class Frame {
        public final int player, sequence, holdMicros;
        public final long epoch, sourceMicros, arrivalMicros;
        private final float[] bands;
        private Frame(ByteBuffer b, long now) {
            b.position(6);
            player = b.getShort() & 0xffff;
            sequence = b.getInt();
            epoch = b.getLong();
            sourceMicros = b.getLong();
            holdMicros = b.getInt();
            arrivalMicros = now;
            bands = new float[8];
            for (int i=0; i<8; i++) bands[i]=b.getFloat();
        }
        private Frame(Frame source, int remainingHoldMicros) {
            player = source.player;
            sequence = source.sequence;
            epoch = source.epoch;
            sourceMicros = source.sourceMicros;
            arrivalMicros = source.arrivalMicros;
            bands = source.bands;
            holdMicros = remainingHoldMicros;
        }
        private boolean hasIntensity() {
            return bands[0] > 0 || bands[2] > 0 || bands[4] > 0 || bands[6] > 0;
        }
        public float[] getBands() { return bands.clone(); }
    }

    public static synchronized void receive(byte[] bytes) {
        if (bytes == null || bytes.length != 64) return;
        ByteBuffer b=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (b.getInt()!=0x31485052 || b.getShort()!=1) return;
        Frame frame=new Frame(b,SystemClock.elapsedRealtimeNanos()/1000);
        if (frame.player>=4 || frame.sourceMicros<=0 || frame.holdMicros<=0 || frame.holdMicros>250000) return;
        for (int i=0;i<8;i++) {
            float f=frame.bands[i];
            if (Float.isNaN(f) || Float.isInfinite(f) || f<0 || f>((i&1)==0 ? 1 : 2000)) return;
        }
        Frame previous = observed[frame.player];
        if (previous != null && (Long.compareUnsigned(frame.epoch, previous.epoch) < 0
                || (previous.epoch == frame.epoch && frame.sequence - previous.sequence <= 0))) return;
        observed[frame.player] = frame;

        // The clocks have unrelated origins. The lowest observed offset includes normal
        // transport time, while the excess measures extra time a snapshot spent in transit.
        long offset = frame.arrivalMicros - frame.sourceMicros;
        minimumClockOffsetMicros = Math.min(minimumClockOffsetMicros, offset);
        long addedTransit = offset - minimumClockOffsetMicros;
        if (addedTransit < 0) addedTransit = Long.MAX_VALUE;
        if (frame.hasIntensity() && (addedTransit >= frame.holdMicros || addedTransit > MAX_ADDED_TRANSIT_MICROS)) {
            if (++stale <= 4 || stale % 100 == 0) {
                Log.i("RpRawHaptics", "Discarded old vibration snapshot, added transit " + addedTransit + " us");
            }
            return;
        }
        // A stop still releases the motor when late, as long as it is newer than the last packet.
        frame = new Frame(frame, (int) Math.max(0, frame.holdMicros - addedTransit));
        latest[frame.player] = frame;
        if (sink != null) sink.apply(frame);
        received++;
        if (received<=16 || received%100==0) {
            Log.i("RpRawHaptics", "rx="+received+" player="+frame.player+" seq="+Integer.toUnsignedString(frame.sequence)
                    +" epoch="+frame.epoch+" bands="+java.util.Arrays.toString(frame.bands)+" sink="+(sink == null ? "unavailable" : sink.mode()));
        }
    }

    /** Expired snapshots cannot be read back and restarted by a future motor sink. */
    public static synchronized Frame snapshot(int player) {
        if (player<0 || player>=4) return null;
        Frame f=latest[player];
        return f!=null && SystemClock.elapsedRealtimeNanos()/1000-f.arrivalMicros<=f.holdMicros ? f : null;
    }

    public static synchronized void reset() {
        if (sink != null) sink.stop();
        java.util.Arrays.fill(latest, null);
        java.util.Arrays.fill(observed, null);
        minimumClockOffsetMicros = Long.MAX_VALUE;
        received = 0;
        stale = 0;
    }
}
