package it.unimi.dsi.fastutil;
import it.unimi.dsi.fastutil.doubles.*; import it.unimi.dsi.fastutil.ints.*; import it.unimi.dsi.fastutil.longs.*; import it.unimi.dsi.fastutil.floats.*; 
public class Helpers {
    public static DoubleList doubleOf(double[] a) { DoubleArrayList l = new DoubleArrayList(a.length); for (double v : a) l.add(v); return l; }
    public static IntList intOf(int[] a) { IntArrayList l = new IntArrayList(a.length); for (int v : a) l.add(v); return l; }
    public static LongList longOf(long[] a) { LongArrayList l = new LongArrayList(a.length); for (long v : a) l.add(v); return l; }
    public static FloatList floatOf(float[] a) { FloatArrayList l = new FloatArrayList(a.length); for (float v : a) l.add(v); return l; }
}
