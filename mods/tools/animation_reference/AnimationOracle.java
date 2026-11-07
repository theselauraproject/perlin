import java.io.PrintWriter;
import java.util.Locale;
import net.minecraft.SharedConstants;
import net.minecraft.server.Bootstrap;
import net.minecraft.client.model.HumanoidModel;
import net.minecraft.client.model.geom.ModelPart;
import net.minecraft.client.model.geom.builders.CubeDeformation;
import net.minecraft.client.renderer.entity.state.HumanoidRenderState;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.component.SwingAnimation;

public final class AnimationOracle {
    private static void roots(String file) throws Exception {
        var unsafeField = sun.misc.Unsafe.class.getDeclaredField("theUnsafe");
        unsafeField.setAccessible(true);
        var unsafe = (sun.misc.Unsafe)unsafeField.get(null);
        var renderer = unsafe.allocateInstance(net.minecraft.client.renderer.entity.player.AvatarRenderer.class);
        var setup = net.minecraft.client.renderer.entity.player.AvatarRenderer.class.getDeclaredMethod("setupRotations",
            net.minecraft.client.renderer.entity.state.AvatarRenderState.class, com.mojang.blaze3d.vertex.PoseStack.class, float.class, float.class);
        setup.setAccessible(true);
        var directions = new net.minecraft.core.Direction[]{net.minecraft.core.Direction.SOUTH, net.minecraft.core.Direction.WEST, net.minecraft.core.Direction.NORTH, net.minecraft.core.Direction.EAST};
        try (var out = new PrintWriter(file)) {
            out.println("# Original AvatarRenderer.setupRotations with Java-to-Bedrock model coordinate conversion");
            out.println("# id yaw pitch age death swim glideTicks flightYaw scale height gliding spin sleeping inWater visuallySwimming flyingYaw upsideDown shaking bedDirection; column-major 4x4 model matrix (per pixel)");
            for (int id = 0; id < 120; ++id) {
                var state = new net.minecraft.client.renderer.entity.state.AvatarRenderState();
                state.bodyRot = id * 7.3f - 190;
                state.xRot = id % 150 - 75;
                state.ageInTicks = id * 1.31f;
                state.deathTime = id % 13 == 0 ? 1 + id % 20 : 0;
                state.swimAmount = id % 3 * .5f;
                state.fallFlyingTimeInTicks = id % 20;
                state.flyingYRot = id % 10 * .13f - .65f;
                state.boundingBoxHeight = 1.8f;
                state.isFallFlying = id % 4 == 0;
                state.isAutoSpinAttack = id % 6 == 1;
                state.pose = id % 7 == 0 ? net.minecraft.world.entity.Pose.SLEEPING : net.minecraft.world.entity.Pose.STANDING;
                state.isInWater = id % 2 == 0;
                state.isVisuallySwimming = id % 5 != 0;
                state.shouldApplyFlyingYRot = id % 3 == 0;
                state.isUpsideDown = id % 11 == 3;
                state.isFullyFrozen = id % 11 == 2;
                int bed = id % 5 - 1;
                state.bedOrientation = bed < 0 ? null : directions[bed];
                float scale = .9375f;
                var stack = new com.mojang.blaze3d.vertex.PoseStack();
                setup.invoke(renderer, state, stack, state.bodyRot, 1.0f);
                var matrix = new org.joml.Matrix4f(stack.last().pose());
                matrix.scale(-scale, -scale, scale).translate(0, -1.501f, 0).scale(1f / 16).translate(0, 24, 0).scale(1, -1, 1);
                out.printf(Locale.ROOT, "%d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %d %d %d %d %d %d", id,
                    state.bodyRot, state.xRot, state.ageInTicks, state.deathTime, state.swimAmount, state.fallFlyingTimeInTicks, state.flyingYRot, scale, state.boundingBoxHeight,
                    state.isFallFlying ? 1 : 0, state.isAutoSpinAttack ? 1 : 0, state.hasPose(net.minecraft.world.entity.Pose.SLEEPING) ? 1 : 0,
                    state.isInWater ? 1 : 0, state.isVisuallySwimming ? 1 : 0, state.shouldApplyFlyingYRot ? 1 : 0,
                    state.isUpsideDown ? 1 : 0, state.isFullyFrozen ? 1 : 0, bed);
                float[] values = new float[16]; matrix.get(values);
                for (float value : values) out.printf(Locale.ROOT, " %.9g", value);
                out.println();
            }
        }
    }
    public static void main(String[] args) throws Exception {
        SharedConstants.tryDetectVersion();
        Bootstrap.bootStrap();
        if (args.length > 1) roots(args[1]);
        ModelPart root = HumanoidModel.createMesh(CubeDeformation.NONE, 0).getRoot().bake(64, 64);
        var model = new HumanoidModel<HumanoidRenderState>(root);
        try (var out = new PrintWriter(args[0])) {
            out.println("# Minecraft Java 26.3; client SHA-1 e877b6a07acd633fb3bb475002175cec036e7b87");
            out.println("# id yaw pitch walk speed age attack swim speedScale useTicks chargeTicks crouch ride glide using useLeft mainLeft attackLeft rightPose leftPose; six parts: rotationXYZ offsetXYZ (radians, Bedrock translation axes)");
            float[] phases = {0, 3, 13.999f, 14.001f, 21.999f, 22.001f, 25.999f, 26.001f};
            float[] attacks = {0, .05f, .2f, .3f, .5f, .8f, .999f};
            for (int id = 0; id < 400; ++id) {
                var s = new HumanoidRenderState();
                boolean mainLeft = (id / 100 % 2) != 0;
                boolean useLeft = (id / 200 % 2) != 0;
                boolean attackLeft = (id % 3) == 0;
                s.mainArm = mainLeft ? HumanoidArm.LEFT : HumanoidArm.RIGHT;
                s.useItemHand = useLeft == mainLeft ? InteractionHand.MAIN_HAND : InteractionHand.OFF_HAND;
                s.speedValue = 1 + (id % 4) * .7f;
                s.ageScale = 1;
                s.walkAnimationPos = phases[id % phases.length];
                s.walkAnimationSpeed = (id % 5) * .25f;
                s.ageInTicks = id * 1.71f;
                s.xRot = -70 + id % 140;
                s.yRot = -80 + id % 160;
                s.isCrouching = (id % 4) == 1;
                s.isPassenger = (id % 11) == 2;
                s.isFallFlying = (id % 7) == 1;
                s.isUsingItem = (id % 3) != 0;
                s.swimAmount = (id % 3) * .5f;
                s.ticksUsingItem = id % 40;
                s.maxCrossbowChargeDuration = 10 + id % 30;
                s.rightArmPose = HumanoidModel.ArmPose.values()[id % 10];
                s.leftArmPose = HumanoidModel.ArmPose.values()[id / 10 % 10];
                s.currentSwing = new LivingEntity.SwingDescription(attackLeft == mainLeft ? InteractionHand.MAIN_HAND : InteractionHand.OFF_HAND, SwingAnimation.DEFAULT, 6);
                s.swingAnimation = attacks[id % attacks.length];
                model.setupAnim(s);
                out.printf(Locale.ROOT, "%d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %d %d %d %d %d %d", id,
                    s.yRot, s.xRot, s.walkAnimationPos, s.walkAnimationSpeed, s.ageInTicks, s.swingAnimation, s.swimAmount, s.speedValue, s.ticksUsingItem, s.maxCrossbowChargeDuration,
                    s.isCrouching ? 1 : 0, s.isPassenger ? 1 : 0, s.isFallFlying ? 1 : 0, s.isUsingItem ? 1 : 0, useLeft ? 1 : 0, mainLeft ? 1 : 0, attackLeft ? 1 : 0, s.rightArmPose.ordinal(), s.leftArmPose.ordinal());
                for (var part : new ModelPart[]{model.head, model.body, model.rightArm, model.leftArm, model.rightLeg, model.leftLeg}) {
                    var rest = part.getInitialPose();
                    out.printf(Locale.ROOT, " %.9g %.9g %.9g %.9g %.9g %.9g", part.xRot, part.yRot, part.zRot, part.x - rest.x(), -(part.y - rest.y()), part.z - rest.z());
                }
                out.println();
            }
        }
    }
}
