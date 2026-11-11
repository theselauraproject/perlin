package org.apache.commons.lang3.mutable;
public class MutableDouble extends Number implements Mutable<Number> { private double v; public MutableDouble() {} public MutableDouble(double i) { v = i; }
 public Number getValue() { return v; } public void setValue(Number x) { v = x.doubleValue(); } public void setValue(double x) { v = x; } public void add(double x) { v += x; }
 public int intValue() { return (int) v; } public long longValue() { return (long) v; } public float floatValue() { return (float) v; } public double doubleValue() { return v; } }
