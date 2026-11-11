package org.apache.commons.lang3.mutable;
public class MutableLong extends Number implements Mutable<Number> { private long v; public MutableLong() {} public MutableLong(long i) { v = i; }
 public Number getValue() { return v; } public void setValue(Number x) { v = x.longValue(); } public void setValue(long x) { v = x; } public void add(long x) { v += x; } public void increment() { v++; }
 public int intValue() { return (int) v; } public long longValue() { return v; } public float floatValue() { return v; } public double doubleValue() { return v; } }
