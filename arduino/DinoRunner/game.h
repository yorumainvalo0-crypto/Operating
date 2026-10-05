// Game state and game logic (independent of the display type).
#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "platform.h"

// Positions and speeds are fixed-point values in 1/16 pixel
#define FP_SHIFT 4
#define DINO_X 8
#define MAX_OBSTACLES 4

enum GameState : uint8_t { STATE_TITLE, STATE_RUNNING, STATE_PAUSED, STATE_GAME_OVER };
enum ObstacleType : uint8_t { OB_CACTUS_S, OB_CACTUS_L, OB_BIRD_LOW, OB_BIRD_HIGH };

// Sprite size, sprite bottom altitude and hitbox (x offset/width, bottom offset/height)
struct ObstacleDef {
  uint8_t w, h, alt;
  uint8_t hbX, hbW, hbB, hbH;
};

const ObstacleDef OB_DEFS[] = {
  {  8, 12,  0,  1,  6, 0, 12 },  // OB_CACTUS_S
  { 12, 16,  0,  1, 10, 0, 16 },  // OB_CACTUS_L
  { 16, 10,  7,  1, 14, 2,  6 },  // OB_BIRD_LOW:  duck under it (or jump high)
  { 16, 10, 24,  1, 14, 2,  6 },  // OB_BIRD_HIGH: just don't jump into it
};

struct Obstacle {
  int16_t x;        // left edge, 1/16 px
  uint8_t type;
  bool active;
};

struct Game {
  GameState state;
  uint8_t difficulty;   // 0 = easy, 1 = normal, 2 = hard
  bool soundOn;
  int16_t dinoY;        // altitude above ground, 1/16 px
  int16_t dinoVY;
  bool ducking;
  uint16_t score;
  uint16_t hiScore[3];
  bool newHiScore;
  uint8_t scoreAcc;     // distance accumulator for the score, 1/16 px
  int16_t speed;        // 1/16 px per frame
  int16_t spawnDist;    // distance until next obstacle, 1/16 px
  uint16_t frame;
  uint16_t groundOffset; // scroll position of the ground pebbles, 1/16 px
  uint32_t gameOverAt;
  Obstacle obs[MAX_OBSTACLES];
};

Game game;

const char DIFF_NAMES[3][7] PROGMEM = { "EASY", "NORMAL", "HARD" };

// ---------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------
void beep(uint16_t freq, uint16_t ms) {
#if BUZZER_PIN >= 0
  if (game.soundOn) tone(BUZZER_PIN, freq, ms);
#else
  (void)freq; (void)ms;
#endif
}

// ---------------------------------------------------------------------
// Persistent settings and high scores (EEPROM)
// ---------------------------------------------------------------------
#define SAVE_MAGIC 0xD5

struct SaveData {
  uint8_t magic;
  uint8_t difficulty;
  uint8_t soundOn;
  uint16_t hiScore[3];
};

void loadSave() {
  SaveData s;
  EEPROM.get(0, s);
  if (s.magic != SAVE_MAGIC || s.difficulty > 2) {
    s.difficulty = 1;
    s.soundOn = 1;
    s.hiScore[0] = s.hiScore[1] = s.hiScore[2] = 0;
  }
  game.difficulty = s.difficulty;
  game.soundOn = s.soundOn;
  for (uint8_t i = 0; i < 3; i++) game.hiScore[i] = s.hiScore[i];
}

void writeSave() {
  SaveData s;
  s.magic = SAVE_MAGIC;
  s.difficulty = game.difficulty;
  s.soundOn = game.soundOn;
  for (uint8_t i = 0; i < 3; i++) s.hiScore[i] = game.hiScore[i];
  EEPROM.put(0, s);  // put() only writes bytes that changed
}

// ---------------------------------------------------------------------
// Collision
// ---------------------------------------------------------------------
#if CELL_COLLISION
// Text LCD: obstacles occupy one character cell, collide if dino is in the same cell
int8_t obstacleCol(const Obstacle& o) {
  int16_t center = (o.x >> FP_SHIFT) + OB_DEFS[o.type].w / 2;
  return center < 0 ? -1 : center / 8;
}

uint8_t obstacleRow(const Obstacle& o) {
  return (o.type == OB_BIRD_LOW || o.type == OB_BIRD_HIGH) ? 0 : 1;
}

uint8_t dinoRow() {
  return game.dinoY > LCD_JUMP_THRESHOLD ? 0 : 1;
}

bool collides(const Obstacle& o) {
  return obstacleCol(o) == LCD_DINO_COL && obstacleRow(o) == dinoRow();
}
#else
// Graphic displays: axis-aligned hitbox test (altitudes measured upwards from the ground)
bool collides(const Obstacle& o) {
  const ObstacleDef& d = OB_DEFS[o.type];
  int16_t ox = (o.x >> FP_SHIFT) + d.hbX;
  int16_t oBottom = d.alt + d.hbB;
  int16_t oTop = oBottom + d.hbH;

  int16_t dx, dw, dh;
  if (game.ducking) { dx = DINO_X + 1; dw = 14; dh = 7; }
  else              { dx = DINO_X + 4; dw = 10; dh = 14; }
  int16_t dBottom = game.dinoY >> FP_SHIFT;

  return ox < dx + dw && ox + d.hbW > dx && oBottom < dBottom + dh && oTop > dBottom;
}
#endif

// ---------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------
void startGame() {
  randomSeed(micros());
  game.state = STATE_RUNNING;
  game.dinoY = 0;
  game.dinoVY = 0;
  game.ducking = false;
  game.score = 0;
  game.scoreAcc = 0;
  game.newHiScore = false;
  game.frame = 0;
  game.groundOffset = 0;
  game.speed = SPEED_START + game.difficulty * DIFF_SPEED_BONUS;
  game.spawnDist = (SCREEN_W / 2) << FP_SHIFT;
  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) game.obs[i].active = false;
}

void gameOver() {
  game.state = STATE_GAME_OVER;
  game.gameOverAt = millis();
  if (game.score > game.hiScore[game.difficulty]) {
    game.hiScore[game.difficulty] = game.score;
    game.newHiScore = true;
  }
  writeSave();
  beep(150, 300);
}

uint8_t pickObstacleType() {
  uint8_t r = random(100);
#if USE_LOW_BIRDS
  if (game.score > 250 && r < 15) return OB_BIRD_LOW;
#endif
  if (game.score > 120 && r < 30) return OB_BIRD_HIGH;
#if USE_LARGE_CACTUS
  if (r < 60) return OB_CACTUS_L;
#endif
  return OB_CACTUS_S;
}

void spawnObstacle() {
  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
    Obstacle& o = game.obs[i];
    if (o.active) continue;
    o.active = true;
    o.type = pickObstacleType();
    o.x = SCREEN_W << FP_SHIFT;

    // Minimum gap = distance covered during one jump + safety margin
    const int16_t airFrames = 2 * JUMP_VELOCITY / GRAVITY;
    int16_t minGap = (int32_t)airFrames * game.speed / 16 + MIN_GAP_EXTRA + OB_DEFS[o.type].w;
    int16_t randomGap = GAP_RANDOM - game.difficulty * (GAP_RANDOM / 4);
    game.spawnDist = (minGap + (int16_t)random(randomGap)) << FP_SHIFT;
    return;
  }
  // No free slot: try again next frame (spawnDist stays <= 0)
}

// Advance the game by one frame
void gameUpdate(bool jumpPressed, bool duckHeld) {
  game.frame++;

  // --- Dino physics ---
  bool onGround = game.dinoY == 0 && game.dinoVY == 0;
  if (onGround && jumpPressed) {
    game.dinoVY = JUMP_VELOCITY;
    onGround = false;
    beep(880, 25);
  }
  if (!onGround) {
    game.dinoY += game.dinoVY;
    game.dinoVY -= duckHeld ? FAST_FALL_GRAVITY : GRAVITY;
    if (game.dinoY <= 0) {
      game.dinoY = 0;
      game.dinoVY = 0;
      onGround = true;
    }
  }
  game.ducking = onGround && duckHeld;

  // --- Obstacles ---
  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
    Obstacle& o = game.obs[i];
    if (!o.active) continue;
    o.x -= game.speed;
    if ((o.x >> FP_SHIFT) + OB_DEFS[o.type].w < 0) o.active = false;
  }
  game.spawnDist -= game.speed;
  if (game.spawnDist <= 0) spawnObstacle();

  // --- Score and speed ---
  uint16_t oldScore = game.score;
  uint16_t acc = game.scoreAcc + game.speed;
  game.score += acc / (SCORE_PIXELS << FP_SHIFT);
  game.scoreAcc = acc % (SCORE_PIXELS << FP_SHIFT);
  if (game.score / 100 != oldScore / 100) beep(1320, 40);

  int16_t speed = SPEED_START + game.difficulty * DIFF_SPEED_BONUS + game.score / SPEED_STEP_SCORE;
  game.speed = speed > SPEED_MAX ? SPEED_MAX : speed;
  game.groundOffset = (game.groundOffset + game.speed) % (SCREEN_W << FP_SHIFT);

  // --- Collision ---
  for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
    if (game.obs[i].active && collides(game.obs[i])) {
      gameOver();
      return;
    }
  }
}
