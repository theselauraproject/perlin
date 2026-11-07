import java.lang.reflect.*;
import java.util.*;
import java.util.function.Consumer;

// asks the game's own parameter list (r-tree) which biome a set of climate samples lands in, next to its brute-force search
public class BiomeLookup {
    public static void main(String[] args) throws Exception {
        Class<?> b = Class.forName("net.minecraft.world.level.biome.OverworldBiomeBuilder");
        Object inst = b.getConstructor().newInstance();
        Method add = b.getDeclaredMethod("addBiomes", Consumer.class);
        add.setAccessible(true);
        List<Object> pairs = new ArrayList<>();
        Consumer<Object> sink = pairs::add;
        add.invoke(inst, sink);
        Class<?> listClass = Class.forName("net.minecraft.world.level.biome.Climate$ParameterList");
        Object list = listClass.getConstructor(List.class).newInstance(pairs);
        Method find = listClass.getMethod("findValue", Class.forName("net.minecraft.world.level.biome.Climate$TargetPoint"));
        Method brute = listClass.getMethod("findValueBruteForce", Class.forName("net.minecraft.world.level.biome.Climate$TargetPoint"));
        Constructor<?> tp = Class.forName("net.minecraft.world.level.biome.Climate$TargetPoint").getConstructor(long.class, long.class, long.class, long.class, long.class, long.class);
        // box edges seen in the table, to land targets exactly on them
        TreeSet<Long> edges = new TreeSet<>();
        Class<?> pp = Class.forName("net.minecraft.world.level.biome.Climate$ParameterPoint");
        Class<?> par = Class.forName("net.minecraft.world.level.biome.Climate$Parameter");
        String[] axes = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness"};
        Class<?> pairClass = Class.forName("com.mojang.datafixers.util.Pair");
        Method first = pairClass.getMethod("getFirst");
        for (Object p : pairs) {
            Object point = first.invoke(p);
            for (String a : axes) {
                Object r = pp.getMethod(a).invoke(point);
                edges.add((Long) par.getMethod("min").invoke(r));
                edges.add((Long) par.getMethod("max").invoke(r));
            }
        }
        List<Long> edgeList = new ArrayList<>(edges);
        Random rnd = new Random(2026);
        long[] lo = {-12000, -12000, -12000, -12000, -3000, -12000}, hi = {12000, 12000, 12000, 12000, 13000, 12000};
        int differ = 0, n = 30000;
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < n; i++) {
            long[] v = new long[6];
            boolean snap = i % 2 == 1;
            for (int k = 0; k < 6; k++) {
                if (snap && rnd.nextInt(3) != 0) v[k] = edgeList.get(rnd.nextInt(edgeList.size()));
                else v[k] = lo[k] + (long) (rnd.nextDouble() * (hi[k] - lo[k]));
            }
            Object t = tp.newInstance(v[0], v[1], v[2], v[3], v[4], v[5]);
            Object key = find.invoke(list, t), kb = brute.invoke(list, t);
            if (!key.equals(kb)) differ++;
            Object id = kb.getClass().getMethod("identifier").invoke(kb);
            out.append(v[0]).append(' ').append(v[1]).append(' ').append(v[2]).append(' ').append(v[3]).append(' ').append(v[4]).append(' ').append(v[5]).append(' ').append(id).append('\n');
        }
        System.out.print(out);
        System.err.println("rtree and brute force differ on " + differ + " of " + n);
    }
}
