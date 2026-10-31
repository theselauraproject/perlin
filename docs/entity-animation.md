Player animation and body rotation use the Minecraft Java 26.3 client as a
behavioral reference. The reference JAR has SHA-1
`e877b6a07acd633fb3bb475002175cec036e7b87`; it is not bundled with Perlin.
The relevant classes are `LivingEntity`, `WalkAnimationState`,
`BodyRotationControl`, `HumanoidModel`, and `AvatarRenderer`.

`JavaAnimation.hpp/.cpp` implement the player torso movement direction, backward
walking, 50-degree head/body limit, attack turn, walking phase and speed, idle
arm bob, crouching, riding, attack shoulder offsets, swimming cycle, and common
held-item arm poses, including dominant-arm and active-arm priority rules. Mobs with head/body geometry use the separate mob rotation
controller (75-degree head limit, ten-tick idle delay and ten-tick realignment).
Their species-specific animation controllers remain resource driven.

Motion is sampled at 20 Hz along the interpolated frame trajectory. Rendering
interpolates the last two simulation poses. Catch-up frames sample each crossed
tick rather than applying the entire frame's movement once; teleports reset
walking, torso interpolation and cape state. Velocity queries use elapsed time.
World, first-person and inventory doll views maintain separate animation state.
Riding, sleeping and death stop the walking cycle.

Remote and third-person players run `BodyRotation::PlayerTick` through
`PlayerBodyClock`: each 20 Hz tick sees the position and head yaw at its own
time, so the body is identical at every frame rate. The drawn body trails the
head by one tick of interpolation; the 50-degree head/body cap is applied to the
drawn pair as well.

Java's player model parts are independent. Bedrock arms and heads often inherit
the torso transform, and armor can have a different hierarchy again. The pose
adapter fits each part's world transform to the actual skin or armor skeleton.
Sleeves, skin layers and held-item locators inherit the fitted bones. Native
player poses supersede the old additive player locomotion/crouch overrides;
first-person animations and non-player animation controllers keep their existing
resource animation paths. Custom geometries without the six humanoid parts
retain their resource animations. `PERLIN_NO_JAVA_ANIMATIONS=1` selects the
resource-driven player pose for comparison.

Native Bedrock bone Y rotation maps directly to the model-space Y axis. The
vanilla humanoid bow pose must rotate its left wrist inward toward the bow.
The Java pose adapter preserves its existing world matrices through this
conversion, including compound poses and singular head turns. The animation
test is registered in CTest with both numerical JAR fixture files.

Validation:

```
cmake --build build --target Perlin entity_animation_selftest entity_state_selftest packet_selftest
ctest --test-dir build -R '^(entity_animation_selftest|entity_state_selftest|packet_selftest|attachable_selftest)$' --output-on-failure
```

The animation self-test checks 10, 15, 20, 30, 60, 144 and 240 FPS trajectories,
angle wrapping, idle and backward movement, mob idle alignment, walk damping,
riding and attack angles, swimming phases, and actual bone world matrices with
compound crouch/attack/swim poses and attached sleeves.

The test also compares against 400 poses produced by the original Java
`HumanoidModel.setupAnim` and 120 matrices produced by the original
`AvatarRenderer.setupRotations`. The fixtures cover both dominant arms, both
swing arms, all ten common arm poses, item priority, crossbow charge durations,
compound attack/swim/crouch states, swimming transitions, gliding, spin attacks,
sleep directions, death and upside-down rotation. These are executed model
outputs rather than a second implementation of the animation formulas.

To regenerate the fixtures with a legally obtained client JAR and JDK 26+:

```
python3 tools/gen_java_animation_reference.py --jar /path/to/minecraft-26.3-client.jar --java-home /path/to/jdk
```

The driver boots Minecraft's registries headlessly; it does not launch the game
or open a window. The renderer constructor requires a graphics context, so the
root-transform driver allocates its receiver without invoking that constructor,
then calls the original transformation method. The local cache contains the
client libraries; fixture generation verifies the client and library checksums.
Only numerical outputs and the driver are committed.

Entity-state tests round-trip actual metadata packets with simultaneous high
and low flags, test the protocol flag-word split, and verify crawling, sleeping,
shield blocking, dominant arm and swing-hand state. The checked-in protocol
schema supplies the authoritative flag positions. In particular, laying down
starts the extended word at bit 64; sleeping is extended bit 12, and swimming is
bit 57. Flag words are decoded as integers, without floating-point conversion.

The root adapter accounts for the Java/Bedrock axis and model-origin differences
before applying scale. Full-body lighting normals follow the resulting pose.
The inventory doll uses the same root transform in its own view state.

Offline visual checks can use `PERLIN_PREVIEW_ENTITIES=player:cape,player:alex`,
`PERLIN_PREVIEW_ENTITY_WALK=1` (with `PERLIN_PREVIEW_ENTITY_WALK_DIR=90` to walk sideways
or `180` backward), `PERLIN_PREVIEW_ENTITY_HEAD=70` (head turns that far between 0.5 and 1.5 s), `PERLIN_PREVIEW_TURN=1`,
`PERLIN_PREVIEW_SNEAK=1`, `PERLIN_PREVIEW_SWIM=1`,
`PERLIN_PREVIEW_ARMOR=diamond`, `PERLIN_PREVIEW_USING=1`,
`PERLIN_PREVIEW_LEFT_HANDED=1`, `PERLIN_PREVIEW_OFFHAND=minecraft:shield`,
`PERLIN_PREVIEW_BLOCKING=1` or `PERLIN_PREVIEW_ATTACK=0.3` with the usual
`PERLIN_PREVIEW=1`, `PERLIN_PREVIEW_SHOT` and `--offline` flags.

Remaining fidelity work includes charged crossbow item components, the newer
spear swing descriptions, and a broader live-server/state-transition audit.
Swing-hand and dominant-arm state can now represent both sides; remote generic
swing packets still select the main hand unless a source supplies an explicit
hand. Mob locomotion remains species-specific and resource driven. Passing the
humanoid and root fixtures does not by itself prove every creature animation or
every live interaction is correct.

Resource animation sampling also handles outgoing Catmull-Rom interpolation,
per-channel `query.key_frame_lerp_time` (including query caching), discontinuous
first/last keyframes and one-element uniform vectors. Animations without an
explicit duration derive it from their last keyframe. The sampler test includes
the installed vanilla sheep grazing expression and a non-linear four-key curve;
these checks do not establish complete Bedrock animation parity.
