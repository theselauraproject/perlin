package it.unimi.dsi.fastutil;
import it.unimi.dsi.fastutil.floats.Float2FloatFunction;
public class IdentityF2F implements Float2FloatFunction {
    public float get(float k) { return k; }
    public Float apply(Float k) { return k; }
}
