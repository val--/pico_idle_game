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

static inline bool pressed(int pin) { return digitalRead(pin) == LOW; }

// ================= SCREEN =================
static const int W = 240;
static const int H = 240;

// ===== HUD sizing (bigger text) =====
// Default GFX font is 6x8 px. With textSize=2 => 12x16 px.
// We'll reserve enough height for it + padding.
static const int HUD_TEXT_SIZE = 2;
static const int HUD_TOP_H = 24;   // fits textSize=2 comfortably
static const int HUD_BOT_H = 24;

// ===== Grid (logical) =====
static const int GRID_W = 16;
static const int GRID_H = 6;

// Width constraint: 240 / 16 = 15 max
static const int TILE_W = W / GRID_W; // 15

// Height: take all remaining space
static const int GAME_H_AVAIL = H - HUD_TOP_H - HUD_BOT_H;

// Make tiles taller to fill the remaining area (non-square tiles)
static const int TILE_H = GAME_H_AVAIL / GRID_H; // e.g. 192/6=32 (or 31 depending on HUD)

// Center game area vertically within remaining space (handles remainder)
static const int GAME_USED_H = TILE_H * GRID_H;
static const int GAME_Y_PAD = (GAME_H_AVAIL - GAME_USED_H) / 2;

static const int GRID_X = (W - GRID_W * TILE_W) / 2; // should be 0
static const int GRID_Y = HUD_TOP_H + GAME_Y_PAD;

// House / garden split
static const int SEP_X  = 8;   // separation between x=7 and x=8
static const int DOOR_Y = 3;   // door row

// Colors
static const uint16_t COL_HOUSE  = ST77XX_BLACK;
static const uint16_t COL_GARDEN = 0x03E0; // green RGB565
static const uint16_t COL_WALL   = ST77XX_WHITE;
static const uint16_t COL_TEXT   = ST77XX_WHITE;
static const uint16_t COL_HUD_BG = ST77XX_BLACK;

// ================= GAME STATE (HUD) =================
int resM = 0; // souris
int resV = 0; // legumes
int resK = 0; // croquettes

// ================= PLAYER =================
int catGX = 3, catGY = 2;
int oldCatGX = 3, oldCatGY = 2;

// movement held
unsigned long lastMoveMs = 0;
const unsigned long MOVE_DELAY = 120;

// HUD caches
int lastResM = -999, lastResV = -999, lastResK = -999;
bool bottomDrawn = false;

// ================= HELPERS =================
static inline bool isGardenCell(int gx) { return gx >= SEP_X; }

static inline int cellX(int gx) { return GRID_X + gx * TILE_W; }
static inline int cellY(int gy) { return GRID_Y + gy * TILE_H; }

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

  // door outline
  int py = cellY(DOOR_Y);
  // Keep door outline inside the game area (avoid exceeding)
  int doorH = TILE_H - 4;
  if (doorH < 6) doorH = 6;
  tft.drawRect(x - 2, py + 2, 4, doorH, COL_WALL);
}

void drawStaticWalls() {
  drawFrame();
  drawSeparatorWithDoor();
}

void drawWorldOnce() {
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      drawCell(gx, gy);
    }
  }
  drawStaticWalls();
}

// Restore exactly one cell + redraw walls (cheap and stable)
void restoreBackgroundAt(int gx, int gy) {
  drawCell(gx, gy);
  drawStaticWalls();
}

// ================= CAT SPRITE =================
// IMPORTANT: sprite strictly inside TILE_W x TILE_H => no trailing outside
void drawCat(int gx, int gy) {
  int px = cellX(gx);
  int py = cellY(gy);

  // safe margins
  const int mx = 2;
  const int my = 2;

  int cx = px + TILE_W / 2;
  int cy = py + TILE_H / 2;

  // Radius based on the smallest dimension
  int maxRbyW = (TILE_W / 2) - mx;   // keep inside width
  int maxRbyH = (TILE_H / 2) - my;   // keep inside height
  int R = maxRbyW < maxRbyH ? maxRbyW : maxRbyH;

  // clamp radius to a reasonable look
  if (R > 7) R = 7;
  if (R < 4) R = 4;

  // ensure head fits vertically (important when TILE_H is big/small)
  if (cy - (R + 4) < py + my) cy = (py + my) + (R + 4);
  if (cy + R > py + TILE_H - 1 - my) cy = (py + TILE_H - 1 - my) - R;

  // head
  tft.fillCircle(cx, cy, R, ST77XX_WHITE);

  // ears (triangles), kept inside the cell
  int earTopY = cy - (R + 4);
  if (earTopY < py + my) earTopY = py + my;

  // left ear
  tft.fillTriangle(
    cx - R,  cy - 1,
    cx - 1,  cy - 1,
    cx - (R - 1), earTopY,
    ST77XX_WHITE
  );
  // right ear
  tft.fillTriangle(
    cx + 1,  cy - 1,
    cx + R,  cy - 1,
    cx + (R - 1), earTopY,
    ST77XX_WHITE
  );

  // eyes
  tft.fillCircle(cx - (R / 2), cy - 1, 1, ST77XX_BLACK);
  tft.fillCircle(cx + (R / 2), cy - 1, 1, ST77XX_BLACK);

  // nose
  tft.fillTriangle(cx, cy + 1, cx - 1, cy + 3, cx + 1, cy + 3, ST77XX_BLACK);
}

// ================= COLLISION =================
bool canCrossSeparator(int fromGX, int fromGY, int toGX, int toGY) {
  // cross wall only through the door
  if (fromGY == toGY) {
    bool crossing =
      (fromGX == SEP_X - 1 && toGX == SEP_X) ||
      (fromGX == SEP_X && toGX == SEP_X - 1);
    if (crossing) return (fromGY == DOOR_Y);
  }
  return true;
}

// ================= HUD =================
void drawTopHUD(bool force) {
  if (!force && resM == lastResM && resV == lastResV && resK == lastResK) return;

  tft.fillRect(0, 0, W, HUD_TOP_H, COL_HUD_BG);
  tft.drawFastHLine(0, HUD_TOP_H - 1, W, COL_WALL);

  tft.setTextWrap(false);
  tft.setTextSize(HUD_TEXT_SIZE);
  tft.setTextColor(COL_TEXT, COL_HUD_BG);

  char buf[40];
  snprintf(buf, sizeof(buf), "S:%d  L:%d  C:%d", resM, resV, resK);

  // Center-ish vertically for textSize=2 (height ~16)
  tft.setCursor(6, (HUD_TOP_H - 16) / 2);
  tft.print(buf);

  lastResM = resM; lastResV = resV; lastResK = resK;
}

void drawBottomHUD(bool force) {
  if (!force && bottomDrawn) return;

  int y = H - HUD_BOT_H;
  tft.fillRect(0, y, W, HUD_BOT_H, COL_HUD_BG);
  tft.drawFastHLine(0, y, W, COL_WALL);

  tft.setTextWrap(false);
  tft.setTextSize(HUD_TEXT_SIZE);
  tft.setTextColor(COL_TEXT, COL_HUD_BG);

  // "Michka se promene" only
  tft.setCursor(6, y + (HUD_BOT_H - 16) / 2);
  tft.print("Michka se promene");

  bottomDrawn = true;
}

void drawHUD(bool force) {
  drawTopHUD(force);
  drawBottomHUD(force);
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

  drawHUD(true);
  drawWorldOnce();
  drawCat(catGX, catGY);
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();
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

  // Restore old cell, then draw new cell sprite
  restoreBackgroundAt(oldCatGX, oldCatGY);
  drawCat(catGX, catGY);

  // HUD doesn't change often; keep it stable and readable
  // drawHUD(false);

  lastMoveMs = now;
}
