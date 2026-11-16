import com.limelight.binding.input.RawHapticsReceiver;
import java.nio.*;
import android.os.SystemClock;
public class RawReceiverTest {
    static byte[] frame(int seq,long epoch) {
        ByteBuffer b=ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(0x31485052).putShort((short)1).putShort((short)0).putInt(seq).putLong(epoch).putLong(1234).putInt(200000);
        for (float f:new float[]{.25f,164.81f,.5f,329.62f,.75f,171.81f,1f,336.62f}) b.putFloat(f);
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
        System.out.println("Raw receiver: exact bands, immutable snapshot, version/NaN rejection, ordering/wrap, epoch, timeout, disconnect reset: PASS");
    }
}
