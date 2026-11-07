import java.lang.reflect.*;
import java.util.*;
import java.util.function.Consumer;

// prints every (climate parameter point, biome) pair of the overworld preset, as the game's own builder produces them
public class BiomeDump {
    public static void main(String[] args) throws Exception {
        Class<?> b = Class.forName("net.minecraft.world.level.biome.OverworldBiomeBuilder");
        Object inst = b.getConstructor().newInstance();
        Method add = b.getDeclaredMethod("addBiomes", Consumer.class);
        add.setAccessible(true);
        List<Object> pairs = new ArrayList<>();
        Consumer<Object> sink = pairs::add;
        add.invoke(inst, sink);
        int nulls = 0; for (Object o : pairs) if (o == null) nulls++;
        System.err.println("collected " + pairs.size() + ", null " + nulls);
        pairs.removeIf(Objects::isNull);
        Class<?> pairClass = Class.forName("com.mojang.datafixers.util.Pair");
        Method first = pairClass.getMethod("getFirst"), second = pairClass.getMethod("getSecond");
        Class<?> pp = Class.forName("net.minecraft.world.level.biome.Climate$ParameterPoint");
        Class<?> par = Class.forName("net.minecraft.world.level.biome.Climate$Parameter");
        Method min = par.getMethod("min"), max = par.getMethod("max");
        String[] axes = {"temperature", "humidity", "continentalness", "erosion", "depth", "weirdness"};
        StringBuilder out = new StringBuilder();
        for (Object p : pairs) {
            Object point = first.invoke(p), key = second.invoke(p);
            StringBuilder line = new StringBuilder();
            for (String axis : axes) {
                Object range = pp.getMethod(axis).invoke(point);
                line.append(min.invoke(range)).append(' ').append(max.invoke(range)).append(' ');
            }
            line.append(pp.getMethod("offset").invoke(point)).append(' ');
            Object id = key.getClass().getMethod("identifier").invoke(key);
            line.append(id);
            out.append(line).append('\n');
        }
        System.out.print(out);
        System.err.println(pairs.size() + " points");
    }
}
