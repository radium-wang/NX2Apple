import com.limelight.binding.input.RawHapticsReceiver;
import java.nio.*;
import android.os.SystemClock;
public class RawReceiverTest {
    static byte[] frame(int seq, long epoch) {
        return frame(seq, epoch, SystemClock.now / 1000 - 10_000, 200_000, false);
    }
    static byte[] frame(int seq, long epoch, long source, int hold, boolean stop) {
        ByteBuffer b=ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(0x31485052).putShort((short)1).putShort((short)0).putInt(seq).putLong(epoch).putLong(source).putInt(hold);
        float[] bands = {.25f,164.81f,.5f,329.62f,.75f,171.81f,1f,336.62f};
        for (int band = 0; band < bands.length; ++band) b.putFloat(stop && band % 2 == 0 ? 0 : bands[band]);
        return b.array();
    }
    static void check(boolean b) { if (!b) throw new AssertionError(); }
    public static void main(String[] args) {
        RawHapticsReceiver.reset(); check(RawHapticsReceiver.snapshot(0)==null);
        RawHapticsReceiver.receive(frame(1,1));
        float[] raw=RawHapticsReceiver.snapshot(0).getBands();
        check(Float.floatToRawIntBits(raw[1])==Float.floatToRawIntBits(164.81f));
        check(raw[0]==.25f && raw[4]==.75f);
        raw[1]=0; check(RawHapticsReceiver.snapshot(0).getBands()[1]==164.81f);
        RawHapticsReceiver.receive(frame(0,1)); check(RawHapticsReceiver.snapshot(0).sequence==1);
        RawHapticsReceiver.receive(frame(2,1)); check(RawHapticsReceiver.snapshot(0).sequence==2);
        byte[] malformed=frame(3,1); malformed[4]=2; RawHapticsReceiver.receive(malformed);
        malformed=frame(3,1); ByteBuffer.wrap(malformed).order(ByteOrder.LITTLE_ENDIAN).putFloat(32,Float.NaN); RawHapticsReceiver.receive(malformed);
        check(RawHapticsReceiver.snapshot(0).sequence==2);
        RawHapticsReceiver.receive(frame(Integer.MAX_VALUE,2));
        RawHapticsReceiver.receive(frame(Integer.MIN_VALUE,2)); check(RawHapticsReceiver.snapshot(0).sequence==Integer.MIN_VALUE);
        SystemClock.now+=201_000_000L; check(RawHapticsReceiver.snapshot(0)==null);
        RawHapticsReceiver.receive(frame(1,3)); check(RawHapticsReceiver.snapshot(0).epoch==3);
        RawHapticsReceiver.reset(); check(RawHapticsReceiver.snapshot(0)==null);
        long nowMicros = SystemClock.now / 1000;
        long firstSource = nowMicros - 10_000;
        RawHapticsReceiver.receive(frame(1, 4, firstSource, 90_000, false));
        SystemClock.now += 30_000_000;
        RawHapticsReceiver.receive(frame(2, 4, firstSource + 5_000, 90_000, false));
        check(RawHapticsReceiver.snapshot(0).holdMicros == 65_000);
        SystemClock.now += 60_000_000;
        RawHapticsReceiver.receive(frame(3, 4, firstSource + 10_000, 90_000, false));
        check(RawHapticsReceiver.snapshot(0).sequence == 2);
        SystemClock.now += 35_000_000;
        RawHapticsReceiver.receive(frame(4, 4, firstSource + 15_000, 90_000, true));
        check(RawHapticsReceiver.snapshot(0).sequence == 4);
        check(RawHapticsReceiver.snapshot(0).getBands()[0] == 0);
        RawHapticsReceiver.receive(frame(3, 4));
        RawHapticsReceiver.receive(frame(99, 3));
        check(RawHapticsReceiver.snapshot(0).sequence == 4);
        SystemClock.now += 1_000;
        check(RawHapticsReceiver.snapshot(0) == null);
        RawHapticsReceiver.reset();
        RawHapticsReceiver.receive(frame(1, 1, nowMicros - 390_000, 90_000, false));
        check(RawHapticsReceiver.snapshot(0).holdMicros == 90_000);
        System.out.println("Raw receiver: bands, validation, order, remaining hold, stale pulse and late stop passed");
    }
}
