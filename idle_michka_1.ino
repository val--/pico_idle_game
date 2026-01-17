#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ================= LCD =================
#define TFT_DC   8
#define TFT_CS   9
#define TFT_SCK  10
#define TFT_MOSI 11
#define TFT_RST  12
#define TFT_BL   13

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);

// ================= INPUT =================
#define JOY_UP     2
#define JOY_DOWN   18
#define JOY_LEFT   16
#define JOY_RIGHT  20
#define BTN_OK     15

static inline bool pressed(int pin) { return digitalRead(pin) == LOW; }

// ================= SCREEN =================
static const int W = 240;
static const int H = 240;

// ===== HUD sizing (bigger text) =====
static const int HUD_TEXT_SIZE = 2;
static const int HUD_TOP_H = 24;
static const int HUD_BOT_H = 24;

// ✅ NEW: dedicated progress bar band (always cleared fully)
static const int BAR_H = 14;
static const int BAR_Y = H - HUD_BOT_H - BAR_H;     // just above bottom HUD
static const int BAR_X = 10;
static const int BAR_W = W - 20;

// ===== Grid (logical) =====
static const int GRID_W = 16;
static const int GRID_H = 6;

static const int TILE_W = W / GRID_W; // 15

// Height uses all space except HUDs + BAR band
static const int GAME_H_AVAIL = H - HUD_TOP_H - HUD_BOT_H - BAR_H;
static const int TILE_H = GAME_H_AVAIL / GRID_H;
static const int GAME_USED_H = TILE_H * GRID_H;
static const int GAME_Y_PAD  = (GAME_H_AVAIL - GAME_USED_H) / 2;

static const int GRID_X = (W - GRID_W * TILE_W) / 2;
static const int GRID_Y = HUD_TOP_H + GAME_Y_PAD;

// House / garden split
static const int SEP_X  = 8;
static const int DOOR_Y = 3;

// Colors
static const uint16_t COL_HOUSE  = ST77XX_BLACK;
static const uint16_t COL_GARDEN = 0x03E0;        // green
static const uint16_t COL_WALL   = ST77XX_WHITE;
static const uint16_t COL_TEXT   = ST77XX_WHITE;
static const uint16_t COL_UI_BG  = ST77XX_BLACK;

// Veg colors
static const uint16_t COL_VEG_0 = 0x7BEF;
static const uint16_t COL_VEG_1 = 0x07E0;
static const uint16_t COL_VEG_2 = 0xFFE0;

// ================= GAME STATE (HUD) =================
int resM = 0;
int resV = 0;
int resK = 0;

// ================= PLAYER =================
int catGX = 3, catGY = 2;
int oldCatGX = 3, oldCatGY = 2;

unsigned long lastMoveMs = 0;
const unsigned long MOVE_DELAY = 120;

bool prevOk = false;

// HUD caches
int lastResM = -999, lastResV = -999, lastResK = -999;
bool bottomDrawn = false;

// ================= VEGGIES =================
static const unsigned long VEG_GROW_MIN_MS = 6500;
static const unsigned long VEG_GROW_MAX_MS = 10000;

struct Veg {
  int x, y;
  int stage;
  unsigned long nextStepMs;
};

Veg vegs[] = {
  {10, 1, 0, 0},
  {12, 3, 0, 0},
  {14, 2, 0, 0},
  { 9, 4, 0, 0}
};
static const int VEG_COUNT = sizeof(vegs) / sizeof(vegs[0]);

// Harvest
static const unsigned long HARVEST_DURATION_MS = 2500;
struct HarvestState {
  bool active;
  int vegIndex;
  unsigned long startMs;
  unsigned long endMs;
} harvest = {false, -1, 0, 0};

// ================= HELPERS =================
static inline bool isGardenCell(int gx) { return gx >= SEP_X; }
static inline int cellX(int gx) { return GRID_X + gx * TILE_W; }
static inline int cellY(int gy) { return GRID_Y + gy * TILE_H; }

int findVegAt(int gx, int gy) {
  for (int i = 0; i < VEG_COUNT; i++) {
    if (vegs[i].x == gx && vegs[i].y == gy) return i;
  }
  return -1;
}

void drawCell(int gx, int gy) {
  int x = cellX(gx);
  int y = cellY(gy);
  uint16_t bg = isGardenCell(gx) ? COL_GARDEN : COL_HOUSE;
  tft.fillRect(x, y, TILE_W, TILE_H, bg);
}

void drawFrame() {
  tft.drawRect(GRID_X, GRID_Y, GRID_W * TILE_W, GRID_H * TILE_H, COL_WALL);
}

void drawSeparatorWithDoor() {
  int x = GRID_X + SEP_X * TILE_W;

  for (int gy = 0; gy < GRID_H; gy++) {
    if (gy == DOOR_Y) continue;
    int y = cellY(gy);
    tft.drawFastVLine(x, y, TILE_H, COL_WALL);
  }

  int py = cellY(DOOR_Y);
  int doorH = TILE_H - 4;
  if (doorH < 6) doorH = 6;
  tft.drawRect(x - 2, py + 2, 4, doorH, COL_WALL);
}

void drawStaticWalls() {
  drawFrame();
  drawSeparatorWithDoor();
}

bool canCrossSeparator(int fromGX, int fromGY, int toGX, int toGY) {
  if (fromGY == toGY) {
    bool crossing =
      (fromGX == SEP_X - 1 && toGX == SEP_X) ||
      (fromGX == SEP_X && toGX == SEP_X - 1);
    if (crossing) return (fromGY == DOOR_Y);
  }
  return true;
}

// ================= VEG DRAW =================
void drawVegAtCell(int gx, int gy, int stage) {
  int px = cellX(gx);
  int py = cellY(gy);

  int cx = px + TILE_W / 2;
  int baseY = py + TILE_H - 4;

  if (stage == 0) {
    tft.drawPixel(cx, baseY, COL_VEG_0);
    return;
  }

  if (stage == 1) {
    int stemTop = baseY - 6;
    if (stemTop < py + 3) stemTop = py + 3;
    tft.drawFastVLine(cx, stemTop, baseY - stemTop, COL_VEG_1);
    tft.drawPixel(cx - 1, stemTop + 2, COL_VEG_1);
    tft.drawPixel(cx + 1, stemTop + 2, COL_VEG_1);
    return;
  }

  uint16_t c = COL_VEG_2;
  int r = 3;
  int y = py + TILE_H / 2;
  if (y - r < py + 2) y = py + 2 + r;
  if (y + r > py + TILE_H - 2) y = py + TILE_H - 2 - r;

  tft.fillCircle(cx - 3, y, r, c);
  tft.fillCircle(cx + 3, y, r, c);
  tft.fillCircle(cx,     y - 3, r, c);
}

void redrawCellWithEntities(int gx, int gy) {
  drawCell(gx, gy);

  int vi = findVegAt(gx, gy);
  if (vi >= 0) drawVegAtCell(gx, gy, vegs[vi].stage);

  drawStaticWalls();
}

// ================= CAT =================
void drawCat(int gx, int gy) {
  int px = cellX(gx);
  int py = cellY(gy);

  const int mx = 2;
  const int my = 2;

  int cx = px + TILE_W / 2;
  int cy = py + TILE_H / 2;

  int maxRbyW = (TILE_W / 2) - mx;
  int maxRbyH = (TILE_H / 2) - my;
  int R = (maxRbyW < maxRbyH) ? maxRbyW : maxRbyH;

  if (R > 7) R = 7;
  if (R < 4) R = 4;

  if (cy - (R + 4) < py + my) cy = (py + my) + (R + 4);
  if (cy + R > py + TILE_H - 1 - my) cy = (py + TILE_H - 1 - my) - R;

  tft.fillCircle(cx, cy, R, ST77XX_WHITE);

  int earTopY = cy - (R + 4);
  if (earTopY < py + my) earTopY = py + my;

  tft.fillTriangle(cx - R, cy - 1, cx - 1, cy - 1, cx - (R - 1), earTopY, ST77XX_WHITE);
  tft.fillTriangle(cx + 1, cy - 1, cx + R, cy - 1, cx + (R - 1), earTopY, ST77XX_WHITE);

  tft.fillCircle(cx - (R / 2), cy - 1, 1, ST77XX_BLACK);
  tft.fillCircle(cx + (R / 2), cy - 1, 1, ST77XX_BLACK);

  tft.fillTriangle(cx, cy + 1, cx - 1, cy + 3, cx + 1, cy + 3, ST77XX_BLACK);
}

// ================= HUD =================
void drawTopHUD(bool force) {
  if (!force && resM == lastResM && resV == lastResV && resK == lastResK) return;

  tft.fillRect(0, 0, W, HUD_TOP_H, COL_UI_BG);
  tft.drawFastHLine(0, HUD_TOP_H - 1, W, COL_WALL);

  tft.setTextWrap(false);
  tft.setTextSize(HUD_TEXT_SIZE);
  tft.setTextColor(COL_TEXT, COL_UI_BG);

  char buf[40];
  snprintf(buf, sizeof(buf), "S:%d  L:%d  C:%d", resM, resV, resK);
  tft.setCursor(6, (HUD_TOP_H - 16) / 2);
  tft.print(buf);

  lastResM = resM; lastResV = resV; lastResK = resK;
}

void drawBottomHUD(bool force) {
  if (!force && bottomDrawn) return;

  int y = H - HUD_BOT_H;
  tft.fillRect(0, y, W, HUD_BOT_H, COL_UI_BG);
  tft.drawFastHLine(0, y, W, COL_WALL);

  tft.setTextWrap(false);
  tft.setTextSize(HUD_TEXT_SIZE);
  tft.setTextColor(COL_TEXT, COL_UI_BG);

  tft.setCursor(6, y + (HUD_BOT_H - 16) / 2);
  tft.print("Michka se promene");

  bottomDrawn = true;
}

// ✅ NEW: always clear the progress band completely (no residues)
void clearProgressBand() {
  tft.fillRect(0, BAR_Y, W, BAR_H, COL_UI_BG);
  // optional small separator line
  // tft.drawFastHLine(0, BAR_Y, W, COL_WALL);
}

void drawHUD(bool force) {
  drawTopHUD(force);
  drawBottomHUD(force);
  if (force) clearProgressBand();
}

// ================= HARVEST BAR =================
void drawHarvestBar(unsigned long now) {
  // Always start by clearing the whole band => no leftover pixels
  clearProgressBand();

  if (!harvest.active) return;

  unsigned long total = harvest.endMs - harvest.startMs;
  unsigned long elapsed = (now > harvest.startMs) ? (now - harvest.startMs) : 0;
  if (elapsed > total) elapsed = total;

  int fill = (int)((elapsed * (unsigned long)(BAR_W - 2)) / total);
  if (fill < 0) fill = 0;
  if (fill > BAR_W - 2) fill = BAR_W - 2;

  tft.drawRect(BAR_X, BAR_Y + 2, BAR_W, BAR_H - 4, COL_WALL);
  if (fill > 0) {
    tft.fillRect(BAR_X + 1, BAR_Y + 3, fill, (BAR_H - 4) - 2, ST77XX_WHITE);
  }
}

// ================= VEG UPDATE =================
void scheduleVegIfNeeded(int i, unsigned long now) {
  if (vegs[i].nextStepMs == 0) {
    vegs[i].nextStepMs = now + (unsigned long)random(VEG_GROW_MIN_MS, VEG_GROW_MAX_MS);
  }
}

void updateVeggies(unsigned long now) {
  for (int i = 0; i < VEG_COUNT; i++) {
    if (vegs[i].stage < 2) scheduleVegIfNeeded(i, now);

    if (vegs[i].stage < 2 && vegs[i].nextStepMs > 0 && now >= vegs[i].nextStepMs) {
      vegs[i].stage++;
      if (vegs[i].stage < 2) vegs[i].nextStepMs = now + (unsigned long)random(VEG_GROW_MIN_MS, VEG_GROW_MAX_MS);
      else vegs[i].nextStepMs = 0;

      redrawCellWithEntities(vegs[i].x, vegs[i].y);
      if (catGX == vegs[i].x && catGY == vegs[i].y) drawCat(catGX, catGY);
    }
  }
}

// ================= HARVEST =================
void startHarvest(int vegIndex, unsigned long now) {
  harvest.active = true;
  harvest.vegIndex = vegIndex;
  harvest.startMs = now;
  harvest.endMs = now + HARVEST_DURATION_MS;
  drawHarvestBar(now);
}

void finishHarvest(unsigned long now) {
  (void)now;
  if (!harvest.active) return;

  int i = harvest.vegIndex;
  if (i >= 0 && i < VEG_COUNT) {
    resV += 1;
    vegs[i].stage = 0;
    vegs[i].nextStepMs = 0;

    redrawCellWithEntities(vegs[i].x, vegs[i].y);
    if (catGX == vegs[i].x && catGY == vegs[i].y) drawCat(catGX, catGY);

    drawTopHUD(false);
  }

  harvest.active = false;
  harvest.vegIndex = -1;

  // Clear the band once at end
  drawHarvestBar(millis());
}

void updateHarvest(unsigned long now) {
  if (!harvest.active) return;

  drawHarvestBar(now);

  if (now >= harvest.endMs) {
    finishHarvest(now);
  }
}

// ================= WORLD DRAW =================
void drawWorldOnce() {
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      drawCell(gx, gy);
    }
  }
  drawStaticWalls();

  for (int i = 0; i < VEG_COUNT; i++) {
    drawVegAtCell(vegs[i].x, vegs[i].y, vegs[i].stage);
  }

  drawStaticWalls();
}

// ================= SETUP =================
void setup() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init(240, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  randomSeed(micros());

  drawHUD(true);
  drawWorldOnce();
  drawCat(catGX, catGY);
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  updateVeggies(now);

  if (harvest.active) {
    updateHarvest(now);
    return;
  }

  bool ok = pressed(BTN_OK);
  bool okEdge = (ok && !prevOk);
  prevOk = ok;

  if (okEdge) {
    int vi = findVegAt(catGX, catGY);
    if (vi >= 0 && vegs[vi].stage == 2) {
      startHarvest(vi, now);
      return;
    }
  }

  if (now - lastMoveMs < MOVE_DELAY) return;

  int nextGX = catGX;
  int nextGY = catGY;

  if (pressed(JOY_UP))    nextGY--;
  if (pressed(JOY_DOWN))  nextGY++;
  if (pressed(JOY_LEFT))  nextGX--;
  if (pressed(JOY_RIGHT)) nextGX++;

  nextGX = constrain(nextGX, 0, GRID_W - 1);
  nextGY = constrain(nextGY, 0, GRID_H - 1);

  if (!canCrossSeparator(catGX, catGY, nextGX, nextGY)) {
    lastMoveMs = now;
    return;
  }

  if (nextGX == catGX && nextGY == catGY) {
    lastMoveMs = now;
    return;
  }

  oldCatGX = catGX;
  oldCatGY = catGY;
  catGX = nextGX;
  catGY = nextGY;

  redrawCellWithEntities(oldCatGX, oldCatGY);
  drawCat(catGX, catGY);

  lastMoveMs = now;
}
