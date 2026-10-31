#include <array>
#include "client/player/PlayerPhysics.hpp"
#include "client/player/PerlinPhysicsNumerics.hpp"
#include "client/player/PerlinSineTable.hpp"
#include "client/player/PerlinWaterDrag.hpp"
#include "client/player/PerlinGliding.hpp"
#include "client/player/PerlinRelativeMovement.hpp"
#include "client/player/PerlinMoveFinalization.hpp"
#include "client/player/PerlinSupport.hpp"
#include "client/player/PerlinScaffolding.hpp"
#include "client/player/PerlinPowderSnow.hpp"
#include "client/player/PerlinFallDistance.hpp"
#include "client/player/PerlinWorldCollision.hpp"
#include "client/player/PerlinBlockGeometry.hpp"
#include "client/player/PerlinCollisionCache.hpp"
#include "client/player/PerlinBlockSlowdown.hpp"
#include "client/player/PerlinSneak.hpp"
#include "client/player/PerlinInsideQuery.hpp"
#include "client/player/PerlinJumpRequests.hpp"
#include "client/player/PerlinHoney.hpp"
#include "client/player/PerlinFluidState.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace perlin::physics {
    bds::ScaffoldingCell World::ClimberCell(int x, int y, int z) const {
        using Cell = bds::ScaffoldingCell;
        const auto shape = Query(x, y, z);
        if (shape.scaffoldingStability >= 0) return Cell::Scaffold;
        if (shape.powderSnow) return Cell::PowderSnow;
        if (shape.fluid == 1) return Cell::Water;
        return shape.isAir && shape.Empty() ? Cell::Air : Cell::Other;
    }
    namespace {
        constexpr float kFloatEps = 1.1920929e-7f;
        constexpr float kClimbSpeed = 0.2f;

        struct Vec3 {
            double x = 0, y = 0, z = 0;
            double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
            double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
        };

        double F(double v) {
            return static_cast<double>(static_cast<float>(v));
        }

        struct ShapeMetadata {
            uint8_t surface;
            bool preserveHorizontalMotion;
            bool hasBlock;
            bds::BlockPosition position;
        };

        bds::CollisionBox CollisionBox(const Box& box);
        Box PhysicsBox(const bds::CollisionBox& box);

        void Gather(const World& world, const Box& region, std::vector<Box>& out,
                    std::vector<ShapeMetadata>* metadata = nullptr, const Box* actorBox = nullptr,
                    bool fallThrough = false, float fallDistance = 0, bool canStandOnSnow = false) {
            out.clear();
            if (metadata)
                metadata->clear();
            const auto nativeRegion = CollisionBox(region);
            bds::CollectUnavailableChunkBarriers(nativeRegion, [&](int32_t x, int32_t z) {
                return bds::ChunkCollisionState{world.CollisionChunkReady(x, z)};
            }, [&](const bds::CollisionBox& box) {
                out.push_back({box[0], box[1], box[2], box[3], box[4], box[5]});
                if (metadata)
                    metadata->push_back({0, true, false, {}});
            });
            bds::CollectTerrainCells(nativeRegion, world.CollisionHeightLimits(),
                                    [&](int32_t x, int32_t z) { return world.CollisionColumnPresent(x, z); },
                                    [&](int32_t x, int32_t y, int32_t z) {
                BlockShape shape = world.CollisionQuery(x, y, z);
                const bds::BlockPosition position{x, y, z};
                if (shape.scaffoldingStability >= 0 && actorBox &&
                    bds::ScaffoldingSupports(CollisionBox(*actorBox), position,
                                             shape.scaffoldingStability, fallThrough)) {
                    shape.boxes.push_back({0, 0, 0, 1, 1, 1});
                }
                if (shape.powderSnow && actorBox) {
                    const float height = bds::PowderSnowSupportHeight(static_cast<float>(actorBox->y0),
                        static_cast<float>(y) + 1.0f, fallThrough, fallDistance, canStandOnSnow);
                    if (height > 0)
                        shape.boxes.push_back({0, 0, 0, 1, height, 1});
                }
                const auto offset = shape.randomOffset ? bds::GenerateBlockOffset(*shape.randomOffset, position)
                                                       : bds::Float3{};
                const auto origin = bds::BlockCollisionOrigin(offset, position);
                for (const auto& local : shape.boxes) {
                    const auto box = bds::TranslateBlockCollisionShape(CollisionBox(local), origin);
                    if (bds::AcceptBlockCollisionShape(box, &nativeRegion)) {
                        out.push_back({box[0], box[1], box[2], box[3], box[4], box[5]});
                        if (metadata)
                            metadata->push_back({shape.surface, false, true, {x, y, z}});
                    }
                }
            }, [&](const bds::CollisionBox& box) {
                out.push_back({box[0], box[1], box[2], box[3], box[4], box[5]});
                if (metadata)
                    metadata->push_back({0, false, false, {}});
            });
        }

        // MoveCollisionSystem 0x14360a290 -> 0x143a10780: whole entity boxes after every block shape, no block,
        // kept when they overlap the query region grown by 0.25
        template<class Emit>
        void CollectEntityBoxes(const World& world, const bds::CollisionBox& region, Emit&& emit) {
            const auto* entities = world.CollisionEntityBoxes();
            if (!entities)
                return;
            bds::CollisionBox grown;
            for (int axis = 0; axis < 3; ++axis) {
                const float low = bds::detail::Round(region[axis] + -0.25f);
                const float high = bds::detail::Round(region[axis + 3] + 0.25f);
                grown[axis] = low < high ? low : high;
                grown[axis + 3] = low > high ? low : high;
            }
            for (const Box& entity : *entities) {
                const auto box = CollisionBox(entity);
                if (box[0] < grown[3] && grown[0] < box[3] && box[1] < grown[4] && grown[1] < box[4] &&
                    box[2] < grown[5] && grown[2] < box[5])
                    emit(box);
            }
        }

        bool NoCollision(const World& world, const Box& box, std::vector<Box>& scratch) {
            Gather(world, box, scratch);
            CollectEntityBoxes(world, CollisionBox(box), [&](const bds::CollisionBox& b) { scratch.push_back(PhysicsBox(b)); });
            for (const Box& b : scratch)
                if (b.Intersects(box))
                    return false;
            return true;
        }

        bds::CollisionBox CollisionBox(const Box& box) {
            return {static_cast<float>(box.x0), static_cast<float>(box.y0), static_cast<float>(box.z0),
                    static_cast<float>(box.x1), static_cast<float>(box.y1), static_cast<float>(box.z1)};
        }

        Box PhysicsBox(const bds::CollisionBox& box) {
            return {box[0], box[1], box[2], box[3], box[4], box[5]};
        }

        struct Motion {
            Box box;
            Vec3 resolved;
            bds::Float3 requested;
            Vec3 velocity;
            bool cx = false, cy = false, cz = false;
            bool stepped = false;
            uint8_t supportingSurface = 0;
            bool preserveHorizontalMotion = false;
            float penetration = 0;
        };

        Motion ResolveMotion(const World& world, const Box& start, const Vec3& velocity, Vec3 storedVelocity, bool wasOnGround,
                             std::vector<Box>& scratch, bds::CollisionVector recoveryLimits, float stepHeight,
                             bool flyingAbility, bool& oneShotStep, bool sneakEdge = false, bool fallThrough = false,
                             float fallDistance = 0, bool canStandOnSnow = false) {
            Motion result;
            const auto search =
                bds::SearchMovement(CollisionBox(start),
                                    {static_cast<float>(velocity.x), static_cast<float>(velocity.y),
                                     static_cast<float>(velocity.z)},
                                    stepHeight);
            std::vector<ShapeMetadata> metadata;
            bds::CollisionCache cache;
            bds::RefreshCollisionCache(cache, search.region, [&](const bds::CollisionBox& region) {
                std::vector<ShapeMetadata> incoming;
                Gather(world, PhysicsBox(region), scratch, &incoming, &start, fallThrough, fallDistance, canStandOnSnow);
                const size_t base = metadata.size();
                metadata.insert(metadata.end(), incoming.begin(), incoming.end());
                std::vector<bds::CollisionRecord> records;
                records.reserve(scratch.size());
                for (size_t i = 0; i < scratch.size(); ++i) {
                    const auto& entry = incoming[i];
                    records.push_back({CollisionBox(scratch[i]),
                                       {entry.hasBlock ? static_cast<uint64_t>(base + i + 1) : 0, entry.position},
                                       entry.preserveHorizontalMotion});
                }
                return records;
            });
            // the move request is rebuilt every tick (TravelMoveRequestSystem 0x147ccdf90 adds velocity to it), so the
            // cache never outlives one move and the entity pass runs on every refresh
            CollectEntityBoxes(world, search.region, [&](const bds::CollisionBox& box) {
                cache.shapes.push_back(box);
                cache.blocks.push_back({});
            });
            const auto& shapes = cache.shapes;
            auto requested = search.movement;
            if (sneakEdge) {
                const auto clipped = bds::ClipSneakMovement(
                    CollisionBox(start), stepHeight, requested,
                    {static_cast<float>(storedVelocity.x), static_cast<float>(storedVelocity.y),
                     static_cast<float>(storedVelocity.z)}, shapes);
                requested = clipped.movement;
                storedVelocity = {clipped.velocity[0], clipped.velocity[1], clipped.velocity[2]};
            }
            result.velocity = storedVelocity;
            result.requested = {requested[0], requested[1], requested[2]};
            const std::array<bds::CollisionVector, 3> moves{
                {{0, requested[1], 0}, {requested[0], 0, 0}, {0, 0, requested[2]}}};
            auto resolved = bds::ResolveMoves(CollisionBox(start), moves, shapes, recoveryLimits);
            result.penetration = resolved.penetration;
            if (bds::ShouldAutoStep(stepHeight, requested, resolved.displacement, wasOnGround, flyingAbility,
                                    oneShotStep))
                result.stepped = bds::TryAutoStep(CollisionBox(start), requested, stepHeight, shapes,
                                                  recoveryLimits, resolved);
            result.box = PhysicsBox(resolved.box);
            const auto support = bds::SelectSupportingShape(resolved.box, shapes);
            if (support.index && cache.blocks[*support.index].block)
                result.supportingSurface = metadata[cache.blocks[*support.index].block - 1].surface;
            result.preserveHorizontalMotion = cache.preserveHorizontalMotion;
            result.resolved = {resolved.displacement[0], resolved.displacement[1], resolved.displacement[2]};
            result.cx = std::fabs(requested[0] - resolved.displacement[0]) > kFloatEps;
            result.cy = std::fabs(requested[1] - resolved.displacement[1]) > kFloatEps;
            result.cz = std::fabs(requested[2] - resolved.displacement[2]) > kFloatEps;
            return result;
        }

        Vec3 FinalizeMotion(State& state, const Motion& motion, const Vec3& velocity) {
            const auto result = bds::FinalizeMove(
                CollisionBox(motion.box), 0.0f, motion.requested,
                {static_cast<float>(motion.resolved.x), static_cast<float>(motion.resolved.y),
                 static_cast<float>(motion.resolved.z)},
                {static_cast<float>(velocity.x), static_cast<float>(velocity.y), static_cast<float>(velocity.z)},
                {state.horizontalCollision, state.verticalCollision, state.onGround,
                 state.collision, state.collidableMobNear, state.horizontalCollisionX, state.horizontalCollisionZ},
                false, motion.preserveHorizontalMotion);
            state.x = result.position.x;
            state.y = result.position.y;
            state.z = result.position.z;
            state.box = motion.box;
            state.hasBox = true;
            state.horizontalCollision = result.flags.horizontalCollision;
            state.horizontalCollisionX = result.flags.horizontalCollisionX;
            state.horizontalCollisionZ = result.flags.horizontalCollisionZ;
            state.verticalCollision = result.flags.verticalCollision;
            state.onGround = result.flags.onGround;
            state.fallDistance = bds::FallDistanceAfterMove(state.fallDistance,
                static_cast<float>(motion.resolved.y), state.onGround, state.inWater);
            state.collision = result.flags.collision;
            state.collidableMobNear = result.flags.collidableMobNear;
            return {result.velocity.x, result.velocity.y, result.velocity.z};
        }

        Vec3 LandingResponse(State& state, Vec3 velocity, uint8_t supportingSurface, bool collided, bool sneaking,
                             std::optional<float>* appliedRestitutionY = nullptr) {
            bds::Float3 motion{static_cast<float>(velocity.x), static_cast<float>(velocity.y),
                               static_cast<float>(velocity.z)};
            if (collided) {
                const auto surface = supportingSurface == 1 ? bds::LandingSurface::Slime
                                     : supportingSurface == 2 ? bds::LandingSurface::Bed
                                                            : bds::LandingSurface::Ordinary;
                // bounce event needs the block position, empty would drop it
                const bds::Float3 pos{static_cast<float>(state.x), static_cast<float>(state.y), static_cast<float>(state.z)};
                motion = bds::VerticalCollision(motion, pos, surface, sneaking, false, false, true,
                                                state.restitution).velocity;
                // ResetVerticalVelocitySystem 0x147b8d050: every vertical collision, rising bodies on slime too;
                // runs after the bounce is computed and before it is applied
                motion.y = 0.0f;
            }
            if (state.restitution) {
                if (appliedRestitutionY)
                    *appliedRestitutionY = state.restitution->y;
                motion = bds::ApplyRestitution(motion, *state.restitution);
                state.restitution.reset();
            }
            return {motion.x, motion.y, motion.z};
        }

        struct CellRange {
            int x0, y0, z0, x1, y1, z1;
        };

        CellRange Cells(const Box& b) {
            return {static_cast<int>(std::floor(b.x0)), static_cast<int>(std::floor(b.y0)),
                    static_cast<int>(std::floor(b.z0)), static_cast<int>(std::floor(b.x1)),
                    static_cast<int>(std::floor(b.y1)), static_cast<int>(std::floor(b.z1))};
        }

        struct Environment {
            bool water = false, lava = false, web = false;
        };

        bool TouchesFluid(const World& world, const Box& shrunk, int kind) {
            return bds::CollectLiquidCells(CollisionBox(shrunk), kind == 1 ? 5 : 6, false,
                [&](bds::BlockPosition position) {
                    const auto shape = world.Query(position[0], position[1], position[2]);
                    return shape.fluid == 1 ? 5u : (shape.fluid == 2 ? 6u : 0u);
                }, [](bds::BlockPosition, uint8_t) {});
        }

        Environment Scan(const World& world, const Box& box) {
            Environment env;
            const auto bounds = bds::MakeLiquidQueryBounds(CollisionBox(box));
            env.lava = TouchesFluid(world, PhysicsBox(bounds.lava), 2);
            env.water = TouchesFluid(world, PhysicsBox(bounds.water), 1);
            const Box shrunk = box.Inflate(-0.001);
            const CellRange c = Cells(shrunk);
            for (int y = c.y0; y <= c.y1; y++)
                for (int x = c.x0; x <= c.x1; x++)
                    for (int z = c.z0; z <= c.z1; z++)
                        env.web = env.web || world.Query(x, y, z).web;
            return env;
        }

        bool SolidMotion(const BlockShape& s) { return !s.Empty() && !s.fluid && !s.climbable && !s.web; }

        struct Cell { int mat = 0; int depth = 0; };
        Cell LiquidCell(const World& w, int x, int y, int z, bool& unknown) {
            const BlockShape s = w.Query(x, y, z);
            if (!s.fluid) return {};
            if (s.fluidDepth == 255) { unknown = true; return {s.fluid, 0}; }
            return {s.fluid, s.fluidDepth};
        }
        inline int EffDepth(int d) { return d < 8 ? d : 0; }

        void FlowAt(const World& w, int x, int y, int z, int mat, float out[3]) {
            static const int dirAxis[4] = {2, 0, 2, 0};
            static const int dirStep[4] = {-1, 1, 1, -1};
            bool unk = false;
            const Cell c = LiquidCell(w, x, y, z, unk);
            const bool same = c.mat == mat;
            const int depth = same ? EffDepth(c.depth) : -1;
            float f[3] = {0, 0, 0};
            for (int d = 0; d < 4; d++) {
                int n[3] = {x, y, z};
                n[dirAxis[d]] += dirStep[d];
                const Cell o = LiquidCell(w, n[0], n[1], n[2], unk);
                if (o.mat == mat) {
                    f[dirAxis[d]] += static_cast<float>((EffDepth(o.depth) - depth) * dirStep[d]);
                    continue;
                }
                if (SolidMotion(w.Query(n[0], n[1], n[2])))
                    continue;
                const Cell below = LiquidCell(w, n[0], n[1] - 1, n[2], unk);
                if (below.mat == mat)
                    f[dirAxis[d]] += static_cast<float>((EffDepth(below.depth) - depth + 8) * dirStep[d]);
            }
            if (same && c.depth >= 8) {
                bool solid = false;
                for (int d = 0; d < 4 && !solid; d++) {
                    int n[3] = {x, y, z};
                    n[dirAxis[d]] += dirStep[d];
                    const BlockShape a = w.Query(n[0], n[1], n[2]);
                    const BlockShape b = w.Query(n[0], n[1] + 1, n[2]);
                    auto full = [](const BlockShape& s) {
                        return s.boxes.size() == 1 && s.boxes[0].x0 <= 0 && s.boxes[0].z0 <= 0 && s.boxes[0].x1 >= 1 &&
                               s.boxes[0].z1 >= 1 && s.boxes[0].y0 <= 0 && s.boxes[0].y1 >= 1;
                    };
                    solid = full(a) || full(b);
                }
                if (solid) {
                    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
                    if (l >= 0.0001f) { f[0] /= l; f[1] /= l; f[2] /= l; } else f[0] = f[1] = f[2] = 0;
                    f[1] += -6.0f;
                }
            }
            const float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (l >= 0.0001f) { out[0] = f[0] / l; out[1] = f[1] / l; out[2] = f[2] / l; }
            else out[0] = out[1] = out[2] = 0;
        }

        void ApplyCurrent(State& s, const World& w, const Box& sensingBox) {
            const auto bounds = bds::MakeLiquidQueryBounds(CollisionBox(sensingBox));
            for (int mat = 2; mat >= 1; mat--) {
                const auto& box = mat == 1 ? bounds.water : bounds.lava;
                const float lx = box[0], ly = box[1], lz = box[2], hx = box[3], hy = box[4], hz = box[5];
                const int x0 = static_cast<int>(std::floor(lx)), y0 = static_cast<int>(std::floor(ly)),
                          z0 = static_cast<int>(std::floor(lz));
                const int x1 = static_cast<int>(std::floor(hx + 1.0f)), y1 = static_cast<int>(std::floor(hy + 1.0f)),
                          z1 = static_cast<int>(std::floor(hz + 1.0f));
                bool unknown = false, any = false;
                std::vector<std::array<int, 4>> cells;
                for (int y = y0; y < y1; y++)
                    for (int z = z0; z < z1; z++)
                        for (int x = x0; x < x1; x++) {
                            const Cell c = LiquidCell(w, x, y, z, unknown);
                            if (c.mat == mat) { any = true; cells.push_back({x, y, z, c.depth}); }
                        }
                if (!any) continue;
                if (unknown) return;
                bool admitted = false;
                for (auto& c : cells) if (c[3] > 0) { admitted = true; break; }
                if (!admitted) {
                    static const int ax[4] = {2, 0, 2, 0}, st[4] = {-1, 1, 1, -1};
                    for (auto& c : cells) {
                        for (int d = 0; d < 4 && !admitted; d++) {
                            const int edge = ax[d] == 0 ? (st[d] < 0 ? x0 : x1 - 1) : (st[d] < 0 ? z0 : z1 - 1);
                            if (c[ax[d] == 0 ? 0 : 2] != edge) continue;
                            int n[3] = {c[0], c[1], c[2]};
                            n[ax[d]] += st[d];
                            bool u = false;
                            const Cell o = LiquidCell(w, n[0], n[1], n[2], u);
                            if (o.mat == mat && !u && o.depth != 0) admitted = true;
                        }
                    }
                }
                if (!admitted) return;
                float sum[3] = {0, 0, 0};
                for (auto& c : cells) {
                    float f[3];
                    FlowAt(w, c[0], c[1], c[2], mat, f);
                    sum[0] += f[0]; sum[1] += f[1]; sum[2] += f[2];
                }
                const float l = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                if (l >= 0.0001f) {
                    const float k = mat == 1 ? 0.014f : 0.0035f;
                    s.vx = F(static_cast<float>(s.vx) + sum[0] / l * k);
                    s.vy = F(static_cast<float>(s.vy) + sum[1] / l * k);
                    s.vz = F(static_cast<float>(s.vz) + sum[2] / l * k);
                }
                return;
            }
        }

        void MoveRelative(State& s, double strafeIn, double forwardIn, double speed, bool currentMovementMath) {
            const auto movement = currentMovementMath ? bds::CurrentRelativeMovement : bds::RelativeMovement;
            const auto velocity = movement(
                {static_cast<float>(s.vx), static_cast<float>(s.vy), static_cast<float>(s.vz)}, s.yaw,
                {-static_cast<float>(strafeIn), 0.0f, static_cast<float>(forwardIn)}, static_cast<float>(speed));
            s.vx = velocity.x;
            s.vy = velocity.y;
            s.vz = velocity.z;
        }

        double DampHorizontal(double value, float retention) {
            return bds::DampComponent(static_cast<float>(value), retention);
        }

        // TravelMoveRequestSystem 0x147ccdf90 builds the request from posDelta, before the block slowdown;
        // 0x14ce2d080 copies it and the pre-move position for the sprint stop test
        void RecordMoveRequest(State& state) {
            state.requestX = static_cast<float>(state.vx);
            state.requestZ = static_cast<float>(state.vz);
            state.requestFromX = static_cast<float>(state.x);
            state.requestFromZ = static_cast<float>(state.z);
        }

        Vec3 ApplyStoredBlockSlowdown(State& state, Vec3 requested) {
            bds::Float3 multiplier{state.stuckX, state.stuckY, state.stuckZ};
            bds::Float3 movement{static_cast<float>(requested.x), static_cast<float>(requested.y),
                                 static_cast<float>(requested.z)};
            bds::Float3 velocity{static_cast<float>(state.vx), static_cast<float>(state.vy),
                                 static_cast<float>(state.vz)};
            bds::ApplyBlockSlowdown(multiplier, movement, velocity);
            state.stuckX = multiplier.x;
            state.stuckY = multiplier.y;
            state.stuckZ = multiplier.z;
            state.vx = velocity.x;
            state.vy = velocity.y;
            state.vz = velocity.z;
            return {movement.x, movement.y, movement.z};
        }

        const BlockShape& Below(const World& world, const State& s, BlockShape& storage) {
            // float add on the box bottom (GroundTravelType 0x146d1f730, DefaultMove 0x1484c0920)
            storage = world.Query(static_cast<int>(std::floor(s.x)),
                                  static_cast<int>(std::floor(static_cast<float>(s.y) + -0.1f)),
                                  static_cast<int>(std::floor(s.z)));
            return storage;
        }
    }

    Box Box::Expand(double dx, double dy, double dz) const {
        Box b = *this;
        (dx < 0 ? b.x0 : b.x1) += dx;
        (dy < 0 ? b.y0 : b.y1) += dy;
        (dz < 0 ? b.z0 : b.z1) += dz;
        return b;
    }

    bool Box::Intersects(const Box& o) const {
        return static_cast<float>(x1) > static_cast<float>(o.x0) && static_cast<float>(x0) < static_cast<float>(o.x1) &&
               static_cast<float>(y1) > static_cast<float>(o.y0) && static_cast<float>(y0) < static_cast<float>(o.y1) &&
               static_cast<float>(z1) > static_cast<float>(o.z0) && static_cast<float>(z0) < static_cast<float>(o.z1);
    }

    bool World::InsideRegionReady(const bds::CollisionBox& bounds) const {
        return bds::InsideRegionReady(bounds, false, [&](int32_t x, int32_t z) {
            return bds::ChunkCollisionState{CollisionChunkReady(x, z)};
        });
    }

    // sub_140493140 for the sneak part; item use is a later system (0x14ce4c200)
    MoveVector ProcessMoveInput(float forward, float strafe, bool sneaking, bool inWater, float itemUseModifier,
                                float sneakScale) {
        const float lengthSq = forward * forward + strafe * strafe;
        if (lengthSq > 1.0f) {
            const float length = std::sqrt(lengthSq);
            forward /= length;
            strafe /= length;
        }
        if (sneaking && !inWater) {
            forward *= sneakScale;
            strafe *= sneakScale;
        }
        const float useScale = itemUseModifier * itemUseModifier;
        return {forward * useScale, strafe * useScale};
    }

    // SneakingSystem 0x14b3307b0: cvtsi2ss, mulss 0.15f, addss 0.3f, minss 1.0f
    float SneakSpeed(int swiftSneakLevel, bool sneaking) {
        if (swiftSneakLevel <= 0 || !sneaking)
            return 0.3f;
        const float scaled = static_cast<float>(swiftSneakLevel) * 0.15f;
        return std::fmin(scaled + 0.3f, 1.0f);
    }

    float ClampAttribute(float value, float min, float max, const std::vector<AttributeModifierValue>& modifiers) {
        float cap = max;
        for (const auto& m : modifiers)
            if (m.operation == 3)
                cap = std::fmin(m.amount, cap);
        return cap < value ? cap : std::fmax(value, min);
    }

    float RecalculateAttribute(const std::array<float, 3>& base, const std::vector<AttributeModifierValue>& modifiers,
                               float current, std::array<float, 2>& limits) {
        // additions, then multiply-base on the added base, then multiply-total in list order
        std::array<float, 3> added = base;
        for (const auto& m : modifiers)
            if (m.operation == 0 && m.operand >= 0 && m.operand < 3)
                added[m.operand] = m.amount + added[m.operand];
        std::array<float, 3> total = added;
        for (const auto& m : modifiers)
            if (m.operation == 1 && m.operand >= 0 && m.operand < 3) {
                const float scaled = added[m.operand] * m.amount;
                total[m.operand] = scaled + total[m.operand];
            }
        for (const auto& m : modifiers)
            if (m.operation == 2 && m.operand >= 0 && m.operand < 3) {
                const float factor = m.amount + 1.0f;
                total[m.operand] = factor * total[m.operand];
            }
        limits = {total[0], total[1]};
        // modifiers that leave the base unchanged keep the old value; none at all resets it (movement: Attribute byte 0 set)
        const float value = !modifiers.empty() && !(total[2] != base[2]) ? current : total[2];
        return ClampAttribute(value, limits[0], limits[1], modifiers);
    }

    namespace {
        double BoxCentre(double lo, double hi) {
            return F((static_cast<float>(lo) + static_cast<float>(hi)) * 0.5f);
        }
    }

    Box BoundingBox(const State& s, bool sneaking) {
        const float half = (s.mobSize ? (*s.mobSize)[0] : static_cast<float>(kWidth)) * 0.5f;
        const float x = static_cast<float>(s.x), y = static_cast<float>(s.y), z = static_cast<float>(s.z);
        const float h = static_cast<float>(s.Height(sneaking));
        if (s.hasBox && BoxCentre(s.box.x0, s.box.x1) == F(s.x) && BoxCentre(s.box.z0, s.box.z1) == F(s.z) &&
            static_cast<float>(s.box.y0) == y) {
            Box b = s.box;
            b.y1 = static_cast<double>(y + h);
            return b;
        }
        return {static_cast<double>(x - half), static_cast<double>(y), static_cast<double>(z - half),
                static_cast<double>(x + half), static_cast<double>(y + h), static_cast<double>(z + half)};
    }

    bool TouchingWater(const State& s, const World& world) {
        const auto env = Scan(world, BoundingBox(s, s.collisionHeight == static_cast<float>(kSneakHeight)));
        return bds::SelectFluidMaterial(env.water, env.lava) == 5;
    }

    bool PoseFits(const State& s, const World& world, double height) {
        thread_local std::vector<Box> scratch;
        auto bounds = CollisionBox(BoundingBox(s, false));
        bounds[4] = bounds[1] + static_cast<float>(height);
        for (int axis = 0; axis < 3; ++axis) {
            bounds[axis] += 0.01f;
            bounds[axis + 3] -= 0.01f;
            if (bounds[axis] > bounds[axis + 3])
                bounds[axis] = bounds[axis + 3] = (bounds[axis] + bounds[axis + 3]) * 0.5f;
        }
        const Box box{bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]};
        return NoCollision(world, box, scratch);
    }

    static void StepMovement(State& s, const Input& in, const Attributes& attr, const World& world) {
        thread_local std::vector<Box> scratch;
        const float swimSpeedMultiplier = bds::SwimSpeedMultiplier(in.swimming, s.movementEffects);
        const Box fluidSensingBox = BoundingBox(s, s.collisionHeight == static_cast<float>(kSneakHeight));
        s.crawling = in.crawl && !in.flying;
        s.swimming = in.swimming && !in.flying;
        s.gliding = in.gliding && !in.flying;
        const bool sneaking = in.sneak && !in.flying;
        const bool crouched = (in.sneak || in.forceCrouch) && !in.flying;
        // UpdateHorizontalPoseSystem / bounding box request 0x142D006B0: horizontal pose first, then sneak
        const float collisionHeight = static_cast<float>(s.Height(crouched));
        if (s.collisionHeight != collisionHeight) {
            s.collisionHeight = collisionHeight;
            s.hasBox = false;
        }

        s.x = F(s.x); s.y = F(s.y); s.z = F(s.z);
        s.vx = F(s.vx); s.vy = F(s.vy); s.vz = F(s.vz);
        // GlideInputSystem 0x146d09030 adds (0, 0.1f, 0.0f) in the input phase
        if (in.glideLift) {
            s.vx = F(static_cast<float>(s.vx) + 0.0f);
            s.vy = F(static_cast<float>(s.vy) + 0.1f);
            s.vz = F(static_cast<float>(s.vz) + 0.0f);
        }

        if (!in.flying)
            ApplyCurrent(s, world, fluidSensingBox);

        const bool grounded = s.onGround;
        if (!in.jump)
            s.jumpDelay = 0;

        const Environment env = Scan(world, fluidSensingBox);
        const uint32_t fluidMaterial = bds::SelectFluidMaterial(env.water, env.lava);
        bds::UpdateWaterState(fluidMaterial, s.inWater, s.fallDistance,
            s.waterSplashRequest, [] { return false; }, [] { return false; });
        s.inLava = bds::WasInLava(fluidMaterial);
        bds::UpdateLavaFallDistance(fluidMaterial, s.fallDistance);
        const BlockShape feet = world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                                            static_cast<int>(std::floor(s.z)));
        // DefaultMoveSystems 0x1484c0b40, AutoClimbSystem 0x14332c490, jump 0x14ad34580: wall climbers always,
        // else CAN_CLIMB with a climbable block or powder snow the body can stand on
        const auto climbing = [&](const BlockShape& block) {
            return in.wallClimbing ||
                   (in.canClimb && (block.climbable || (block.powderSnow && in.canStandOnPowderSnow)));
        };
        s.onClimbable = climbing(feet);
        bds::ResetClimbingFallDistance(s.fallDistance, in.wallClimbing, in.canClimb,
                                      feet.climbable, feet.powderSnow, in.canStandOnPowderSnow);

        BlockShape frictionStorage;
        const BlockShape& under = Below(world, s, frictionStorage);
        const float blockFriction = under.Empty() && !under.fluid && under.friction <= 0 ? 0.6f : under.friction;

        float nf = in.forward, ns = in.strafe;
        {
            const float lengthSq = nf * nf + ns * ns;
            if (lengthSq > 1.0f) {
                const float length = std::sqrt(lengthSq);
                nf /= length;
                ns /= length;
            }
        }
        const auto intent = in.mobIntent ? bds::TravelIntentResult{*in.mobIntent, 0.0f, *in.mobIntent}
                                         : bds::AttenuateTravelIntent({ns, 0.0f, nf}, 0.0f);
        const double forward = intent.movement.z;
        const double strafe = intent.movement.x;

        // TravelTypeSensingSystem 0x147cce890 reads MovementAbilities, updated a tick after a double tap; the
        // vertical control and its hover friction (VerticalFlySpeedControlSystem 0x142cf1c60) follow the new state
        const bool flyingTravel = in.flying != in.flyToggled;
        if (flyingTravel) {
            if (s.hasTeleported)
                return;
            const bool hover = in.flying && std::max(std::fabs(in.forward), std::fabs(in.strafe)) < 0.01f;
            MoveRelative(s, strafe, forward, static_cast<double>(attr.flySpeed) * (in.sprint ? 2.0 : 1.0),
                         attr.currentMovementMath);
            if (in.flying)
                s.vy = bds::FlyingControl(static_cast<float>(s.vy), attr.verticalFlySpeed,
                    hover, in.creativeFlight, in.flyUp, in.flyDown, in.flyUpSlow, in.flyDownSlow);
            // FlyingPlayerStuckOnGroundWorkaroundSystem 0x14333df70: runs before collision, lets a grounded flyer lift off
            if (grounded && s.vy == 0)
                s.vy = std::numeric_limits<float>::denorm_min();
            const float hMod = hover ? (in.creativeFlight ? 0.375f : 0.75f) : 1.0f;
            RecordMoveRequest(s);
            const Vec3 req = ApplyStoredBlockSlowdown(s, {s.vx, s.vy, s.vz});
            if (in.noClip) {
                s.hasBox = false;
                s.x = F(s.x + req.x);
                s.y = F(s.y + req.y);
                s.z = F(s.z + req.z);
                s.collidableMobNear = false;
                // FinalizeMoveSystem 0x145dd9980 keeps last tick's collision tags under no-clip, and the
                // velocity resets (0x147b8c020, 0x147b8d050) still act on them; no support block, no bounce
                Vec3 kept{s.vx, s.vy, s.vz};
                if (s.horizontalCollision) {
                    if (s.horizontalCollisionX)
                        kept.x = 0;
                    if (s.horizontalCollisionZ)
                        kept.z = 0;
                }
                const auto velocity = LandingResponse(s, kept, 0, s.verticalCollision, in.sneak);
                s.vx = velocity.x;
                s.vy = velocity.y;
                s.vz = velocity.z;
                s.jumpDelay = 0;
                const float h = bds::FlyingHorizontalRetention(1.0f,
                    hover ? std::optional<float>(hMod) : std::nullopt, attr.airDrag);
                s.vx = DampHorizontal(s.vx, h);
                s.vz = DampHorizontal(s.vz, h);
                s.vy = bds::FlyingVerticalDrag(static_cast<float>(s.vy), s.fallDistance, attr.airDrag);
                return;
            }
            const double groundFriction = grounded ? blockFriction : 1.0;
            const Box box = BoundingBox(s, false);
            const Motion m =
                ResolveMotion(world, box, req, {s.vx, s.vy, s.vz}, grounded, scratch, bds::RecoveryLimits(s.recovery),
                              attr.maxAutoStep, in.flying, s.canAlwaysAutoStep, false,
                              in.gravity.descendThroughBlock, s.fallDistance, in.canStandOnPowderSnow);
            bds::UpdateRecovery(s.recovery, m.penetration >= 1.0e-6f, in.pushTowardsClosestSpace);
            auto finalized = FinalizeMotion(s, m, m.velocity);
            finalized = LandingResponse(s, finalized, m.supportingSurface, m.cy, in.sneak);
            s.vx = finalized.x;
            s.vy = finalized.y;
            s.vz = finalized.z;
            const float h = bds::FlyingHorizontalRetention(static_cast<float>(groundFriction),
                hover ? std::optional<float>(hMod) : std::nullopt, attr.airDrag);
            s.vx = DampHorizontal(s.vx, h);
            s.vz = DampHorizontal(s.vz, h);
            s.vy = bds::FlyingVerticalDrag(static_cast<float>(s.vy), s.fallDistance, attr.airDrag);
            return;
        }

        // flight starts this tick: input-phase vertical control on top of ground/air travel, gravity and drag
        std::optional<float> flyFrictionOverride;
        if (in.flying && !s.hasTeleported) {
            const bool hover = std::max(std::fabs(in.forward), std::fabs(in.strafe)) < 0.01f;
            s.vy = bds::FlyingControl(static_cast<float>(s.vy), attr.verticalFlySpeed,
                hover, in.creativeFlight, in.flyUp, in.flyDown, in.flyUpSlow, in.flyDownSlow);
            if (hover)
                flyFrictionOverride = in.creativeFlight ? 0.375f : 0.75f;
        }

        const bool water = s.inWater, lava = s.inLava;
        auto gravity = in.gravity;
        if (in.overDescendableBlock)
            gravity.descendThroughBlock = in.sneak;
        if (gravity.descendThroughBlock && in.overDescendableBlock) {
            s.vy = -0.15f;
            s.fallDistance = 0;
        }
        // ground and air friction (0x1433563e0, 0x1433307f0) take the hover override as 0.91 * (coef * override)
        const float friction = flyFrictionOverride
            ? bds::FlyingHorizontalRetention(grounded ? blockFriction : 1.0f, flyFrictionOverride, attr.airDrag)
            : grounded ? 0.91f * blockFriction : 0.91f;
        double relSpeed;
        if (water) {
            relSpeed = bds::WaterAcceleration(attr.speed, attr.underwaterSpeed, attr.speed,
                swimSpeedMultiplier, attr.waterWalkSpeedEnchant, attr.dolphin, grounded, true);
        } else if (lava) {
            // lava travel speed 0x14CE37DE0: the lava_movement attribute as is
            relSpeed = attr.lavaSpeed;
        } else if (grounded) {
            float accFriction = blockFriction;
            // soul sand drag (GroundTravelType 0x146d1f730); soul speed boots skip it
            if (under.surface == 4 && !attr.soulSpeed)
                accFriction *= 1.225f;
            relSpeed = bds::GroundAcceleration(attr.speed, accFriction);
        } else if (attr.airSpeed) {
            relSpeed = *attr.airSpeed;
        } else {
            // player air speed 0x14336f010: sprint value is 0x3cd4fdf3, one ulp under 0.026f
            relSpeed = in.sprint ? std::bit_cast<float>(0x3cd4fdf3u) : 0.02f;
        }

        const bool jumpBlockedBySwim = (s.swimAmount > 0.0f && s.swimAmount < 1.0f)
                                    || (in.swimming && !in.headInWater);
        const bool scaffoldJump = in.jump && !jumpBlockedBySwim &&
                                  (gravity.inScaffolding || in.inAscendableBlock) &&
                                  !gravity.descendThroughBlock;
        if (scaffoldJump) {
            s.vy = 0.15f;
            s.jumpDelay = 10;
        }
        // MobJump 0x14AD34580: swim block wins first, scaffolding and climbing jumps return before the fluid lift
        if (in.jump && (water || lava)) {
            if (jumpBlockedBySwim) {
                if (water) s.vy = 0.0f;
            } else if (!scaffoldJump && !s.onClimbable) {
                s.previousJumpPosition = {static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z)};
                s.vy = F(static_cast<float>(s.vy) + 0.04f);
            }
        }

        if (water && in.swimming && !in.jump) {
            const float look = in.lookY;
            const float gain = look < -0.2f ? 0.085f : 0.06f;
            if (look <= 0.0f || in.headInWater)
                s.vy = F(static_cast<float>(s.vy) + (look - static_cast<float>(s.vy)) * gain);
            else
                s.vy = 0;
        }

        const bool glide = in.gliding && !water && !lava && !s.hasTeleported;

        // the scaffolding lift returns before the climb branch in the jump system
        const bool climbJump = in.jump && !jumpBlockedBySwim && !scaffoldJump && s.onClimbable;
        // jump permission 0x14333bef0: any shape crossing minY - 0.2 under the body wins (queried without an actor,
        // so powder snow has none); with none, the block at the feet decides
        const auto jumpOnPowderSnow = [&] {
            Box probe = BoundingBox(s, crouched);
            probe.y0 = probe.y1 = static_cast<double>(F(static_cast<float>(probe.y0) - 0.2f));
            return NoCollision(world, probe, scratch) &&
                   world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                               static_cast<int>(std::floor(s.z))).powderSnow;
        };
        const bool jumpInitiated = in.jump && !scaffoldJump && !jumpBlockedBySwim && !glide && !climbJump && s.onGround && s.jumpDelay == 0 && !water && !lava &&
            bds::GroundJumpAllowed((gravity.inScaffolding ? 0x20ull : 0ull) |
                                   (gravity.overScaffolding ? 0x40ull : 0ull) |
                                   (gravity.descendThroughBlock ? 0x80ull : 0ull), jumpOnPowderSnow(),
                                   [&] { return in.canStandOnPowderSnow; }, [] { return false; });
        if (jumpInitiated) {
            bds::JumpParameters jump;
            jump.bodyMinimumY = static_cast<float>(BoundingBox(s, crouched).y0);
            jump.position = {static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z)};
            jump.velocity = {static_cast<float>(s.vx), static_cast<float>(s.vy), static_cast<float>(s.vz)};
            jump.previousJumpPosition = s.previousJumpPosition;
            jump.yaw = s.yaw;
            jump.sprinting = in.sprint;
            jump.currentMovementMath = attr.currentMovementMath;
            jump.customHeight = attr.jumpHeight;
            if (attr.jumpBoost != 0)
                jump.effectAmplifier = std::bit_cast<int32_t>(static_cast<uint32_t>(attr.jumpBoost) - 1);
            const auto result = bds::GroundJump(jump, [&](bds::BlockPosition position) {
                return world.Query(position[0], position[1], position[2]).jumpProperties;
            });
            s.vx = result.velocity.x;
            s.vy = result.velocity.y;
            s.vz = result.velocity.z;
            s.previousJumpPosition = result.previousJumpPosition;
            s.jumpDelay = 10;
        }

        // JumpFromGroundSystem (0x147028c25) is registered before DefaultMoveSystems (0x14702b483): the sprint jump
        // boost 0x14ad32d50 lands in posDelta before the input acceleration 0x1484c0920 is added
        if (glide) {
            const bool boost = s.movementEffects[static_cast<int32_t>(bds::MovementEffectType::GlideBoost)].type != -1;
            const auto velocity = bds::GlideVelocity(
                {static_cast<float>(s.vx), static_cast<float>(s.vy), static_cast<float>(s.vz)},
                s.previousRotation, {in.pitch, s.yaw}, attr.slowFalling, boost, s.fallDistance);
            s.vx = velocity.x;
            s.vy = velocity.y;
            s.vz = velocity.z;
        } else if (!s.hasTeleported) {
            MoveRelative(s, strafe, forward, relSpeed, attr.currentMovementMath);
        }

        if (s.hasTeleported)
            return;

        if (climbJump)
            s.vy = kClimbSpeed;
        // DefaultMoveSystems Ground/Air 0x1484c0b40: water, lava and glide travel skip the clamp; climbing up
        // only comes from the jump above and AutoClimbSystem after the move
        if (s.onClimbable && !water && !lava && !glide) {
            if (static_cast<float>(s.vy) < -kClimbSpeed)
                s.vy = -kClimbSpeed;
            // sneak-hold property (+0x134 bit 2) comes with every climbable block, never with powder snow
            if (sneaking && s.vy < 0 && feet.climbable)
                s.vy = 0;
        }
        if (water && sneaking)
            s.vy = F(static_cast<float>(s.vy) - 0.04f);

        RecordMoveRequest(s);
        Vec3 req = ApplyStoredBlockSlowdown(s, {s.vx, s.vy, s.vz});
        const Box box = BoundingBox(s, crouched);
        bool requestSneakFlag = false;
        const bool clipSneak = bds::ShouldClipSneak(in.actorMovementFlags, sneaking, grounded, requestSneakFlag);
        const Motion m = ResolveMotion(world, box, req, {s.vx, s.vy, s.vz}, grounded, scratch, bds::RecoveryLimits(s.recovery),
                                       attr.maxAutoStep, in.flying, s.canAlwaysAutoStep, clipSneak,
                                       gravity.descendThroughBlock, s.fallDistance, in.canStandOnPowderSnow);
        bds::UpdateRecovery(s.recovery, m.penetration >= 1.0e-6f, in.pushTowardsClosestSpace);
        Vec3 v = FinalizeMotion(s, m, m.velocity);
        std::optional<float> restitutionY;
        v = LandingResponse(s, v, m.supportingSurface, m.cy, sneaking, &restitutionY);
        const auto bounceGravity = restitutionY
            ? bds::RequestGravityCorrection(*restitutionY, m.requested.y, static_cast<float>(m.resolved.y))
            : std::nullopt;

        // AutoClimbSystem Ground/Air 0x14332c490: needs ground or air travel (no water, lava or glide)
        const bool autoClimb =
            !water && !lava && !glide && (m.cx || m.cz) &&
            climbing(world.Query(static_cast<int>(std::floor(s.x)), static_cast<int>(std::floor(s.y)),
                                 static_cast<int>(std::floor(s.z))));
        if (autoClimb)
            v.y = kClimbSpeed;

        if (water || lava) {
            if (water) {
                const auto dragged = bds::WaterDrag(
                    {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)},
                    s.onGround, in.sprint, swimSpeedMultiplier, attr.waterWalkSpeedEnchant, attr.waterDrag);
                v = {dragged.x, dragged.y, dragged.z};
            } else {
                v.x = F(static_cast<float>(v.x) * 0.5f);
                v.y = F(static_cast<float>(v.y) * 0.5f);
                v.z = F(static_cast<float>(v.z) * 0.5f);
            }
            if (water)
                v.y = bds::PostWaterVertical(static_cast<float>(v.y), s.fallDistance, attr.levitation, in.swimming,
                                             bounceGravity);
            else if (attr.levitation > 0)
                v.y = bds::LevitateVelocity(static_cast<float>(v.y), attr.levitation);
            else
                v.y = bds::ApplyGravity(static_cast<float>(v.y), s.fallDistance, {-0.02f, false}, bounceGravity);
        } else if (!glide) {
            v.y = bds::PostGroundVertical(static_cast<float>(v.y), s.fallDistance, gravity,
                                         attr.levitation, attr.slowFalling, autoClimb, false, attr.airDrag,
                                         bounceGravity);
            v.x = DampHorizontal(v.x, friction);
            v.z = DampHorizontal(v.z, friction);
        }

        if ((water || lava) && (m.cx || m.cz)) {
            const float dy = ((static_cast<float>(box.y0) - static_cast<float>(s.y)) + 0.6f)
                             + static_cast<float>(v.y);
            const Box probe = BoundingBox(s, crouched).Offset(static_cast<float>(v.x), dy,
                                                              static_cast<float>(v.z));
            if (!TouchesFluid(world, probe, 1) && !TouchesFluid(world, probe, 2) &&
                NoCollision(world, probe, scratch))
                v.y = 0.3f;
        }
        // slime/honey onStandOn 0x146d7aee0: one-sided vy < 0.1, a fast rise (bounce) is not damped
        if (s.onGround && !sneaking && !(static_cast<float>(v.y) >= 0.1f)) {
            const BlockShape stand = world.Query(static_cast<int>(std::floor(s.x)),
                                                 static_cast<int>(std::floor(s.y - 0.5)),
                                                 static_cast<int>(std::floor(s.z)));
            if (stand.surface == 1 || stand.surface == 3) {
                const float factor = bds::detail::Round(bds::detail::Round(std::fabs(static_cast<float>(v.y)) * 0.2f) + 0.4f);
                v.x = F(static_cast<float>(v.x) * factor);
                v.z = F(static_cast<float>(v.z) * factor);
            }
        }
        s.vx = v.x; s.vy = v.y; s.vz = v.z;
        s.jumpDelay = bds::TickJumpDelay(s.jumpDelay);
    }

    static void UpdateBlockContacts(State& s, const Input& in, const Attributes& attr, const World& world, bool wasInWater) {
        const bool crouched = (in.sneak || in.forceCrouch) && !in.flying;
        {
            const Box inside = BoundingBox(s, crouched);
            bool web = false, powderSnow = false, berryBush = false;
            std::vector<bds::BlockPosition> honey;
            bds::CollectInsideCells(CollisionBox(inside), 0.0f, true,
                                   [&](const bds::CollisionBox& region) { return world.InsideRegionReady(region); },
                                   [&](int32_t x, int32_t y, int32_t z) {
                const auto shape = world.Query(x, y, z);
                web |= shape.web;
                powderSnow |= shape.powderSnow;
                berryBush |= shape.berryBush;
                if (shape.bubbleColumn && !in.flying) {
                    const auto above = world.Query(x, y + 1, z);
                    const bool surface = above.isAir && !above.fluid && above.Empty();
                    s.vy = bds::BubbleColumnVelocity(static_cast<float>(s.vy), shape.bubbleColumn < 0,
                                                     s.fallDistance, surface);
                }
                if (shape.honey)
                    honey.push_back({x, y, z});
            });
            if (!wasInWater && !honey.empty()) {
                const auto velocity = bds::HoneyContacts(
                    {static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z)},
                    {static_cast<float>(s.vx), static_cast<float>(s.vy), static_cast<float>(s.vz)},
                    s.mobSize ? (*s.mobSize)[0] : static_cast<float>(kWidth), honey, s.fallDistance);
                s.vx = velocity.x;
                s.vy = velocity.y;
                s.vz = velocity.z;
            }
            if (web || powderSnow || berryBush) {
                bds::Float3 multiplier{s.stuckX, s.stuckY, s.stuckZ};
                std::optional<bool> firstApplication;
                if (web)
                    bds::AccumulateBlockSlowdown(multiplier, {0.25f, 0.05f, 0.25f}, firstApplication);
                if (powderSnow)
                    bds::AccumulateBlockSlowdown(multiplier, {0.9f, 1.5f, 0.9f}, firstApplication);
                if (berryBush)
                    bds::AccumulateBlockSlowdown(multiplier, {0.8f, 0.75f, 0.8f}, firstApplication);
                bds::ApplyPlayerSlowdownImmunity(multiplier, firstApplication, in.flying, in.creativeFlight);
                if (firstApplication && attr.weaving && web && !powderSnow && !berryBush)
                    multiplier = {0.5f, 0.25f, 0.5f};
                if (firstApplication)
                    bds::AdjustSlowdownFallDistance(*firstApplication, s.fallDistance);
                s.stuckX = multiplier.x;
                s.stuckY = multiplier.y;
                s.stuckZ = multiplier.z;
            }
        }
    }

    // StartSpinAttackActionSystem 0x14B3367C0: push along the unit look by ((level + 1) * 0.25) * 3, before the move
    static void StartSpinAttack(State& s, const Input& in, bool headInWater) {
        const auto mul = [](float a, float b) { return bds::detail::Round(a * b); };
        const auto add = [](float a, float b) { return bds::detail::Round(a + b); };
        const auto div = [](float a, float b) { return bds::detail::Round(a / b); };
        const auto sine = [](float coordinate) { return bds::CurrentSineEntry(bds::SineIndex(coordinate)); };
        const float yawCoordinate = mul(mul(s.yaw, 0.017453292f), 10430.378f);
        const float pitchCoordinate = mul(mul(0.017453292f, in.pitch), 10430.378f);
        const float pitchCos = sine(add(pitchCoordinate, 16384.0f));
        const float pitchSin = sine(pitchCoordinate);
        const float lookX = mul(-sine(yawCoordinate), pitchCos);
        const float lookZ = mul(pitchCos, sine(add(16384.0f, yawCoordinate)));
        const float length = std::sqrt(add(mul(lookZ, lookZ), add(mul(pitchSin, pitchSin), mul(lookX, lookX))));
        // a look shorter than 1.0e-4 pushes nowhere
        const bool tiny = length < 1.0e-4f;
        const float unitX = tiny ? 0.0f : div(lookX, length);
        const float unitY = tiny ? 0.0f : div(-pitchSin, length);
        const float unitZ = tiny ? 0.0f : div(lookZ, length);
        const float force = mul(mul(add(static_cast<float>(in.riptideLevel), 1.0f), 0.25f), 3.0f);
        float pushY = mul(unitY, force);
        // launched from the ground: +0.08 cancels this tick's gravity; in water with the head out, the water drag
        if (s.onGround)
            pushY = s.inWater && !headInWater ? mul(div(pushY, 0.8f), 0.98f) : add(pushY, 0.08f);
        s.vx = add(static_cast<float>(s.vx), mul(unitX, force));
        s.vy = add(static_cast<float>(s.vy), pushY);
        s.vz = add(mul(force, unitZ), static_cast<float>(s.vz));
        s.spinAttack = true;
        s.spinTicks = 0;
    }

    // PostDamageNearbyMobsSystem 0x14B33CC90: each mob hit scales the velocity by -0.2 and ends the spin, so does a
    // wall; otherwise 20 ticks, or landing after 5
    static void EndSpinAttack(State& s, const Input& in) {
        if (!s.spinAttack)
            return;
        // SpinAttackFetchNearbyMobsSystem 0x14B316910: mobs touching the box grown by 1 on every side
        const auto grow = [](double v, float d) { return static_cast<double>(static_cast<float>(v) + d); };
        const Box body = BoundingBox(s, false);
        const Box reach{grow(body.x0, -1.0f), grow(body.y0, -1.0f), grow(body.z0, -1.0f),
                        grow(body.x1, 1.0f), grow(body.y1, 1.0f), grow(body.z1, 1.0f)};
        int hits = 0;
        for (const Box& mob : in.mobBoxes)
            if (reach.Intersects(mob))
                ++hits;
        if (hits > 0) {
            for (int i = 0; i < hits; ++i) {
                s.vx = static_cast<float>(s.vx) * -0.2f;
                s.vy = static_cast<float>(s.vy) * -0.2f;
                s.vz = static_cast<float>(s.vz) * -0.2f;
            }
            s.spinAttack = false;
            s.spinTicks = 0;
        } else if (s.horizontalCollision) {
            s.spinAttack = false;
            s.spinTicks = 0;
        }
        const int elapsed = s.spinTicks++;
        if (elapsed > 18 || (elapsed >= 5 && s.onGround)) {
            s.spinAttack = false;
            s.spinTicks = 0;
        }
    }

    void Step(State& s, const Input& in, const Attributes& attr, const World& world) {
        for (const auto& effect : in.movementEffects)
            bds::ApplyMovementEffect(s.movementEffects, effect);
        if (in.riptideLevel > 0)
            StartSpinAttack(s, in, in.headInWater);
        StepMovement(s, in, attr, world);
        EndSpinAttack(s, in);
        const auto climber = bds::SenseScaffolding(CollisionBox(BoundingBox(s, in.sneak)),
            [&](int32_t x, int32_t y, int32_t z) { return world.ClimberCell(x, y, z); }, in.canStandOnPowderSnow);
        if (!climber.overPowderSnow && (climber.inside || climber.over || climber.ascendable || climber.descendable))
            s.fallDistance = 0;
        UpdateBlockContacts(s, in, attr, world, s.inWater);
        bds::TickMovementEffects(s.movementEffects);
        // CurrentSwimAmountSystem 0x1484D5990: the crawling flag drives the amount too
        s.swimAmount = (in.swimming || s.crawling) ? std::fmin(s.swimAmount + 0.1f, 1.0f)
                                                  : std::fmax(s.swimAmount - 0.1f, 0.0f);
        s.previousRotation = {in.pitch, s.yaw};
        s.hasTeleported = false;
    }

    BoxMove MoveBox(const World& world, const Box& start, float dx, float dy, float dz, bool wasOnGround,
                    bds::RecoveryState& recovery) {
        std::vector<Box> scratch;
        bool oneShotStep = false;
        const Vec3 request{dx, dy, dz};
        const Motion m = ResolveMotion(world, start, request, request, wasOnGround, scratch,
                                       bds::RecoveryLimits(recovery), 0.0f, false, oneShotStep);
        bds::UpdateRecovery(recovery, m.penetration >= 1.0e-6f, false);
        bds::MovementFlags flags;
        flags.onGround = wasOnGround;
        const auto result = bds::FinalizeMove(
            CollisionBox(m.box), 0.0f, m.requested,
            {static_cast<float>(m.resolved.x), static_cast<float>(m.resolved.y), static_cast<float>(m.resolved.z)},
            {dx, dy, dz}, flags, false, m.preserveHorizontalMotion);
        BoxMove out;
        out.x = result.position.x;
        out.y = result.position.y;
        out.z = result.position.z;
        out.vx = result.velocity.x;
        out.vy = result.velocity.y;
        out.vz = result.velocity.z;
        // ResetVerticalVelocitySystem 0x147b8d050: every vertical collision tag
        if (result.flags.verticalCollision)
            out.vy = 0.0f;
        out.onGround = result.flags.onGround;
        out.horizontalCollision = result.flags.horizontalCollision;
        out.verticalCollision = result.flags.verticalCollision;
        return out;
    }
}
