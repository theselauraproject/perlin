package org.apache.commons.lang3.mutable;
public class MutableInt extends Number implements Mutable<Number> { private int v; public MutableInt() {} public MutableInt(int i) { v = i; }
 public Number getValue() { return v; } public void setValue(Number x) { v = x.intValue(); } public void setValue(int x) { v = x; } public void add(int x) { v += x; } public void increment() { v++; } public void decrement() { v--; }
 public int getAndIncrement() { return v++; } public int incrementAndGet() { return ++v; } public int getAndAdd(int x) { int o = v; v += x; return o; } public int addAndGet(int x) { v += x; return v; }
 public int intValue() { return v; } public long longValue() { return v; } public float floatValue() { return v; } public double doubleValue() { return v; } public byte byteValue() { return (byte) v; } public int getAsInt() { return v; } }
