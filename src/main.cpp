// Turbo Loop: a small 3D arcade racer. C++17 + raylib. Everything is procedural (no asset files).
//   turboloop          play
//   turboloop --sim    no window: the AI drives all four cars through a race and prints the results
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr int kLaps = 3;
constexpr int kCars = 4;
constexpr float kRoadHalf = 9.0f;   // half the road's width
constexpr float kWallDist = 14.0f;  // the barrier, measured from the centre line
constexpr float kMaxSpeed = 58.0f;  // m/s (about 209 km/h)
constexpr float kScale = 1.4f;
constexpr int kSteps = 45;          // track samples between two control points

// ---------------------------------------------------------------- small vector helpers
inline Vector3 add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 mul(Vector3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot3(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

// ---------------------------------------------------------------- the track
// Directions: yaw 0 faces +Z, and a positive yaw turns left. forward = (sin yaw, 0, cos yaw), left = (cos yaw, 0, -sin yaw).
struct TrackPt {
    Vector3 p;     // centre line
    Vector3 t;     // unit tangent
    Vector3 n;     // unit normal, pointing left
    float curv;    // how much the road turns over the next stretch (radians)
    float curvAhead;  // the largest of those over the next ~60 m
};
std::vector<TrackPt> track;
int N = 0;
float avgSpacing = 3.0f;

Vector3 catmull(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float t) {
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float p0, float p1, float p2, float p3) {
        return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
    };
    return {f(a.x, b.x, c.x, d.x), 0, f(a.z, b.z, c.z, d.z)};
}

void buildTrack() {
    static const float cp[][2] = {{0, 0},      {120, 0},    {240, 40},   {320, 140}, {300, 260}, {200, 320}, {80, 300},
                                  {20, 380},   {-80, 440},  {-200, 400}, {-240, 300}, {-180, 200}, {-220, 100}, {-140, 20}};
    const int M = (int)(sizeof(cp) / sizeof(cp[0]));
    std::vector<Vector3> pts;
    for (int i = 0; i < M; i++) {
        auto P = [&](int k) {
            const int j = ((i + k) % M + M) % M;
            return Vector3{cp[j][0] * kScale, 0, cp[j][1] * kScale};
        };
        for (int s = 0; s < kSteps; s++) pts.push_back(catmull(P(-1), P(0), P(1), P(2), (float)s / kSteps));
    }
    N = (int)pts.size();
    track.assign(N, TrackPt{});
    float length = 0;
    for (int i = 0; i < N; i++) {
        const Vector3 next = pts[(i + 1) % N], prev = pts[(i - 1 + N) % N];
        Vector3 t = sub(next, prev);
        const float l = sqrtf(t.x * t.x + t.z * t.z);
        t = {t.x / l, 0, t.z / l};
        track[i].p = pts[i];
        track[i].t = t;
        track[i].n = {t.z, 0, -t.x};
        const Vector3 d = sub(next, pts[i]);
        length += sqrtf(d.x * d.x + d.z * d.z);
    }
    avgSpacing = length / N;
    for (int i = 0; i < N; i++) {
        const float c = dot3(track[i].t, track[(i + 10) % N].t);
        track[i].curv = acosf(clampf(c, -1.0f, 1.0f));
    }
    for (int i = 0; i < N; i++) {
        float m = 0;
        for (int k = 0; k < 18; k++) m = fmaxf(m, track[(i + k) % N].curv);
        track[i].curvAhead = m;
    }
}

// ---------------------------------------------------------------- cars
struct Car {
    Vector3 pos{}, vel{};
    float yaw = 0, speed = 0, steer = 0, lat = 0;
    int idx = 0, crossings = 0;
    bool player = false, finished = false, braking = false;
    float finishTime = 0, lapStart = 0, lastLap = 0, bestLap = 1e9f;
    float skill = 1, lane = 0;
    int wallHits = 0;
    Color color{};
    const char* name = "";
};

enum class Phase { Countdown, Racing, Finished };
std::vector<Car> cars;
Phase phase = Phase::Countdown;
float raceTime = 0, countdown = 3.5f;
bool paused = false;
bool simMode = false;
float camYaw = 0;

void resetRace() {
    cars.assign(kCars, Car{});
    static const char* names[kCars] = {"Rex", "Mika", "Ozzy", "YOU"};
    static const Color colors[kCars] = {{60, 120, 230, 255}, {240, 190, 40, 255}, {60, 190, 90, 255}, {230, 50, 45, 255}};
    static const float skills[kCars] = {0.97f, 0.93f, 0.89f, 1.0f};
    static const float lanes[kCars] = {-2.0f, 2.0f, 0.0f, 0.0f};
    static const int slotOf[kCars] = {0, 1, 3, 2};  // the player starts third
    for (int i = 0; i < kCars; i++) {
        Car& c = cars[i];
        const int slot = slotOf[i];
        const int row = slot / 2;
        const float side = (slot % 2 == 0) ? 3.4f : -3.4f;
        const int idx = ((N - 6 - row * 4) % N + N) % N;
        c.idx = idx;
        c.pos = add(track[idx].p, mul(track[idx].n, side));
        c.yaw = atan2f(track[idx].t.x, track[idx].t.z);
        c.lat = side;
        c.player = (i == kCars - 1);
        c.name = names[i];
        c.color = colors[i];
        c.skill = skills[i];
        c.lane = lanes[i];
    }
    phase = Phase::Countdown;
    raceTime = 0;
    countdown = 3.5f;
    camYaw = cars[kCars - 1].yaw;
}

float totalProgress(const Car& c) { return (float)c.crossings * N + c.idx; }

std::vector<int> standings() {
    std::vector<int> order(kCars);
    for (int i = 0; i < kCars; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [](int a, int b) {
        const Car &x = cars[a], &y = cars[b];
        if (x.finished != y.finished) return x.finished;
        if (x.finished) return x.finishTime < y.finishTime;
        return totalProgress(x) > totalProgress(y);
    });
    return order;
}

void onCross(Car& c, int dir) {
    if (dir > 0) {
        c.crossings++;
        if (c.crossings >= 2) {
            c.lastLap = raceTime - c.lapStart;
            c.bestLap = fminf(c.bestLap, c.lastLap);
        }
        c.lapStart = raceTime;
        if (c.crossings == kLaps + 1 && !c.finished) {
            c.finished = true;
            c.finishTime = raceTime;
            if (c.player && phase == Phase::Racing) phase = Phase::Finished;
        }
    } else {
        c.crossings--;
    }
}

// Where the car is on the track, and how far from the centre line.
void locate(Car& c) {
    int best = c.idx;
    float bd = 1e18f;
    for (int k = -25; k <= 25; k++) {
        const int j = ((c.idx + k) % N + N) % N;
        const float dx = c.pos.x - track[j].p.x, dz = c.pos.z - track[j].p.z;
        const float d = dx * dx + dz * dz;
        if (d < bd) {
            bd = d;
            best = j;
        }
    }
    const int old = c.idx;
    c.idx = best;
    if (old > N * 3 / 4 && best < N / 4) onCross(c, +1);
    else if (old < N / 4 && best > N * 3 / 4) onCross(c, -1);
    c.lat = dot3(sub(c.pos, track[best].p), track[best].n);
}

// Arcade car physics: forward speed and sideways slip are handled separately, and grip pulls the slip away.
void stepCar(Car& c, float throttle, float brake, float steer, bool handbrake, float dt) {
    Vector3 fwd = {sinf(c.yaw), 0, cosf(c.yaw)};
    Vector3 left = {cosf(c.yaw), 0, -sinf(c.yaw)};
    float vf = dot3(c.vel, fwd), vl = dot3(c.vel, left);
    const bool off = fabsf(c.lat) > kRoadHalf + 1.2f;
    const float maxs = kMaxSpeed * (off ? 0.5f : 1.0f);

    if (throttle > 0 && vf < maxs) vf += throttle * 24.0f * clampf(1.0f - vf / maxs, 0.0f, 1.0f) * dt + throttle * 3.0f * dt;
    c.braking = brake > 0 && vf > 0.5f;
    if (brake > 0) {
        if (vf > 0.5f) vf = fmaxf(0.0f, vf - 46.0f * brake * dt);
        else vf = fmaxf(-10.0f, vf - 12.0f * brake * dt);
    }
    vf -= vf * 0.07f * dt;                         // air
    if (off) vf -= vf * 1.1f * dt;                 // grass
    if (throttle <= 0 && brake <= 0) vf -= clampf(vf, -1.0f, 1.0f) * 3.0f * dt;  // rolling
    if (handbrake) vf -= vf * 0.35f * dt;

    float grip = handbrake ? 1.3f : 9.0f;
    if (off) grip *= 0.6f;
    vl *= expf(-grip * dt);

    c.steer += (steer - c.steer) * fminf(1.0f, dt * 9.0f);
    const float sf = clampf(fabsf(vf) / 8.0f, 0.0f, 1.0f) * (1.0f - 0.35f * clampf(fabsf(vf) / kMaxSpeed, 0.0f, 1.0f));
    c.yaw = wrapAngle(c.yaw + c.steer * 2.1f * sf * (vf >= 0 ? 1.0f : -1.0f) * (handbrake ? 1.5f : 1.0f) * dt);

    fwd = {sinf(c.yaw), 0, cosf(c.yaw)};
    left = {cosf(c.yaw), 0, -sinf(c.yaw)};
    c.vel = add(mul(fwd, vf), mul(left, vl));
    c.pos = add(c.pos, mul(c.vel, dt));
    c.speed = vf;
}

void wallCollide(Car& c) {
    const float a = fabsf(c.lat);
    if (a <= kWallDist) return;
    const float s = c.lat > 0 ? 1.0f : -1.0f;
    const Vector3 n = track[c.idx].n;
    c.pos = sub(c.pos, mul(n, c.lat - s * kWallDist));
    const float vn = dot3(c.vel, n);
    if (vn * s > 0) c.vel = sub(c.vel, mul(n, vn * 1.25f));
    c.vel = mul(c.vel, 0.9f);
    c.lat = s * kWallDist;
    c.wallHits++;
}

void carVsCar() {
    for (int i = 0; i < kCars; i++)
        for (int j = i + 1; j < kCars; j++) {
            Vector3 d = sub(cars[j].pos, cars[i].pos);
            d.y = 0;
            const float dist = sqrtf(d.x * d.x + d.z * d.z);
            const float minDist = 2.3f;
            if (dist >= minDist || dist < 1e-4f) continue;
            const Vector3 n = mul(d, 1.0f / dist);
            const float push = (minDist - dist) * 0.5f;
            cars[i].pos = sub(cars[i].pos, mul(n, push));
            cars[j].pos = add(cars[j].pos, mul(n, push));
            const float rel = dot3(sub(cars[j].vel, cars[i].vel), n);
            if (rel < 0) {
                cars[i].vel = add(cars[i].vel, mul(n, rel * 0.5f));
                cars[j].vel = sub(cars[j].vel, mul(n, rel * 0.5f));
            }
        }
}

// ---------------------------------------------------------------- driving
void aiControl(const Car& c, float& thr, float& brk, float& steer) {
    const float look = 10.0f + fmaxf(c.speed, 0.0f) * 0.28f;
    const int la = std::max(3, (int)(look / avgSpacing));
    const int t = (c.idx + la) % N;
    const Vector3 target = add(track[t].p, mul(track[t].n, c.lane));
    const Vector3 to = sub(target, c.pos);
    const float desired = atan2f(to.x, to.z);
    steer = clampf(wrapAngle(desired - c.yaw) * 2.4f, -1.0f, 1.0f);
    const float ts = kMaxSpeed * c.skill * (1.0f - clampf(track[c.idx].curvAhead * 1.5f, 0.0f, 0.62f));
    thr = c.speed < ts ? 1.0f : 0.0f;
    brk = c.speed > ts + 3.0f ? 1.0f : 0.0f;
}

void playerInput(float& thr, float& brk, float& steer, bool& hb) {
    thr = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? 1.0f : 0.0f;
    brk = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1.0f : 0.0f;
    steer = ((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0f : 0.0f) - ((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0f : 0.0f);
    hb = IsKeyDown(KEY_SPACE);
    if (IsGamepadAvailable(0)) {
        const float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        if (fabsf(ax) > 0.12f) steer = -ax;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) thr = 1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) brk = 1.0f;
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) hb = true;
    }
}

void updateRace(float dt) {
    if (phase == Phase::Countdown) {
        countdown -= dt;
        if (countdown <= 0) {
            phase = Phase::Racing;
            raceTime = 0;
        }
        return;
    }
    raceTime += dt;
    for (Car& c : cars) {
        float thr = 0, brk = 0, st = 0;
        bool hb = false;
        if (c.finished) {  // coast to a stop, still steering along the road
            aiControl(c, thr, brk, st);
            thr = 0;
            brk = c.speed > 1.0f ? 0.4f : 0.0f;  // slow down, but never reverse
        } else if (c.player && !simMode) {
            playerInput(thr, brk, st, hb);
        } else {
            aiControl(c, thr, brk, st);
        }
        stepCar(c, thr, brk, st, hb, dt);
    }
    carVsCar();
    for (Car& c : cars) {
        locate(c);
        wallCollide(c);
    }
}

// ---------------------------------------------------------------- drawing
struct Tree {
    Vector3 p;
    float h;
};
std::vector<Tree> trees;

void buildScenery() {
    SetRandomSeed(7);
    trees.clear();
    for (int i = 0; i < N; i += 5) {
        for (int side = -1; side <= 1; side += 2) {
            const float off = kWallDist + 6.0f + (float)GetRandomValue(0, 450) / 10.0f;
            const Vector3 p = add(track[i].p, mul(track[i].n, off * side));
            bool ok = true;
            for (int j = 0; j < N && ok; j += 2) {
                const float dx = p.x - track[j].p.x, dz = p.z - track[j].p.z;
                if (dx * dx + dz * dz < (kWallDist + 5.0f) * (kWallDist + 5.0f)) ok = false;
            }
            if (ok) trees.push_back({p, 5.0f + (float)GetRandomValue(0, 70) / 10.0f});
        }
    }
}

Vector3 edgePt(const TrackPt& t, float off, float y) { return {t.p.x + t.n.x * off, y, t.p.z + t.n.z * off}; }

void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

void drawTrack(Vector3 camPos) {
    rlDrawRenderBatchActive();  // raylib batches draws: flush what's queued, so the culling change below only affects the track
    rlDisableBackfaceCulling();
    for (int i = 0; i < N; i++) {
        const int j = (i + 1) % N;
        const float dx = track[i].p.x - camPos.x, dz = track[i].p.z - camPos.z;
        if (dx * dx + dz * dz > 450.0f * 450.0f) continue;
        const TrackPt &a = track[i], &b = track[j];
        const bool alt = (i / 6) % 2 == 0;
        quad(edgePt(a, kRoadHalf, 0), edgePt(b, kRoadHalf, 0), edgePt(b, -kRoadHalf, 0), edgePt(a, -kRoadHalf, 0),
             alt ? Color{62, 64, 70, 255} : Color{56, 58, 64, 255});
        const Color curb = ((i / 3) % 2 == 0) ? Color{215, 40, 40, 255} : Color{240, 240, 240, 255};
        for (int s = -1; s <= 1; s += 2)
            quad(edgePt(a, s * kRoadHalf, 0.01f), edgePt(b, s * kRoadHalf, 0.01f), edgePt(b, s * (kRoadHalf + 1.2f), 0.01f),
                 edgePt(a, s * (kRoadHalf + 1.2f), 0.01f), curb);
        for (int s = -1; s <= 1; s += 2) {
            quad(edgePt(a, s * (kRoadHalf + 1.2f), 0.005f), edgePt(b, s * (kRoadHalf + 1.2f), 0.005f), edgePt(b, s * kWallDist, 0.005f),
                 edgePt(a, s * kWallDist, 0.005f), Color{112, 100, 72, 255});
            const Color wall = ((i / 4) % 2 == 0) ? Color{205, 205, 210, 255} : Color{70, 72, 84, 255};
            quad(edgePt(a, s * kWallDist, 0), edgePt(b, s * kWallDist, 0), edgePt(b, s * kWallDist, 1.0f), edgePt(a, s * kWallDist, 1.0f), wall);
        }
        if (i % 8 < 4) quad(edgePt(a, 0.15f, 0.02f), edgePt(b, 0.15f, 0.02f), edgePt(b, -0.15f, 0.02f), edgePt(a, -0.15f, 0.02f), Color{235, 235, 235, 255});
        if (i < 2) {  // start / finish line
            const int cols = 10;
            const float w = 2 * kRoadHalf / cols;
            for (int k = 0; k < cols; k++) {
                const float o0 = -kRoadHalf + k * w, o1 = o0 + w;
                quad(edgePt(a, o0, 0.03f), edgePt(b, o0, 0.03f), edgePt(b, o1, 0.03f), edgePt(a, o1, 0.03f), ((k + i) % 2 == 0) ? WHITE : BLACK);
            }
        }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void drawScenery(Vector3 camPos) {
    for (const Tree& t : trees) {
        const float dx = t.p.x - camPos.x, dz = t.p.z - camPos.z;
        if (dx * dx + dz * dz > 380.0f * 380.0f) continue;
        DrawCylinder(t.p, 0.45f, 0.45f, t.h * 0.35f, 6, Color{95, 66, 40, 255});
        DrawCylinder({t.p.x, t.p.y + t.h * 0.3f, t.p.z}, 3.0f + t.h * 0.1f, 0.0f, t.h, 7, Color{36, 118, 58, 255});
    }
}

void drawMountains() {
    Vector3 centre{0, 0, 0};
    for (const TrackPt& t : track) centre = add(centre, t.p);
    centre = mul(centre, 1.0f / N);
    for (int i = 0; i < 18; i++) {
        const float a = i * (2 * PI / 18) + 0.2f * sinf(i * 1.7f);
        const float r = 1300.0f + 120.0f * sinf(i * 2.3f);
        const float h = 230.0f + 90.0f * (0.5f + 0.5f * sinf(i * 3.1f));
        DrawCylinder({centre.x + cosf(a) * r, -0.5f, centre.z + sinf(a) * r}, 0.0f + 12.0f, 330.0f, h, 9, Color{74, 98, 120, 255});
    }
}

Color shade(Color c, float k) { return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255}; }

void drawCar(const Car& c) {
    rlPushMatrix();
    rlTranslatef(c.pos.x, 0, c.pos.z);
    rlRotatef(c.yaw * RAD2DEG, 0, 1, 0);
    DrawCube({0, 0.03f, 0}, 2.2f, 0.02f, 4.5f, Fade(BLACK, 0.35f));
    DrawCube({0, 0.55f, 0}, 1.9f, 0.55f, 4.2f, c.color);
    DrawCube({0, 1.02f, -0.3f}, 1.5f, 0.48f, 2.0f, Color{28, 34, 46, 255});
    DrawCube({0, 0.86f, 1.25f}, 0.5f, 0.04f, 1.4f, Color{245, 245, 245, 255});
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) DrawCube({sx * 1.02f, 0.4f, sz * 1.4f}, 0.34f, 0.8f, 0.8f, Color{20, 20, 22, 255});
    DrawCube({0, 1.2f, -2.0f}, 1.9f, 0.08f, 0.5f, shade(c.color, 0.7f));
    DrawCube({-0.7f, 0.95f, -1.95f}, 0.08f, 0.5f, 0.08f, shade(c.color, 0.6f));
    DrawCube({0.7f, 0.95f, -1.95f}, 0.08f, 0.5f, 0.08f, shade(c.color, 0.6f));
    const Color tail = c.braking ? Color{255, 40, 40, 255} : Color{110, 10, 10, 255};
    DrawCube({-0.65f, 0.62f, -2.12f}, 0.4f, 0.15f, 0.05f, tail);
    DrawCube({0.65f, 0.62f, -2.12f}, 0.4f, 0.15f, 0.05f, tail);
    DrawCube({-0.65f, 0.62f, 2.12f}, 0.4f, 0.15f, 0.05f, Color{255, 245, 170, 255});
    DrawCube({0.65f, 0.62f, 2.12f}, 0.4f, 0.15f, 0.05f, Color{255, 245, 170, 255});
    rlPopMatrix();
}

std::string fmtTime(float t) {
    if (t <= 0 || t > 5999) return "--:--.-";
    char b[32];
    snprintf(b, sizeof b, "%d:%04.1f", (int)(t / 60), fmodf(t, 60.0f));
    return b;
}

const char* ordinal(int n) {
    static const char* s[] = {"0th", "1st", "2nd", "3rd", "4th"};
    return n >= 0 && n <= 4 ? s[n] : "?";
}

void text(const std::string& s, int x, int y, int size, Color col) {
    DrawText(s.c_str(), x + 2, y + 2, size, Fade(BLACK, 0.6f));
    DrawText(s.c_str(), x, y, size, col);
}

void textCentred(const std::string& s, int cx, int y, int size, Color col) { text(s, cx - MeasureText(s.c_str(), size) / 2, y, size, col); }

void drawMinimap(int sw, float ui) {
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const TrackPt& t : track) {
        minX = fminf(minX, t.p.x);
        maxX = fmaxf(maxX, t.p.x);
        minZ = fminf(minZ, t.p.z);
        maxZ = fmaxf(maxZ, t.p.z);
    }
    const float box = 190 * ui, pad = 14 * ui;
    const float span = fmaxf(maxX - minX, maxZ - minZ);
    const float x0 = sw - box - pad, y0 = pad;
    DrawRectangle((int)x0 - 6, (int)y0 - 6, (int)box + 12, (int)box + 12, Fade(BLACK, 0.45f));
    auto map = [&](Vector3 p) { return Vector2{x0 + box - (p.x - minX) / span * (box - 8) - 4, y0 + box - (p.z - minZ) / span * (box - 8) - 4}; };
    for (int i = 0; i < N; i += 3) DrawLineEx(map(track[i].p), map(track[(i + 3) % N].p), 3.0f * ui, Color{230, 230, 235, 255});
    for (const Car& c : cars) DrawCircleV(map(c.pos), (c.player ? 5.5f : 4.0f) * ui, c.color);
}

void drawHud(int sw, int sh) {
    const float ui = sh / 720.0f;
    const Car& me = cars[kCars - 1];
    const std::vector<int> order = standings();
    int place = 1;
    for (int i = 0; i < kCars; i++)
        if (order[i] == kCars - 1) place = i + 1;

    text(std::string(ordinal(place)) + " / " + std::to_string(kCars), (int)(24 * ui), (int)(20 * ui), (int)(54 * ui), Color{255, 220, 70, 255});
    text("LAP " + std::to_string((int)clampf((float)me.crossings, 1.0f, (float)kLaps)) + " / " + std::to_string(kLaps), (int)(24 * ui), (int)(80 * ui),
         (int)(34 * ui), WHITE);
    text("TIME  " + fmtTime(phase == Phase::Countdown ? 0.0f : raceTime), (int)(24 * ui), (int)(122 * ui), (int)(26 * ui), WHITE);
    if (me.lastLap > 0) text("LAST  " + fmtTime(me.lastLap), (int)(24 * ui), (int)(152 * ui), (int)(26 * ui), WHITE);
    if (me.bestLap < 1e8f) text("BEST  " + fmtTime(me.bestLap), (int)(24 * ui), (int)(182 * ui), (int)(26 * ui), Color{120, 255, 150, 255});

    const int kph = (int)(fabsf(me.speed) * 3.6f);
    textCentred(std::to_string(kph), sw - (int)(130 * ui), sh - (int)(150 * ui), (int)(84 * ui), WHITE);
    textCentred("KM/H", sw - (int)(130 * ui), sh - (int)(66 * ui), (int)(24 * ui), Color{200, 200, 210, 255});
    const float bw = 260 * ui, bx = sw - bw - 30 * ui, by = sh - 36 * ui;
    DrawRectangle((int)bx, (int)by, (int)bw, (int)(12 * ui), Fade(BLACK, 0.5f));
    DrawRectangle((int)bx, (int)by, (int)(bw * clampf(fabsf(me.speed) / kMaxSpeed, 0.0f, 1.0f)), (int)(12 * ui), Color{255, 140, 40, 255});

    drawMinimap(sw, ui);

    if (phase == Phase::Countdown) {
        const int n = (int)ceilf(countdown - 0.5f);
        if (n >= 1) textCentred(std::to_string(std::min(n, 3)), sw / 2, (int)(sh * 0.28f), (int)(180 * ui), Color{255, 220, 70, 255});
    } else if (raceTime < 1.2f) {
        textCentred("GO!", sw / 2, (int)(sh * 0.28f), (int)(180 * ui), Color{120, 255, 150, 255});
    }
    if (paused) textCentred("PAUSED", sw / 2, (int)(sh * 0.4f), (int)(90 * ui), WHITE);

    if (phase == Phase::Finished) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
        textCentred("RACE COMPLETE", sw / 2, (int)(sh * 0.16f), (int)(76 * ui), Color{255, 220, 70, 255});
        int y = (int)(sh * 0.34f);
        for (int i = 0; i < kCars; i++) {
            const Car& c = cars[order[i]];
            std::string line = std::to_string(i + 1) + ".  " + c.name;
            while (line.size() < 14) line += ' ';
            line += c.finished ? fmtTime(c.finishTime) : "racing...";
            textCentred(line, sw / 2, y, (int)(40 * ui), c.player ? Color{255, 220, 70, 255} : WHITE);
            y += (int)(56 * ui);
        }
        if (me.bestLap < 1e8f) textCentred("Your best lap  " + fmtTime(me.bestLap), sw / 2, y + (int)(10 * ui), (int)(30 * ui), Color{120, 255, 150, 255});
        textCentred("Press R or Enter to race again", sw / 2, (int)(sh * 0.88f), (int)(30 * ui), WHITE);
    }
}

// ---------------------------------------------------------------- the simulation test (no window)
int runSim() {
    simMode = true;
    resetRace();
    const float dt = 1.0f / 60.0f;
    float t = 0;
    while (t < 400.0f) {
        updateRace(dt);
        t += dt;
        bool all = true;
        for (const Car& c : cars) all = all && c.finished;
        if (all) break;
    }
    printf("track: %d samples, %.0f m per lap\n", N, avgSpacing * N);
    int done = 0;
    for (const Car& c : cars) {
        printf("%-5s finished=%d time=%s best lap=%s wall hits=%d\n", c.name, c.finished ? 1 : 0, fmtTime(c.finishTime).c_str(),
               fmtTime(c.bestLap).c_str(), c.wallHits);
        done += c.finished ? 1 : 0;
    }
    printf(done == kCars ? "OK: all cars finished %d laps\n" : "FAIL: only %d cars finished\n", done == kCars ? kLaps : done);
    return done == kCars ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    buildTrack();
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--sim")) return runSim();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Turbo Loop");
    SetTargetFPS(144);
    buildScenery();
    resetRace();

    Camera3D cam{};
    cam.up = {0, 1, 0};
    cam.projection = CAMERA_PERSPECTIVE;
    cam.fovy = 62;

    while (!WindowShouldClose()) {
        const float dt = fminf(GetFrameTime(), 1.0f / 30.0f);
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_P)) paused = !paused;
        if (IsKeyPressed(KEY_R) || (phase == Phase::Finished && IsKeyPressed(KEY_ENTER)) ||
            (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)))
            resetRace();
        if (!paused) updateRace(dt);

        const Car& me = cars[kCars - 1];
        camYaw += wrapAngle(me.yaw - camYaw) * fminf(1.0f, dt * 5.0f);
        const float sp = clampf(fabsf(me.speed) / kMaxSpeed, 0.0f, 1.0f);
        const Vector3 f = {sinf(camYaw), 0, cosf(camYaw)};
        cam.position = add(sub(me.pos, mul(f, 8.5f + sp * 2.5f)), {0, 3.4f + sp * 0.6f, 0});
        cam.target = add(add(me.pos, mul(f, 6.0f)), {0, 1.2f, 0});
        cam.fovy = 62.0f + 16.0f * sp * sp;

        BeginDrawing();
        ClearBackground(Color{150, 200, 240, 255});
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        DrawRectangleGradientV(0, 0, sw, (int)(sh * 0.62f), Color{96, 156, 226, 255}, Color{196, 224, 246, 255});
        rlSetClipPlanes(0.3, 4000.0);
        BeginMode3D(cam);
        DrawPlane({cam.position.x, -0.15f, cam.position.z}, {7000, 7000}, Color{62, 140, 66, 255});
        drawMountains();
        drawTrack(cam.position);
        drawScenery(cam.position);
        for (const Car& c : cars) drawCar(c);
        EndMode3D();
        drawHud(sw, sh);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
