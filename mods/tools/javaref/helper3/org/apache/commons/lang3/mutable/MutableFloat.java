package org.apache.commons.lang3.mutable;
public class MutableFloat extends Number implements Mutable<Number> { private float v; public MutableFloat() {} public MutableFloat(float i) { v = i; }
 public Number getValue() { return v; } public void setValue(Number x) { v = x.floatValue(); } public void setValue(float x) { v = x; } public void add(float x) { v += x; }
 public int intValue() { return (int) v; } public long longValue() { return (long) v; } public float floatValue() { return v; } public double doubleValue() { return v; } }
