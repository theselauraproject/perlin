package org.apache.commons.lang3.mutable;
public class MutableObject<T> implements Mutable<T> { private T v; public MutableObject() {} public MutableObject(T v) { this.v = v; } public T getValue() { return v; } public void setValue(T x) { v = x; } public T get() { return v; } }
