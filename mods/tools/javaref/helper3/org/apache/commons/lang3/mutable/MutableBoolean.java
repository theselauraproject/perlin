package org.apache.commons.lang3.mutable;
public class MutableBoolean implements Mutable<Boolean> { private boolean v; public MutableBoolean() {} public MutableBoolean(boolean b) { v = b; }
 public Boolean getValue() { return v; } public void setValue(Boolean x) { v = x; } public void setValue(boolean x) { v = x; } public void setTrue() { v = true; } public void setFalse() { v = false; }
 public boolean isTrue() { return v; } public boolean isFalse() { return !v; } public boolean booleanValue() { return v; } }
