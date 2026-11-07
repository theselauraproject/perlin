import net.minecraft.core.*;
import net.minecraft.data.worldgen.*;
import net.minecraft.resources.*;
import net.minecraft.tags.TagKey;
import net.minecraft.world.level.levelgen.*;
import net.minecraft.world.level.levelgen.densityfunction.*;
import net.minecraft.world.level.levelgen.synth.*;
import java.lang.reflect.*;
import java.util.*;

public class Oracle {
    static class Reg<T> implements HolderGetter<T> {
        final Map<ResourceKey<T>, Holder.Reference<T>> map = new LinkedHashMap<>();
        @SuppressWarnings("unchecked")
        public Optional<Holder.Reference<T>> get(ResourceKey<T> k) { return Optional.of(map.computeIfAbsent(k, x -> (Holder.Reference<T>) (Holder.Reference) new Ref((HolderOwner) this, (ResourceKey) x, null))); }
        public Optional<HolderSet.Named<T>> get(TagKey<T> t) { return Optional.empty(); }
    }
    static class Ref extends Holder.Reference<Object> { Ref(HolderOwner<Object> o, ResourceKey<Object> k, Object v) { super(Holder.Reference.Type.STAND_ALONE, o, k, v); } void bind(Object v) { bindValue(v); } }
    static class Ctx implements BootstrapContext<Object> {
        final Map<Identifier, Reg<Object>> regs = new HashMap<>();
        @SuppressWarnings("unchecked")
        Reg<Object> reg(Identifier id) { return regs.computeIfAbsent(id, k -> new Reg<>()); }
        @SuppressWarnings({"unchecked","rawtypes"})
        public Holder.Reference register(ResourceKey k, Object v) {
            Reg<Object> r = reg(k.registry());
            Holder.Reference<Object> h = r.map.get(k);
            if (h == null) h = new Ref(r, (ResourceKey<Object>) k, v); else ((Ref) h).bind(v);
            r.map.put((ResourceKey<Object>) k, h);
            return h;
        }
        @SuppressWarnings({"unchecked","rawtypes"})
        public HolderGetter lookup(ResourceKey k) { return reg(k.identifier()); }
        public java.util.stream.Stream listContextElements(ResourceKey k) { return java.util.stream.Stream.empty(); }
    }

    static void walk(Object o, String path, java.util.IdentityHashMap<Object,Boolean> seen, int depthLimit) throws Exception {
        if (o == null || depthLimit < 0 || seen.put(o, true) != null) return;
        Class<?> c = o.getClass();
        if (!c.getName().startsWith("net.minecraft") ) { return; }
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields()) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                f.setAccessible(true);
                Object v = f.get(o);
                String p2 = path + "." + c.getSimpleName() + "." + f.getName();
                if (v == null) { if (DensityFunction.class.isAssignableFrom(f.getType()) || Holder.class.isAssignableFrom(f.getType())) System.out.println("NULLFIELD " + p2); continue; }
                if (v instanceof DensityFunction || v instanceof Holder) {
                    Object inner = v;
                    if (v instanceof Holder<?> h) { try { inner = h.value(); } catch (Throwable ex) { System.out.println("UNBOUNDHOLDER " + p2); continue; } }
                    walk(inner, p2, seen, depthLimit - 1);
                } else if (v instanceof java.util.List<?> l) { int i = 0; for (Object x : l) walk(x, p2 + "[" + (i++) + "]", seen, depthLimit - 1); }
                else if (v instanceof Object[] arr) { int i = 0; for (Object x : arr) walk(x, p2 + "[" + (i++) + "]", seen, depthLimit - 1); }
                else if (v.getClass().getName().startsWith("net.minecraft") && !(v instanceof Enum)) walk(v, p2, seen, depthLimit - 1);
            }
    }
    public static void main(String[] a) throws Exception {
        Ctx ctx = new Ctx();
        NoiseData.bootstrap((BootstrapContext) ctx);
        NoiseRouterData.bootstrap((BootstrapContext) ctx);
        HolderGetter<NormalNoise> noises = (HolderGetter) ctx.reg(Identifier.parse("minecraft:worldgen/noise"));
        HolderGetter<DensityFunction> dfs = (HolderGetter) ctx.reg(Identifier.parse("minecraft:worldgen/density_function"));
        OverworldFunctionSet<ResourceKey<DensityFunction>> k = NoiseRouterData.OVERWORLD_FUNCTIONS;
        DensityFunction temp = NoiseRouterData.getFunction(dfs, k.temperature()), veg = NoiseRouterData.getFunction(dfs, k.vegetation()), cont = NoiseRouterData.getFunction(dfs, k.continents()), ero = NoiseRouterData.getFunction(dfs, k.erosion()), depth = NoiseRouterData.getFunction(dfs, k.depth()), ridges = NoiseRouterData.getFunction(dfs, NoiseRouterData.RIDGES), surf = NoiseRouterData.getFunction(dfs, k.chunkSurfaceLevel()), fin = NoiseRouterData.getFunction(dfs, k.finalDensity());
        NoiseRouter router = new NoiseRouter(temp, veg, cont, ero, depth, ridges, surf, fin);
        for (var e : ((Reg<?>) dfs).map.entrySet()) if (e.getValue().value() == null) System.out.println("UNBOUND " + e.getKey());

        long seed = Long.parseLong(a.length > 0 ? a[0] : "12345");
        RandomState rs = RandomState.create(noises, seed, false, null, 63, router);
        int[][] pts = {{0,64,0},{100,70,-200},{-512,100,640},{1000,-30,1000},{37,63,91}};
        String[] names = {"temp","veg","cont","ero","depth","ridges","surf","final"};
        DensityFunction[] fs = {temp, veg, cont, ero, depth, ridges, surf, fin};
        for (int[] p : pts) {
            StringBuilder sb = new StringBuilder(p[0] + " " + p[1] + " " + p[2]);
            for (int i = 0; i < fs.length; i++) {
                try { sb.append(String.format(" %s=%.9g", names[i], rs.sampleBlockValueUncached(fs[i], p[0], p[1], p[2]))); }
                catch (Throwable ex) { sb.append(" " + names[i] + "=ERR(" + ex + " CAUSE " + ex.getCause() + " @ " + (ex.getStackTrace().length > 0 ? ex.getStackTrace()[0] : "") + ")"); }
            }
            System.out.println(sb);
        }
        {
            ResourceKey<NormalNoise>[] keys = new ResourceKey[]{Noises.CONTINENTALNESS, Noises.EROSION, Noises.TEMPERATURE, Noises.RIDGE, Noises.SHIFT, Noises.CAVE_LAYER, Noises.AQUIFER_BARRIER};
            double[][] cs = {{0,0,0},{100.5,70.25,-200.75},{-512,100,640},{12345.678,-33.5,-98765.4321},{1e6,5,-1e6},{0.001,0.002,0.003}};
            for (ResourceKey<NormalNoise> nk : keys) {
                Noise n = rs.getOrCreateNoise(nk);
                StringBuilder sb = new StringBuilder("NOISE " + nk.identifier());
                for (double[] c : cs) { float f = n.get(c[0], c[1], c[2]); sb.append(" ").append(Integer.toHexString(Float.floatToIntBits(f))); }
                System.out.println(sb);
            }
        }
        {
            int[] xs = {-1000, -37, 0, 5, 123, 2048}, zs = {-513, 0, 64, 999}, ys = {-60, -10, 40, 63, 64, 70, 120, 200, 300};
            StringBuilder grid = new StringBuilder();
            for (int gx : xs) for (int gz : zs) for (int gy : ys)
                grid.append(String.format("GRID %d %d %d depth=%.9g ridges=%.9g final=%.9g%n", gx, gy, gz,
                    rs.sampleBlockValueUncached(depth, gx, gy, gz), rs.sampleBlockValueUncached(ridges, gx, gy, gz), rs.sampleBlockValueUncached(fin, gx, gy, gz)));
            java.nio.file.Files.writeString(java.nio.file.Path.of("/tmp/jh/grid.txt"), grid.toString());
        }
        System.out.println("noises=" + ctx.reg(Identifier.parse("minecraft:worldgen/noise")).map.size() + " dfs=" + ctx.reg(Identifier.parse("minecraft:worldgen/density_function")).map.size());
    }
}
