// Turbo Loop: a small 3D arcade racer. C++17 + raylib. Everything is procedural except one embedded image (the Face Land texture).
//   turboloop            open the track menu
//   turboloop --track N  start straight on track N (1-5)
//   turboloop --sim      no window: the AI drives every track and prints the results
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "face_png.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr int kLaps = 3;
constexpr int kCars = 4;
constexpr float kRoadHalf = 9.0f;   // half the road's width
constexpr float kWallDist = 14.0f;  // the barrier, measured from the centre line
constexpr float kMaxSpeed = 58.0f;  // m/s (about 209 km/h)
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
inline unsigned hash32(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

// ---------------------------------------------------------------- the five tracks
enum class Theme { Sunny, Beach, Neon, Draft, Face };

struct TrackDef {
    const char* name;
    const char* blurb;
    Theme theme;
    float scale;
    std::vector<Vector2> pts;  // control points (x, z)
};

const std::vector<TrackDef>& defs() {
    static const std::vector<TrackDef> d = {
        {"Sunny Circuit", "The classic. Wide, fast and friendly.", Theme::Sunny, 1.4f,
         {{0, 0}, {120, 0}, {240, 40}, {320, 140}, {300, 260}, {200, 320}, {80, 300}, {20, 380}, {-80, 440}, {-200, 400}, {-240, 300}, {-180, 200}, {-220, 100}, {-140, 20}}},
        {"Palm Bay", "Long seaside sweepers. Mind the boardwalk.", Theme::Beach, 1.4f,
         {{0, 0}, {150, -20}, {300, -20}, {420, 30}, {470, 130}, {420, 230}, {300, 260}, {200, 220}, {120, 280}, {20, 330}, {-80, 300}, {-120, 200}, {-100, 100}, {-60, 40}}},
        {"Neon Strip", "Night laps through a city that never sleeps.", Theme::Neon, 1.3f,
         {{0, 0}, {180, 0}, {260, 60}, {260, 200}, {180, 260}, {80, 260}, {40, 330}, {40, 430}, {-40, 490}, {-180, 490}, {-260, 430}, {-260, 300}, {-180, 240}, {-120, 160}, {-120, 60}, {-60, 0}}},
        {"Prototype Park", "Not finished. On purpose. Some of the road is missing.", Theme::Draft, 1.4f,
         {{0, 0}, {200, 0}, {400, 0}, {450, 100}, {450, 300}, {400, 400}, {200, 400}, {0, 400}, {-50, 300}, {-50, 100}}},
        {"Face Land", "Every single texture is the same face.", Theme::Face, 1.5f,
         {{-282, 147}, {-193, 20}, {-52, -45}, {103, -32}, {230, 57}, {282, 147}, {156, 160}, {90, 94}, {0, 70}, {-90, 94}, {-156, 160}}},
    };
    return d;
}

struct Look {
    Color skyTop, skyBot, ground, sea, road0, road1, curbA, curbB, runoff, wallA, wallB, wallTop, dash, mount;
};
const Look kLooks[5] = {
    // Sunny
    {{96, 156, 226, 255}, {196, 224, 246, 255}, {62, 140, 66, 255}, {0, 0, 0, 0}, {62, 64, 70, 255}, {56, 58, 64, 255}, {215, 40, 40, 255}, {240, 240, 240, 255},
     {112, 100, 72, 255}, {205, 205, 210, 255}, {70, 72, 84, 255}, {0, 0, 0, 0}, {235, 235, 235, 255}, {74, 98, 120, 255}},
    // Beach
    {{40, 150, 230, 255}, {206, 242, 250, 255}, {238, 216, 160, 255}, {30, 160, 205, 255}, {88, 88, 92, 255}, {80, 80, 84, 255}, {250, 250, 250, 255}, {20, 130, 210, 255},
     {214, 190, 130, 255}, {252, 252, 252, 255}, {168, 122, 72, 255}, {0, 0, 0, 0}, {250, 230, 120, 255}, {52, 140, 100, 255}},
    // Neon
    {{6, 4, 20, 255}, {120, 20, 130, 255}, {14, 10, 26, 255}, {0, 0, 0, 0}, {24, 24, 36, 255}, {19, 19, 31, 255}, {255, 40, 200, 255}, {30, 230, 255, 255},
     {30, 24, 48, 255}, {34, 36, 62, 255}, {255, 40, 200, 255}, {30, 230, 255, 255}, {255, 230, 60, 255}, {38, 20, 60, 255}},
    // Draft
    {{170, 178, 190, 255}, {214, 218, 224, 255}, {150, 152, 156, 255}, {0, 0, 0, 0}, {95, 97, 102, 255}, {88, 90, 95, 255}, {255, 140, 0, 255}, {255, 140, 0, 255},
     {140, 142, 146, 255}, {255, 140, 0, 255}, {255, 140, 0, 255}, {0, 0, 0, 0}, {255, 255, 255, 255}, {0, 0, 0, 0}},
    // Face (every colour here is unused: this track is textured with the face only)
    {WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, {0, 0, 0, 0}, WHITE, WHITE},
};

// ---------------------------------------------------------------- the current track
// Directions: yaw 0 faces +Z, and a positive yaw turns left. forward = (sin yaw, 0, cos yaw), left = (cos yaw, 0, -sin yaw).
struct TrackPt {
    Vector3 p;        // centre line
    Vector3 t;        // unit tangent
    Vector3 n;        // unit normal, pointing left
    float curv;       // how much the road turns over the next stretch (radians)
    float curvAhead;  // the largest of those over the next ~60 m
    float s;          // distance along the lap
};
std::vector<TrackPt> track;
int N = 0;
float avgSpacing = 3.0f;
Vector3 trackCentre{};
float trackRadius = 500;
int curId = 0;
Theme theme = Theme::Sunny;
Look look = kLooks[0];
std::vector<std::vector<Vector2>> previews;  // one polyline (x, z) per track, for the menu and minimap

Vector3 catmull(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float t) {
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float p0, float p1, float p2, float p3) {
        return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
    };
    return {f(a.x, b.x, c.x, d.x), 0, f(a.z, b.z, c.z, d.z)};
}

void buildTrack(const TrackDef& def) {
    const int M = (int)def.pts.size();
    std::vector<Vector3> pts;
    for (int i = 0; i < M; i++) {
        auto P = [&](int k) {
            const int j = ((i + k) % M + M) % M;
            return Vector3{def.pts[j].x * def.scale, 0, def.pts[j].y * def.scale};
        };
        for (int s = 0; s < kSteps; s++) pts.push_back(catmull(P(-1), P(0), P(1), P(2), (float)s / kSteps));
    }
    N = (int)pts.size();
    track.assign(N, TrackPt{});
    float length = 0;
    trackCentre = {0, 0, 0};
    for (int i = 0; i < N; i++) {
        const Vector3 next = pts[(i + 1) % N], prev = pts[(i - 1 + N) % N];
        Vector3 t = sub(next, prev);
        const float l = sqrtf(t.x * t.x + t.z * t.z);
        t = {t.x / l, 0, t.z / l};
        track[i].p = pts[i];
        track[i].t = t;
        track[i].n = {t.z, 0, -t.x};
        track[i].s = length;
        const Vector3 d = sub(next, pts[i]);
        length += sqrtf(d.x * d.x + d.z * d.z);
        trackCentre = add(trackCentre, pts[i]);
    }
    trackCentre = mul(trackCentre, 1.0f / N);
    trackRadius = 0;
    for (const TrackPt& t : track) trackRadius = fmaxf(trackRadius, sqrtf(powf(t.p.x - trackCentre.x, 2) + powf(t.p.z - trackCentre.z, 2)));
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

void buildPreviews() {
    previews.clear();
    for (const TrackDef& d : defs()) {
        buildTrack(d);
        std::vector<Vector2> pl;
        for (int i = 0; i < N; i += 3) pl.push_back({track[i].p.x, track[i].p.z});
        previews.push_back(pl);
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
float bestLapByTrack[5] = {1e9f, 1e9f, 1e9f, 1e9f, 1e9f};

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
            if (c.player) bestLapByTrack[curId] = fminf(bestLapByTrack[curId], c.lastLap);
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
    if (off) vf -= vf * 1.1f * dt;                 // off the tarmac
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
    const float look2 = 10.0f + fmaxf(c.speed, 0.0f) * 0.28f;
    const int la = std::max(3, (int)(look2 / avgSpacing));
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

// ---------------------------------------------------------------- textures and scenery
Texture2D texFace{}, texWin{}, texChk{};

Texture2D makeTexture(Image img, bool mipmaps, bool point) {
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    if (mipmaps) GenTextureMipmaps(&t);
    SetTextureFilter(t, point ? TEXTURE_FILTER_POINT : (mipmaps ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR));
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

void loadAssets() {
    texFace = makeTexture(LoadImageFromMemory(".png", kFacePng, (int)kFacePngSize), true, false);
    // Neon Strip: dark wall with a grid of windows. The window colour comes from the tint.
    Image w = GenImageColor(64, 64, Color{16, 16, 26, 255});
    for (int cy = 0; cy < 4; cy++)
        for (int cx = 0; cx < 4; cx++) {
            const unsigned h = hash32((unsigned)(cy * 4 + cx) * 2654435u + 17u);
            const Color c = (h % 10 < 7) ? Color{255, 255, 255, 255} : Color{58, 58, 84, 255};
            ImageDrawRectangle(&w, cx * 16 + 3, cy * 16 + 4, 10, 8, c);
        }
    texWin = makeTexture(w, true, false);
    // Prototype Park: the classic "missing texture" checkerboard.
    Image k = GenImageColor(2, 2, BLACK);
    ImageDrawPixel(&k, 0, 0, Color{255, 0, 255, 255});
    ImageDrawPixel(&k, 1, 1, Color{255, 0, 255, 255});
    texChk = makeTexture(k, false, true);
}

struct Prop {
    int kind = 0;
    Vector3 p{};
    float a = 0, b = 0, c = 0;
    Color col{};
};
std::vector<Prop> props;

bool farFromTrack(Vector3 p, float minDist) {
    for (int j = 0; j < N; j += 2) {
        const float dx = p.x - track[j].p.x, dz = p.z - track[j].p.z;
        if (dx * dx + dz * dz < minDist * minDist) return false;
    }
    return true;
}

void buildScenery() {
    SetRandomSeed(7 + curId);
    props.clear();
    static const Color neons[6] = {{255, 60, 200, 255}, {40, 230, 255, 255}, {255, 220, 60, 255}, {255, 90, 60, 255}, {150, 90, 255, 255}, {60, 255, 140, 255}};
    const int step = (theme == Theme::Neon) ? 3 : (theme == Theme::Draft ? 9 : 5);
    for (int i = 0; i < N; i += step) {
        for (int side = -1; side <= 1; side += 2) {
            Prop pr;
            if (theme == Theme::Neon) {
                const float w = 14.0f + (float)GetRandomValue(0, 26), d = 14.0f + (float)GetRandomValue(0, 26);
                const float off = kWallDist + 34.0f + (float)GetRandomValue(0, 100);
                pr.p = add(track[i].p, mul(track[i].n, off * side));
                if (!farFromTrack(pr.p, kWallDist + 8.0f + fmaxf(w, d) * 0.8f)) continue;
                pr.a = w; pr.b = 18.0f + (float)GetRandomValue(0, 100); pr.c = d;
                pr.col = neons[GetRandomValue(0, 5)];
            } else if (theme == Theme::Draft) {
                const float off = kWallDist + 8.0f + (float)GetRandomValue(0, 60);
                pr.p = add(track[i].p, mul(track[i].n, off * side));
                if (!farFromTrack(pr.p, kWallDist + 8.0f)) continue;
                pr.kind = GetRandomValue(0, 6);
                pr.a = 3.0f + (float)GetRandomValue(0, 60) / 10.0f;
            } else {
                const float off = kWallDist + 6.0f + (float)GetRandomValue(0, 450) / 10.0f;
                pr.p = add(track[i].p, mul(track[i].n, off * side));
                if (!farFromTrack(pr.p, kWallDist + 5.0f)) continue;
                pr.a = 5.0f + (float)GetRandomValue(0, 70) / 10.0f;
            }
            props.push_back(pr);
        }
    }
}

void loadTrack(int id, bool scenery) {
    curId = id;
    const TrackDef& d = defs()[id];
    theme = d.theme;
    look = kLooks[(int)theme];
    buildTrack(d);
    if (scenery) buildScenery();
    resetRace();
}

// ---------------------------------------------------------------- drawing helpers
Vector3 edgePt(const TrackPt& t, float off, float y) { return {t.p.x + t.n.x * off, y, t.p.z + t.n.z * off}; }

void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

// A textured quad. The caller calls rlSetTexture(...) once before a run of these, and rlSetTexture(0) after.
void texQuad(unsigned texId, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector2 ua, Vector2 ub, Vector2 uc, Vector2 ud, Color tint) {
    if (rlCheckRenderBatchLimit(4)) rlSetTexture(texId);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlTexCoord2f(ua.x, ua.y); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(ub.x, ub.y); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(uc.x, uc.y); rlVertex3f(c.x, c.y, c.z);
    rlTexCoord2f(ud.x, ud.y); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
}

// A cube with the texture on every face (raylib 5.5 no longer ships DrawCubeTexture).
void DrawCubeTexture(Texture2D tex, Vector3 pos, float w, float h, float l, Color c) {
    const float x = pos.x, y = pos.y, z = pos.z, hw = w * 0.5f, hh = h * 0.5f, hl = l * 0.5f;
    rlCheckRenderBatchLimit(24);
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    // front (+z)
    rlTexCoord2f(0, 1); rlVertex3f(x - hw, y - hh, z + hl); rlTexCoord2f(1, 1); rlVertex3f(x + hw, y - hh, z + hl);
    rlTexCoord2f(1, 0); rlVertex3f(x + hw, y + hh, z + hl); rlTexCoord2f(0, 0); rlVertex3f(x - hw, y + hh, z + hl);
    // back (-z)
    rlTexCoord2f(1, 1); rlVertex3f(x - hw, y - hh, z - hl); rlTexCoord2f(1, 0); rlVertex3f(x - hw, y + hh, z - hl);
    rlTexCoord2f(0, 0); rlVertex3f(x + hw, y + hh, z - hl); rlTexCoord2f(0, 1); rlVertex3f(x + hw, y - hh, z - hl);
    // top
    rlTexCoord2f(0, 1); rlVertex3f(x - hw, y + hh, z - hl); rlTexCoord2f(0, 0); rlVertex3f(x - hw, y + hh, z + hl);
    rlTexCoord2f(1, 0); rlVertex3f(x + hw, y + hh, z + hl); rlTexCoord2f(1, 1); rlVertex3f(x + hw, y + hh, z - hl);
    // bottom
    rlTexCoord2f(1, 1); rlVertex3f(x - hw, y - hh, z - hl); rlTexCoord2f(0, 1); rlVertex3f(x + hw, y - hh, z - hl);
    rlTexCoord2f(0, 0); rlVertex3f(x + hw, y - hh, z + hl); rlTexCoord2f(1, 0); rlVertex3f(x - hw, y - hh, z + hl);
    // right (+x)
    rlTexCoord2f(1, 1); rlVertex3f(x + hw, y - hh, z - hl); rlTexCoord2f(1, 0); rlVertex3f(x + hw, y + hh, z - hl);
    rlTexCoord2f(0, 0); rlVertex3f(x + hw, y + hh, z + hl); rlTexCoord2f(0, 1); rlVertex3f(x + hw, y - hh, z + hl);
    // left (-x)
    rlTexCoord2f(0, 1); rlVertex3f(x - hw, y - hh, z - hl); rlTexCoord2f(1, 1); rlVertex3f(x - hw, y - hh, z + hl);
    rlTexCoord2f(1, 0); rlVertex3f(x - hw, y + hh, z + hl); rlTexCoord2f(0, 0); rlVertex3f(x - hw, y + hh, z - hl);
    rlEnd();
    rlSetTexture(0);
}

bool faceMode() { return theme == Theme::Face; }

void cubeT(Vector3 p, float w, float h, float l, Color c) {
    if (faceMode()) DrawCubeTexture(texFace, p, w, h, l, WHITE);
    else DrawCube(p, w, h, l, c);
}

// ---------------------------------------------------------------- drawing: the track
void drawTrackStandard(Vector3 camPos) {
    rlDrawRenderBatchActive();  // raylib batches draws: flush what's queued, so the culling change below only affects the track
    rlDisableBackfaceCulling();
    for (int i = 0; i < N; i++) {
        const int j = (i + 1) % N;
        const float dx = track[i].p.x - camPos.x, dz = track[i].p.z - camPos.z;
        if (dx * dx + dz * dz > 450.0f * 450.0f) continue;
        const TrackPt &a = track[i], &b = track[j];
        quad(edgePt(a, kRoadHalf, 0), edgePt(b, kRoadHalf, 0), edgePt(b, -kRoadHalf, 0), edgePt(a, -kRoadHalf, 0), ((i / 6) % 2 == 0) ? look.road0 : look.road1);
        const Color curb = ((i / 3) % 2 == 0) ? look.curbA : look.curbB;
        for (int s = -1; s <= 1; s += 2) {
            quad(edgePt(a, s * kRoadHalf, 0.01f), edgePt(b, s * kRoadHalf, 0.01f), edgePt(b, s * (kRoadHalf + 1.2f), 0.01f), edgePt(a, s * (kRoadHalf + 1.2f), 0.01f), curb);
            quad(edgePt(a, s * (kRoadHalf + 1.2f), 0.005f), edgePt(b, s * (kRoadHalf + 1.2f), 0.005f), edgePt(b, s * kWallDist, 0.005f), edgePt(a, s * kWallDist, 0.005f), look.runoff);
            const Color wall = ((i / 4) % 2 == 0) ? look.wallA : look.wallB;
            quad(edgePt(a, s * kWallDist, 0), edgePt(b, s * kWallDist, 0), edgePt(b, s * kWallDist, 1.0f), edgePt(a, s * kWallDist, 1.0f), wall);
            if (look.wallTop.a)
                quad(edgePt(a, s * (kWallDist - 0.04f), 0.88f), edgePt(b, s * (kWallDist - 0.04f), 0.88f), edgePt(b, s * (kWallDist - 0.04f), 1.0f),
                     edgePt(a, s * (kWallDist - 0.04f), 1.0f), look.wallTop);
        }
        if (i % 8 < 4) quad(edgePt(a, 0.15f, 0.02f), edgePt(b, 0.15f, 0.02f), edgePt(b, -0.15f, 0.02f), edgePt(a, -0.15f, 0.02f), look.dash);
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

// Face Land: every surface is the face. The face's top points the way you drive.
void drawTrackFace(Vector3 camPos) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlSetTexture(texFace.id);
    const Color W = WHITE;
    for (int i = 0; i < N; i++) {
        const int j = (i + 1) % N;
        const float dx = track[i].p.x - camPos.x, dz = track[i].p.z - camPos.z;
        if (dx * dx + dz * dz > 450.0f * 450.0f) continue;
        const TrackPt &a = track[i], &b = track[j];
        const float sa = a.s, sb = (j == 0) ? a.s + avgSpacing : b.s;
        // the road: one big face per 18 m, upright when you drive forward
        const float tile = 2 * kRoadHalf;
        texQuad(texFace.id, edgePt(a, kRoadHalf, 0), edgePt(b, kRoadHalf, 0), edgePt(b, -kRoadHalf, 0), edgePt(a, -kRoadHalf, 0), {0, -sa / tile}, {0, -sb / tile},
                {1, -sb / tile}, {1, -sa / tile}, W);
        for (int s = -1; s <= 1; s += 2) {
            const float ct = 1.2f;  // curbs: little faces
            texQuad(texFace.id, edgePt(a, s * kRoadHalf, 0.01f), edgePt(b, s * kRoadHalf, 0.01f), edgePt(b, s * (kRoadHalf + ct), 0.01f), edgePt(a, s * (kRoadHalf + ct), 0.01f),
                    {0, -sa / ct}, {0, -sb / ct}, {1, -sb / ct}, {1, -sa / ct}, W);
            const float rw = kWallDist - kRoadHalf - 1.2f;  // runoff: medium faces
            texQuad(texFace.id, edgePt(a, s * (kRoadHalf + ct), 0.005f), edgePt(b, s * (kRoadHalf + ct), 0.005f), edgePt(b, s * kWallDist, 0.005f), edgePt(a, s * kWallDist, 0.005f),
                    {0, -sa / rw}, {0, -sb / rw}, {1, -sb / rw}, {1, -sa / rw}, W);
            // walls: 1 m faces
            texQuad(texFace.id, edgePt(a, s * kWallDist, 0), edgePt(b, s * kWallDist, 0), edgePt(b, s * kWallDist, 1.0f), edgePt(a, s * kWallDist, 1.0f), {sa, 1}, {sb, 1}, {sb, 0}, {sa, 0}, W);
        }
        if (i < 2) {  // the start line is a row of faces
            const int cols = 6;
            const float w = 2 * kRoadHalf / cols;
            for (int k = 0; k < cols; k++) {
                const float o0 = -kRoadHalf + k * w, o1 = o0 + w;
                texQuad(texFace.id, edgePt(a, o0, 0.03f), edgePt(b, o0, 0.03f), edgePt(b, o1, 0.03f), edgePt(a, o1, 0.03f), {0, 1}, {0, 0}, {1, 0}, {1, 1}, W);
            }
        }
    }
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

// Prototype Park: some of the road is finished, some is wireframe, some is not there yet.
int blockKind(int i) {
    const unsigned h = hash32((unsigned)(i / 14) * 7919u + 3u) % 10;
    return h < 5 ? 0 : (h < 8 ? 1 : 2);  // 0 solid, 1 wireframe, 2 missing
}

void drawTrackDraft(Vector3 camPos) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int i = 0; i < N; i++) {
        const int j = (i + 1) % N;
        const float dx = track[i].p.x - camPos.x, dz = track[i].p.z - camPos.z;
        if (dx * dx + dz * dz > 450.0f * 450.0f) continue;
        const TrackPt &a = track[i], &b = track[j];
        const int k = blockKind(i);
        const bool railBlock = (hash32((unsigned)(i / 14) * 31u + 11u) % 10) < 4;
        if (k == 0) {
            quad(edgePt(a, kRoadHalf, 0), edgePt(b, kRoadHalf, 0), edgePt(b, -kRoadHalf, 0), edgePt(a, -kRoadHalf, 0), ((i / 6) % 2 == 0) ? look.road0 : look.road1);
        } else if (k == 1) {
            const Color wc{40, 44, 60, 255};
            DrawLine3D(edgePt(a, kRoadHalf, 0.02f), edgePt(b, kRoadHalf, 0.02f), wc);
            DrawLine3D(edgePt(a, -kRoadHalf, 0.02f), edgePt(b, -kRoadHalf, 0.02f), wc);
            if (i % 3 == 0) DrawLine3D(edgePt(a, kRoadHalf, 0.02f), edgePt(a, -kRoadHalf, 0.02f), wc);
        } else if (i % 5 == 0) {  // not built yet: just survey markers
            for (int s = -1; s <= 1; s += 2) DrawCube(edgePt(a, s * kRoadHalf, 0.4f), 0.5f, 0.8f, 0.5f, Color{255, 140, 0, 255});
        }
        for (int s = -1; s <= 1; s += 2) {
            if (i % 6 == 0) DrawCube(edgePt(a, s * kWallDist, 0.6f), 0.5f, 1.2f, 0.5f, look.wallA);  // fence posts
            if (railBlock) quad(edgePt(a, s * kWallDist, 0.3f), edgePt(b, s * kWallDist, 0.3f), edgePt(b, s * kWallDist, 0.9f), edgePt(a, s * kWallDist, 0.9f), look.wallA);
        }
        if (i < 2)
            for (int c = 0; c < 10; c++) {
                const float w = 2 * kRoadHalf / 10, o0 = -kRoadHalf + c * w, o1 = o0 + w;
                quad(edgePt(a, o0, 0.03f), edgePt(b, o0, 0.03f), edgePt(b, o1, 0.03f), edgePt(a, o1, 0.03f), ((c + i) % 2 == 0) ? WHITE : BLACK);
            }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void drawTrack(Vector3 camPos) {
    if (theme == Theme::Face) drawTrackFace(camPos);
    else if (theme == Theme::Draft) drawTrackDraft(camPos);
    else drawTrackStandard(camPos);
}

// ---------------------------------------------------------------- drawing: ground, backdrop, scenery
void drawGround(Vector3 cam) {
    switch (theme) {
        case Theme::Sunny:
        case Theme::Neon: DrawPlane({cam.x, -0.15f, cam.z}, {7000, 7000}, look.ground); break;
        case Theme::Beach:
            DrawPlane({cam.x, -0.5f, cam.z}, {7000, 7000}, look.sea);
            DrawCylinder({trackCentre.x, -0.4f, trackCentre.z}, trackRadius + 330, trackRadius + 330, 0.3f, 64, look.ground);
            break;
        case Theme::Draft: {
            DrawPlane({cam.x, -0.15f, cam.z}, {7000, 7000}, look.ground);
            const float g = 12.0f;
            const float cx = floorf(cam.x / g) * g, cz = floorf(cam.z / g) * g;
            for (int k = -25; k <= 25; k++) {
                DrawLine3D({cx + k * g, -0.1f, cz - 25 * g}, {cx + k * g, -0.1f, cz + 25 * g}, Color{120, 124, 134, 255});
                DrawLine3D({cx - 25 * g, -0.1f, cz + k * g}, {cx + 25 * g, -0.1f, cz + k * g}, Color{120, 124, 134, 255});
            }
            break;
        }
        case Theme::Face: {
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlSetTexture(texFace.id);
            const float h = 3500.0f, t = 40.0f;
            const float x0 = cam.x - h, x1 = cam.x + h, z0 = cam.z - h, z1 = cam.z + h;
            texQuad(texFace.id, {x0, -0.15f, z0}, {x0, -0.15f, z1}, {x1, -0.15f, z1}, {x1, -0.15f, z0}, {x0 / t, z0 / t}, {x0 / t, z1 / t}, {x1 / t, z1 / t}, {x1 / t, z0 / t}, WHITE);
            rlSetTexture(0);
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
            break;
        }
    }
}

void pyramidTex(Vector3 c, float base, float h) {
    const float r = base * 0.5f;
    const Vector3 apex{c.x, c.y + h, c.z};
    const Vector3 p0{c.x - r, c.y, c.z - r}, p1{c.x + r, c.y, c.z - r}, p2{c.x + r, c.y, c.z + r}, p3{c.x - r, c.y, c.z + r};
    const Vector3 ring[5] = {p0, p1, p2, p3, p0};
    for (int k = 0; k < 4; k++) {  // each side is a quad whose top edge collapses to the apex (raylib drops the texture when the draw mode changes to triangles)
        if (rlCheckRenderBatchLimit(4)) rlSetTexture(texFace.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0, 1); rlVertex3f(ring[k].x, ring[k].y, ring[k].z);
        rlTexCoord2f(1, 1); rlVertex3f(ring[k + 1].x, ring[k + 1].y, ring[k + 1].z);
        rlTexCoord2f(0.5f, 0); rlVertex3f(apex.x, apex.y, apex.z);
        rlTexCoord2f(0.5f, 0); rlVertex3f(apex.x, apex.y, apex.z);
        rlEnd();
    }
}

void drawBackdrop() {
    if (theme == Theme::Draft) return;
    for (int i = 0; i < 18; i++) {
        const float ang = i * (2 * PI / 18) + 0.2f * sinf(i * 1.7f);
        const float r = trackRadius + 900.0f + 120.0f * sinf(i * 2.3f);
        const float hh = 0.5f + 0.5f * sinf(i * 3.1f);
        const Vector3 p{trackCentre.x + cosf(ang) * r, -0.5f, trackCentre.z + sinf(ang) * r};
        if (theme == Theme::Face) {
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            rlSetTexture(texFace.id);
            pyramidTex(p, 600.0f, 220.0f + 90.0f * hh);
            rlSetTexture(0);
            rlDrawRenderBatchActive();
            rlEnableBackfaceCulling();
        } else if (theme == Theme::Beach) {
            DrawCylinder({p.x * 1.0f + (p.x - trackCentre.x) * 0.4f, -0.6f, p.z + (p.z - trackCentre.z) * 0.4f}, 12.0f, 150.0f + 60.0f * hh, 50.0f + 60.0f * hh, 9, look.mount);
        } else {
            DrawCylinder(p, 12.0f, 330.0f, 230.0f + 90.0f * hh, 9, look.mount);
        }
    }
}

void drawNeonBuildings(Vector3 camPos) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlSetTexture(texWin.id);
    for (const Prop& b : props) {
        const float dx = b.p.x - camPos.x, dz = b.p.z - camPos.z;
        if (dx * dx + dz * dz > 520.0f * 520.0f) continue;
        const float hx = b.a * 0.5f, hz = b.c * 0.5f, H = b.b, t = 8.0f;
        const Vector3 c0{b.p.x - hx, 0, b.p.z - hz}, c1{b.p.x + hx, 0, b.p.z - hz}, c2{b.p.x + hx, 0, b.p.z + hz}, c3{b.p.x - hx, 0, b.p.z + hz};
        const Vector3 *cs[5] = {&c0, &c1, &c2, &c3, &c0};
        const float widths[4] = {b.a, b.c, b.a, b.c};
        for (int k = 0; k < 4; k++) {
            const Vector3 lo0 = *cs[k], lo1 = *cs[k + 1];
            const Vector3 hi0{lo0.x, H, lo0.z}, hi1{lo1.x, H, lo1.z};
            texQuad(texWin.id, lo0, lo1, hi1, hi0, {0, H / t}, {widths[k] / t, H / t}, {widths[k] / t, 0}, {0, 0}, b.col);
        }
        texQuad(texWin.id, {c0.x, H, c0.z}, {c1.x, H, c1.z}, {c2.x, H, c2.z}, {c3.x, H, c3.z}, {0.02f, 0.02f}, {0.04f, 0.02f}, {0.04f, 0.04f}, {0.02f, 0.04f}, WHITE);
    }
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    const float pulse = 0.65f + 0.35f * sinf((float)GetTime() * 3.0f);
    for (const Prop& b : props) {
        const float dx = b.p.x - camPos.x, dz = b.p.z - camPos.z;
        if (dx * dx + dz * dz > 520.0f * 520.0f) continue;
        DrawCube({b.p.x, b.b + 0.4f, b.p.z}, b.a + 0.8f, 0.8f, b.c + 0.8f, b.col);              // glowing roof edge
        DrawCube({b.p.x, b.b + 4.0f, b.p.z}, 0.6f, 7.0f, 0.6f, Fade(b.col, 0.4f + 0.6f * pulse));  // beacon
    }
}

void drawScenery(Vector3 camPos) {
    if (theme == Theme::Neon) {
        drawNeonBuildings(camPos);
        return;
    }
    const float t = (float)GetTime();
    for (const Prop& pr : props) {
        const float dx = pr.p.x - camPos.x, dz = pr.p.z - camPos.z;
        if (dx * dx + dz * dz > 380.0f * 380.0f) continue;
        const Vector3 p = pr.p;
        const float h = pr.a;
        switch (theme) {
            case Theme::Sunny:
                DrawCylinder(p, 0.45f, 0.45f, h * 0.35f, 6, Color{95, 66, 40, 255});
                DrawCylinder({p.x, p.y + h * 0.3f, p.z}, 3.0f + h * 0.1f, 0.0f, h, 7, Color{36, 118, 58, 255});
                break;
            case Theme::Beach:  // palm tree
                DrawCylinder(p, 0.45f, 0.7f, h * 1.1f, 6, Color{150, 108, 66, 255});
                DrawCylinder({p.x, p.y + h * 1.1f - 0.2f, p.z}, 5.0f, 0.0f, 1.8f, 8, Color{40, 150, 64, 255});
                DrawCylinder({p.x, p.y + h * 1.1f - 1.0f, p.z}, 3.6f, 0.0f, 1.6f, 8, Color{30, 125, 54, 255});
                DrawSphere({p.x + 0.5f, p.y + h * 1.1f - 0.6f, p.z}, 0.35f, Color{96, 64, 32, 255});
                break;
            case Theme::Face:  // a face on a stick, and a face on top of that
                DrawCubeTexture(texFace, {p.x, p.y + h * 0.25f, p.z}, 1.4f, h * 0.5f, 1.4f, WHITE);
                DrawCubeTexture(texFace, {p.x, p.y + h * 0.5f + 3.5f, p.z}, 7.0f, 7.0f, 7.0f, WHITE);
                break;
            case Theme::Draft:
                switch (pr.kind) {
                    case 0: case 1:
                        DrawCube({p.x, p.y + h * 0.5f, p.z}, h, h, h, Color{172, 174, 178, 255});
                        DrawCubeWires({p.x, p.y + h * 0.5f, p.z}, h, h, h, BLACK);
                        break;
                    case 2: DrawCubeWires({p.x, p.y + h * 0.5f, p.z}, h, h, h, Color{255, 140, 0, 255}); break;
                    case 3: DrawCubeTexture(texChk, {p.x, p.y + h * 0.5f, p.z}, h, h, h, WHITE); break;
                    case 4: {  // a floating, spinning placeholder
                        rlPushMatrix();
                        rlTranslatef(p.x, 7.0f + sinf(t * 1.5f + p.x) * 1.5f, p.z);
                        rlRotatef(t * 45.0f, 0, 1, 0);
                        DrawCube({0, 0, 0}, h * 0.7f, h * 0.7f, h * 0.7f, Color{255, 140, 0, 255});
                        DrawCubeWires({0, 0, 0}, h * 0.7f, h * 0.7f, h * 0.7f, BLACK);
                        rlPopMatrix();
                        break;
                    }
                    default: DrawCubeWires({p.x, 15.0f, p.z}, 2.0f, 30.0f, 2.0f, WHITE); break;  // scaffolding
                }
                break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------- drawing: cars
Color shade(Color c, float k) { return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255}; }

void drawCar(const Car& c) {
    rlPushMatrix();
    rlTranslatef(c.pos.x, 0, c.pos.z);
    rlRotatef(c.yaw * RAD2DEG, 0, 1, 0);
    if (theme == Theme::Neon) DrawCube({0, 0.04f, 0}, 2.6f, 0.02f, 5.0f, Fade(c.color, 0.55f));  // underglow
    else if (!faceMode()) DrawCube({0, 0.03f, 0}, 2.2f, 0.02f, 4.5f, Fade(BLACK, 0.35f));
    cubeT({0, 0.55f, 0}, 1.9f, 0.55f, 4.2f, c.color);
    cubeT({0, 1.02f, -0.3f}, 1.5f, 0.48f, 2.0f, Color{28, 34, 46, 255});
    cubeT({0, 0.86f, 1.25f}, 0.5f, 0.04f, 1.4f, Color{245, 245, 245, 255});
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) cubeT({sx * 1.02f, 0.4f, sz * 1.4f}, 0.34f, 0.8f, 0.8f, Color{20, 20, 22, 255});
    cubeT({0, 1.2f, -2.0f}, 1.9f, 0.08f, 0.5f, shade(c.color, 0.7f));
    cubeT({-0.7f, 0.95f, -1.95f}, 0.08f, 0.5f, 0.08f, shade(c.color, 0.6f));
    cubeT({0.7f, 0.95f, -1.95f}, 0.08f, 0.5f, 0.08f, shade(c.color, 0.6f));
    const Color tail = c.braking ? Color{255, 40, 40, 255} : Color{110, 10, 10, 255};
    cubeT({-0.65f, 0.62f, -2.12f}, 0.4f, 0.15f, 0.05f, tail);
    cubeT({0.65f, 0.62f, -2.12f}, 0.4f, 0.15f, 0.05f, tail);
    cubeT({-0.65f, 0.62f, 2.12f}, 0.4f, 0.15f, 0.05f, Color{255, 245, 170, 255});
    cubeT({0.65f, 0.62f, 2.12f}, 0.4f, 0.15f, 0.05f, Color{255, 245, 170, 255});
    if (theme == Theme::Draft) DrawCubeWires({0, 0.55f, 0}, 1.9f, 0.55f, 4.2f, BLACK);
    rlPopMatrix();
}

// ---------------------------------------------------------------- drawing: 2D (sky, HUD, menu)
void drawSky(int sw, int sh) {
    const int horizon = (int)(sh * 0.62f);
    if (faceMode()) {
        const float tile = sh * 0.31f;
        const float scroll = fmodf(-camYaw * (sw / 1.64f), tile);
        for (float y = 0; y < horizon; y += tile)
            for (float x = scroll - tile; x < sw; x += tile)
                DrawTexturePro(texFace, {0, 0, (float)texFace.width, (float)texFace.height}, {x, y, tile, tile}, {0, 0}, 0, WHITE);
        return;
    }
    DrawRectangleGradientV(0, 0, sw, horizon, look.skyTop, look.skyBot);
    const float hfov = 1.64f * ((float)sw / (float)sh) / 1.78f;
    auto px = [&](float worldYaw) { return sw * 0.5f - wrapAngle(worldYaw - camYaw) * (sw / hfov); };
    if (theme == Theme::Beach) {
        const float x = px(0.6f);
        DrawCircle((int)x, (int)(sh * 0.2f), sh * 0.07f, Color{255, 246, 190, 255});
        DrawCircle((int)x, (int)(sh * 0.2f), sh * 0.1f, Fade(Color{255, 240, 170, 255}, 0.25f));
    } else if (theme == Theme::Neon) {
        for (int i = 0; i < 90; i++) {
            const unsigned h = hash32((unsigned)i * 977u + 5u);
            const float x = px(((h & 1023) / 1023.0f) * 2 * PI - PI);
            const float y = ((h >> 10) & 1023) / 1023.0f * sh * 0.45f;
            DrawRectangle((int)x, (int)y, 2, 2, Fade(WHITE, 0.4f + 0.6f * ((h >> 20) & 7) / 7.0f));
        }
        const float x = px(-0.8f);
        DrawCircle((int)x, (int)(sh * 0.18f), sh * 0.06f, Color{250, 240, 255, 255});
    }
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

// Draw a track outline (and optional car dots) in a square box.
void drawPolyMap(const std::vector<Vector2>& pl, float x0, float y0, float box, float lineW, Color col, bool dots) {
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const Vector2& p : pl) {
        minX = fminf(minX, p.x); maxX = fmaxf(maxX, p.x);
        minZ = fminf(minZ, p.y); maxZ = fmaxf(maxZ, p.y);
    }
    const float span = fmaxf(maxX - minX, maxZ - minZ);
    const float ox = (span - (maxX - minX)) * 0.5f, oz = (span - (maxZ - minZ)) * 0.5f;  // centre the shorter axis
    auto map = [&](float x, float z) { return Vector2{x0 + box - 6 - (x - minX + ox) / span * (box - 12), y0 + box - 6 - (z - minZ + oz) / span * (box - 12)}; };
    for (size_t i = 0; i < pl.size(); i++) {
        const Vector2 a = map(pl[i].x, pl[i].y), b = map(pl[(i + 1) % pl.size()].x, pl[(i + 1) % pl.size()].y);
        DrawLineEx(a, b, lineW, col);
    }
    if (dots)
        for (const Car& c : cars) DrawCircleV(map(c.pos.x, c.pos.z), c.player ? lineW * 1.9f : lineW * 1.4f, c.color);
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
    text(defs()[curId].name, (int)(24 * ui), sh - (int)(40 * ui), (int)(22 * ui), Fade(WHITE, 0.8f));

    const int kph = (int)(fabsf(me.speed) * 3.6f);
    textCentred(std::to_string(kph), sw - (int)(130 * ui), sh - (int)(150 * ui), (int)(84 * ui), WHITE);
    textCentred("KM/H", sw - (int)(130 * ui), sh - (int)(66 * ui), (int)(24 * ui), Color{200, 200, 210, 255});
    const float bw = 260 * ui, bx = sw - bw - 30 * ui, by = sh - 36 * ui;
    DrawRectangle((int)bx, (int)by, (int)bw, (int)(12 * ui), Fade(BLACK, 0.5f));
    DrawRectangle((int)bx, (int)by, (int)(bw * clampf(fabsf(me.speed) / kMaxSpeed, 0.0f, 1.0f)), (int)(12 * ui), Color{255, 140, 40, 255});

    const float box = 190 * ui, pad = 14 * ui;
    DrawRectangle((int)(sw - box - pad) - 6, (int)pad - 6, (int)box + 12, (int)box + 12, Fade(BLACK, 0.45f));
    drawPolyMap(previews[curId], sw - box - pad, pad, box, 3.0f * ui, Color{230, 230, 235, 255}, true);

    if (theme == Theme::Draft) text("PROTOTYPE BUILD 0.0.1 - ART PENDING", sw / 2 - (int)(190 * ui), (int)(16 * ui), (int)(22 * ui), Color{255, 140, 0, 255});

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
        textCentred("R / Enter: race again      M: track menu", sw / 2, (int)(sh * 0.88f), (int)(30 * ui), WHITE);
    }
}

void drawMenu(int sw, int sh, int sel) {
    const float ui = sh / 720.0f;
    const TrackDef& d = defs()[sel];
    const Look& L = kLooks[(int)d.theme];
    if (d.theme == Theme::Face) {
        const float tile = sh * 0.25f;
        for (float y = 0; y < sh; y += tile)
            for (float x = 0; x < sw; x += tile)
                DrawTexturePro(texFace, {0, 0, (float)texFace.width, (float)texFace.height}, {x, y, tile, tile}, {0, 0}, 0, WHITE);
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
    } else {
        Color bot = (d.theme == Theme::Draft) ? Color{205, 209, 216, 255} : L.skyBot;
        DrawRectangleGradientV(0, 0, sw, sh, L.skyTop, bot);
    }
    textCentred("TURBO LOOP", sw / 2, (int)(36 * ui), (int)(84 * ui), Color{255, 220, 70, 255});
    textCentred("choose a track", sw / 2, (int)(128 * ui), (int)(26 * ui), WHITE);

    const float box = 300 * ui, cx = sw / 2 - box / 2, cy = 180 * ui;
    DrawRectangle((int)cx - 8, (int)cy - 8, (int)box + 16, (int)box + 16, Fade(BLACK, 0.55f));
    drawPolyMap(previews[sel], cx, cy, box, 4.0f * ui, WHITE, false);
    textCentred("<", (int)(cx - 70 * ui), (int)(cy + box / 2 - 30 * ui), (int)(70 * ui), WHITE);
    textCentred(">", (int)(cx + box + 70 * ui), (int)(cy + box / 2 - 30 * ui), (int)(70 * ui), WHITE);

    const int ty = (int)(cy + box + 24 * ui);
    textCentred(std::to_string(sel + 1) + ". " + d.name, sw / 2, ty, (int)(46 * ui), WHITE);
    textCentred(d.blurb, sw / 2, ty + (int)(54 * ui), (int)(24 * ui), Color{235, 235, 240, 255});
    if (bestLapByTrack[sel] < 1e8f) textCentred("best lap  " + fmtTime(bestLapByTrack[sel]), sw / 2, ty + (int)(88 * ui), (int)(24 * ui), Color{120, 255, 150, 255});
    textCentred("Left / Right: choose     Enter: race     Esc: quit", sw / 2, sh - (int)(40 * ui), (int)(22 * ui), Fade(WHITE, 0.85f));
}

// ---------------------------------------------------------------- the simulation test (no window)
int runSim() {
    simMode = true;
    int failures = 0;
    for (int id = 0; id < (int)defs().size(); id++) {
        loadTrack(id, false);
        const float dt = 1.0f / 60.0f;
        float t = 0;
        while (t < 500.0f) {
            updateRace(dt);
            t += dt;
            bool all = true;
            for (const Car& c : cars) all = all && c.finished;
            if (all) break;
        }
        printf("== %s: %d samples, %.0f m per lap\n", defs()[id].name, N, avgSpacing * N);
        int done = 0, hits = 0;
        for (const Car& c : cars) {
            printf("   %-5s finished=%d time=%s best lap=%s wall hits=%d\n", c.name, c.finished ? 1 : 0, fmtTime(c.finishTime).c_str(), fmtTime(c.bestLap).c_str(), c.wallHits);
            done += c.finished ? 1 : 0;
            hits += c.wallHits;
        }
        if (done != kCars) failures++;
        printf("   %s (%d wall hits in total)\n", done == kCars ? "OK" : "FAIL: not everyone finished", hits);
    }
    printf(failures ? "FAIL: %d track(s) had problems\n" : "OK: all tracks completed (%d)\n", failures ? failures : (int)defs().size());
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    int startTrack = -1;
    bool sim = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim")) sim = true;
        else if (!strcmp(argv[i], "--track") && i + 1 < argc) startTrack = clampf((float)atoi(argv[++i]) - 1, 0, (float)defs().size() - 1);
    }
    buildPreviews();
    if (sim) return runSim();

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Turbo Loop");
    SetExitKey(KEY_NULL);  // Esc goes back to the menu instead of quitting
    SetTargetFPS(144);
    loadAssets();

    int selected = std::max(startTrack, 0);
    bool inMenu = (startTrack < 0);
    bool quit = false;
    loadTrack(selected, true);

    Camera3D cam{};
    cam.up = {0, 1, 0};
    cam.projection = CAMERA_PERSPECTIVE;
    cam.fovy = 62;
    const int nTracks = (int)defs().size();

    while (!quit && !WindowShouldClose()) {
        const float dt = fminf(GetFrameTime(), 1.0f / 30.0f);
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        if (IsKeyPressed(KEY_F11)) ToggleBorderlessWindowed();

        if (inMenu) {
            const bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
            const bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));
            if (left) selected = (selected + nTracks - 1) % nTracks;
            if (right) selected = (selected + 1) % nTracks;
            for (int k = 0; k < nTracks; k++)
                if (IsKeyPressed(KEY_ONE + k)) selected = k;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
                loadTrack(selected, true);
                inMenu = false;
            }
            if (IsKeyPressed(KEY_ESCAPE)) quit = true;
            BeginDrawing();
            ClearBackground(BLACK);
            drawMenu(sw, sh, selected);
            EndDrawing();
            continue;
        }

        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_M) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) && phase == Phase::Finished)) {
            inMenu = true;
            continue;
        }
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
        ClearBackground(look.skyBot);
        drawSky(sw, sh);
        rlSetClipPlanes(0.3, 4000.0);
        BeginMode3D(cam);
        drawGround(cam.position);
        drawBackdrop();
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
