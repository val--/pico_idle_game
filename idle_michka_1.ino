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
static const int HUD_TOP_H = 20;
static const int HUD_BOT_H = 20;

// Dedicated progress bar band (not part of the game area)
static const int BAR_H = 12;
static const int BAR_Y = H - HUD_BOT_H - BAR_H;     // just above bottom HUD
static const int BAR_X = 10;
static const int BAR_W = W - 20;

// ===== Grid (logical) =====
static const int GRID_W = 12;  // Reduced for bigger tiles

// Calculate available height: from top HUD to progress bar
static const int GAME_H_AVAIL = BAR_Y - HUD_TOP_H;
// Calculate tile width based on screen width
static const int TILE_W = W / GRID_W;
// Calculate how many rows we can fit with square tiles to maximize height usage
static const int GRID_H = GAME_H_AVAIL / TILE_W;
// Use square tiles
static const int TILE_H = TILE_W;

static const int GAME_USED_W = TILE_W * GRID_W;
static const int GAME_USED_H = TILE_H * GRID_H;
static const int GAME_X_PAD = (W - GAME_USED_W) / 2;
// Start at top, no vertical padding - use maximum height
static const int GAME_Y_PAD = 0;

static const int GRID_X = GAME_X_PAD;
static const int GRID_Y = HUD_TOP_H;

// House / garden split
static const int SEP_X  = 6;  // Adjusted for GRID_W = 12 (half of grid)
static const int DOOR_Y = 3;

// Colors
static const uint16_t COL_HOUSE  = ST77XX_BLACK;
static const uint16_t COL_GARDEN = 0x03E0;        // green (base, not used directly)
static const uint16_t COL_WALL   = ST77XX_WHITE;
static const uint16_t COL_TEXT   = ST77XX_WHITE;
static const uint16_t COL_UI_BG  = ST77XX_BLACK;

// Lawn colors - multiple shades for realistic grass
static const uint16_t COL_GRASS_DARK = 0x02C0;   // dark green
static const uint16_t COL_GRASS_MID = 0x03E0;     // medium green
static const uint16_t COL_GRASS_LIGHT = 0x07E0;  // light green
static const uint16_t COL_DIRT_DARK = 0x4208;    // dark brown dirt
static const uint16_t COL_DIRT_LIGHT = 0x5A67;    // light brown dirt

// Veg colors - darker/more saturated for better contrast on green lawn
static const uint16_t COL_VEG_0 = 0x7BEF;      // light gray for seed
static const uint16_t COL_BRANCH = 0x4208;     // dark brown for branches (good contrast on green)
static const uint16_t COL_FOLIAGE = 0x05E0;    // darker green for foliage (more contrast)
static const uint16_t COL_FRUIT = 0xF800;       // bright red for fruits

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
  { 8, 0, 0, 0},  // Top of garden
  {10, 2, 0, 0},  // Upper-middle
  { 9, 5, 0, 0},  // Lower-middle
  {11, 7, 0, 0}   // Bottom of garden (distributed across full height)
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

// ===== Harvest bar anti-flicker cache =====
int lastBarFill = -1;
unsigned long lastBarDrawMs = 0;
const unsigned long BAR_REFRESH_MS = 33; // ~30 fps

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

void drawLawnCell(int gx, int gy) {
  int x = cellX(gx);
  int y = cellY(gy);
  
  // Simple hash based on cell position for deterministic pattern
  int hash = ((gx * 7) + (gy * 11)) % 20;
  
  // Base color varies slightly by cell (no light green)
  uint16_t baseColor;
  if (hash < 3) {
    baseColor = COL_GRASS_DARK;  // 15% dark green
  } else if (hash < 4) {
    baseColor = COL_DIRT_LIGHT;   // 5% light dirt
  } else {
    baseColor = COL_GRASS_MID;    // 80% medium green
  }
  
  tft.fillRect(x, y, TILE_W, TILE_H, baseColor);
  
  // Add a small square dirt patch in some cells (20% chance)
  if (hash < 4 && TILE_W > 3 && TILE_H > 3) {
    int patchSize = 2 + (hash % 2);  // 2x2 or 3x3 square
    int patchX = x + (hash % (TILE_W - patchSize));
    int patchY = y + ((hash * 3) % (TILE_H - patchSize));
    tft.fillRect(patchX, patchY, patchSize, patchSize, COL_DIRT_DARK);
  }
}

void drawCell(int gx, int gy) {
  if (isGardenCell(gx)) {
    drawLawnCell(gx, gy);
  } else {
    int x = cellX(gx);
    int y = cellY(gy);
    tft.fillRect(x, y, TILE_W, TILE_H, COL_HOUSE);
  }
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
  // Center the door both horizontally and vertically
  int doorW = 4;
  int doorX = x - (doorW / 2);
  int doorY = py + (TILE_H - doorH) / 2;  // Center vertically in the cell
  tft.drawRect(doorX, doorY, doorW, doorH, COL_WALL);
}

void drawStaticWalls() {
  drawFrame();
  drawSeparatorWithDoor();
}

bool canCrossSeparator(int fromGX, int fromGY, int toGX, int toGY) {
  // Check if crossing the separator horizontally (including diagonal movements)
  bool crossingHorizontally =
    (fromGX == SEP_X - 1 && toGX == SEP_X) ||
    (fromGX == SEP_X && toGX == SEP_X - 1);
  
  if (crossingHorizontally) {
    // Can only cross if at door Y position
    return (fromGY == DOOR_Y && toGY == DOOR_Y);
  }
  
  // Allow all other movements (pure vertical, or movements that don't cross the separator)
  return true;
}

// ================= VEG DRAW =================
void drawVegAtCell(int gx, int gy, int stage) {
  int px = cellX(gx);
  int py = cellY(gy);

  int cx = px + TILE_W / 2;
  int baseY = py + TILE_H - 1;

  if (stage == 0) {
    // Rien - pas de dessin
    return;
  }

  if (stage == 1) {
    // Arbuste "nu" - branches marron seulement (pas de tronc vertical)
    int baseBranchY = baseY - 1;
    
    // Branches principales qui partent de la base et montent
    // Branche gauche principale
    int leftBranchStartY = baseBranchY;
    int leftBranchEndY = baseBranchY - 6;
    if (leftBranchEndY < py + 2) leftBranchEndY = py + 2;
    for (int y = leftBranchStartY; y >= leftBranchEndY; y--) {
      int offset = (leftBranchStartY - y) / 2;
      if (cx - offset >= px + 1) {
        tft.drawPixel(cx - offset, y, COL_BRANCH);
        if (offset > 0 && cx - offset - 1 >= px) {
          tft.drawPixel(cx - offset - 1, y, COL_BRANCH);
        }
      }
    }
    
    // Branche droite principale
    int rightBranchStartY = baseBranchY;
    int rightBranchEndY = baseBranchY - 6;
    if (rightBranchEndY < py + 2) rightBranchEndY = py + 2;
    for (int y = rightBranchStartY; y >= rightBranchEndY; y--) {
      int offset = (rightBranchStartY - y) / 2;
      if (cx + offset < px + TILE_W - 1) {
        tft.drawPixel(cx + offset, y, COL_BRANCH);
        if (offset > 0 && cx + offset + 1 < px + TILE_W) {
          tft.drawPixel(cx + offset + 1, y, COL_BRANCH);
        }
      }
    }
    
    // Branche centrale qui monte
    int centerBranchY = baseBranchY - 4;
    if (centerBranchY >= py + 2) {
      tft.drawFastVLine(cx, centerBranchY, baseBranchY - centerBranchY, COL_BRANCH);
    }
    
    // Petites branches secondaires
    int midY = baseBranchY - 3;
    if (midY >= py + 2 && midY < baseBranchY) {
      tft.drawPixel(cx - 2, midY, COL_BRANCH);
      tft.drawPixel(cx + 2, midY, COL_BRANCH);
      tft.drawPixel(cx - 1, midY - 1, COL_BRANCH);
      tft.drawPixel(cx + 1, midY - 1, COL_BRANCH);
    }
    return;
  }

  if (stage == 2) {
    // Arbuste vert - branches marron + feuillage vert
    int baseBranchY = baseY - 1;
    
    // Branches principales marron (même style que stage 1 mais plus développées)
    // Branche gauche principale
    int leftBranchStartY = baseBranchY;
    int leftBranchEndY = baseBranchY - 8;
    if (leftBranchEndY < py + 2) leftBranchEndY = py + 2;
    for (int y = leftBranchStartY; y >= leftBranchEndY; y--) {
      int offset = (leftBranchStartY - y) / 2;
      if (cx - offset >= px + 1) {
        tft.drawPixel(cx - offset, y, COL_BRANCH);
        if (offset > 0 && cx - offset - 1 >= px) {
          tft.drawPixel(cx - offset - 1, y, COL_BRANCH);
        }
      }
    }
    
    // Branche droite principale
    int rightBranchStartY = baseBranchY;
    int rightBranchEndY = baseBranchY - 8;
    if (rightBranchEndY < py + 2) rightBranchEndY = py + 2;
    for (int y = rightBranchStartY; y >= rightBranchEndY; y--) {
      int offset = (rightBranchStartY - y) / 2;
      if (cx + offset < px + TILE_W - 1) {
        tft.drawPixel(cx + offset, y, COL_BRANCH);
        if (offset > 0 && cx + offset + 1 < px + TILE_W) {
          tft.drawPixel(cx + offset + 1, y, COL_BRANCH);
        }
      }
    }
    
    // Branche centrale
    int centerBranchY = baseBranchY - 6;
    if (centerBranchY >= py + 2) {
      tft.drawFastVLine(cx, centerBranchY, baseBranchY - centerBranchY, COL_BRANCH);
    }
    
    // Petites branches secondaires
    int midY = baseBranchY - 4;
    if (midY >= py + 2 && midY < baseBranchY) {
      tft.drawPixel(cx - 3, midY, COL_BRANCH);
      tft.drawPixel(cx + 3, midY, COL_BRANCH);
      tft.drawPixel(cx - 2, midY - 1, COL_BRANCH);
      tft.drawPixel(cx + 2, midY - 1, COL_BRANCH);
    }
    
    // Feuillage vert (plus grand)
    int foliageCenterY = baseBranchY - 7;
    if (foliageCenterY < py + 2) foliageCenterY = py + 2;
    
    int foliageR = 5;
    int maxR = (TILE_H - (foliageCenterY - py) - 2) / 2;
    if (foliageR > maxR) foliageR = maxR;
    if (foliageR < 3) foliageR = 3;
    
    // Feuillage principal (cercle vert)
    tft.fillCircle(cx, foliageCenterY, foliageR, COL_FOLIAGE);
    
    // Ajouter de la texture au feuillage
    if (foliageR >= 4) {
      tft.drawPixel(cx - 2, foliageCenterY - 2, COL_FOLIAGE);
      tft.drawPixel(cx + 2, foliageCenterY - 2, COL_FOLIAGE);
      tft.drawPixel(cx - 3, foliageCenterY, COL_FOLIAGE);
      tft.drawPixel(cx + 3, foliageCenterY, COL_FOLIAGE);
    }
    return;
  }

  // Stage 3: Arbuste vert avec fruits rouges ronds
  int baseBranchY = baseY - 1;
  
  // Branches principales marron (bien développées)
  // Branche gauche principale
  int leftBranchStartY = baseBranchY;
  int leftBranchEndY = baseBranchY - 10;
  if (leftBranchEndY < py + 2) leftBranchEndY = py + 2;
  for (int y = leftBranchStartY; y >= leftBranchEndY; y--) {
    int offset = (leftBranchStartY - y) / 2;
    if (cx - offset >= px + 1) {
      tft.drawPixel(cx - offset, y, COL_BRANCH);
      if (offset > 0 && cx - offset - 1 >= px) {
        tft.drawPixel(cx - offset - 1, y, COL_BRANCH);
      }
    }
  }
  
  // Branche droite principale
  int rightBranchStartY = baseBranchY;
  int rightBranchEndY = baseBranchY - 10;
  if (rightBranchEndY < py + 2) rightBranchEndY = py + 2;
  for (int y = rightBranchStartY; y >= rightBranchEndY; y--) {
    int offset = (rightBranchStartY - y) / 2;
    if (cx + offset < px + TILE_W - 1) {
      tft.drawPixel(cx + offset, y, COL_BRANCH);
      if (offset > 0 && cx + offset + 1 < px + TILE_W) {
        tft.drawPixel(cx + offset + 1, y, COL_BRANCH);
      }
    }
  }
  
  // Branche centrale
  int centerBranchY = baseBranchY - 8;
  if (centerBranchY >= py + 2) {
    tft.drawFastVLine(cx, centerBranchY, baseBranchY - centerBranchY, COL_BRANCH);
  }
  
  // Petites branches secondaires
  int midY1 = baseBranchY - 5;
  int midY2 = baseBranchY - 3;
  if (midY1 >= py + 2 && midY1 < baseBranchY) {
    tft.drawPixel(cx - 3, midY1, COL_BRANCH);
    tft.drawPixel(cx + 3, midY1, COL_BRANCH);
    tft.drawPixel(cx - 2, midY1 - 1, COL_BRANCH);
    tft.drawPixel(cx + 2, midY1 - 1, COL_BRANCH);
  }
  if (midY2 >= py + 2 && midY2 < baseBranchY) {
    tft.drawPixel(cx - 4, midY2, COL_BRANCH);
    tft.drawPixel(cx + 4, midY2, COL_BRANCH);
  }
  
  // Feuillage vert (grand)
  int foliageCenterY = baseBranchY - 9;
  if (foliageCenterY < py + 2) foliageCenterY = py + 2;
  
  int foliageR = 6;
  int maxR = (TILE_H - (foliageCenterY - py) - 2) / 2;
  if (foliageR > maxR) foliageR = maxR;
  if (foliageR < 4) foliageR = 4;
  
  // Feuillage principal
  tft.fillCircle(cx, foliageCenterY, foliageR, COL_FOLIAGE);
  
  // Texture du feuillage
  if (foliageR >= 4) {
    tft.drawPixel(cx - 3, foliageCenterY - 2, COL_FOLIAGE);
    tft.drawPixel(cx + 3, foliageCenterY - 2, COL_FOLIAGE);
    tft.drawPixel(cx - 4, foliageCenterY, COL_FOLIAGE);
    tft.drawPixel(cx + 4, foliageCenterY, COL_FOLIAGE);
    tft.drawPixel(cx - 2, foliageCenterY + 2, COL_FOLIAGE);
    tft.drawPixel(cx + 2, foliageCenterY + 2, COL_FOLIAGE);
  }
  
  // Fruits rouges ronds sur l'arbuste
  int fruitR = 2; // rayon des fruits
  if (fruitR < 1) fruitR = 1;
  
  // Fruits en haut
  if (foliageCenterY - 3 >= py + 2) {
    tft.fillCircle(cx, foliageCenterY - 3, fruitR, COL_FRUIT);
  }
  
  // Fruits sur les côtés
  if (foliageCenterY - 1 >= py + 2 && foliageCenterY - 1 < py + TILE_H - 2) {
    tft.fillCircle(cx - 3, foliageCenterY - 1, fruitR, COL_FRUIT);
    tft.fillCircle(cx + 3, foliageCenterY - 1, fruitR, COL_FRUIT);
  }
  
  // Fruits au centre et en bas
  if (foliageCenterY + 1 >= py + 2 && foliageCenterY + 1 < py + TILE_H - 2) {
    tft.fillCircle(cx, foliageCenterY + 1, fruitR, COL_FRUIT);
  }
  if (foliageCenterY + 2 >= py + 2 && foliageCenterY + 2 < py + TILE_H - 2) {
    tft.fillCircle(cx - 2, foliageCenterY + 2, fruitR, COL_FRUIT);
    tft.fillCircle(cx + 2, foliageCenterY + 2, fruitR, COL_FRUIT);
  }
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

  // Simple white square for the cat
  const int margin = 2;
  int catSize = (TILE_W < TILE_H) ? TILE_W : TILE_H;
  catSize -= (margin * 2);
  if (catSize < 8) catSize = 8;
  if (catSize > 16) catSize = 16;
  
  int catX = px + (TILE_W - catSize) / 2;
  int catY = py + (TILE_H - catSize) / 2;
  
  tft.fillRect(catX, catY, catSize, catSize, ST77XX_WHITE);
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

void clearProgressBand() {
  tft.fillRect(0, BAR_Y, W, BAR_H, COL_UI_BG);
}

void drawHUD(bool force) {
  drawTopHUD(force);
  drawBottomHUD(force);
  if (force) clearProgressBand();
}

// ================= HARVEST BAR (anti-flicker) =================
void drawHarvestBar(unsigned long now) {
  if (!harvest.active) return;

  // throttle redraw rate
  if (now - lastBarDrawMs < BAR_REFRESH_MS) return;
  lastBarDrawMs = now;

  unsigned long total = harvest.endMs - harvest.startMs;
  unsigned long elapsed = (now > harvest.startMs) ? (now - harvest.startMs) : 0;
  if (elapsed > total) elapsed = total;

  int fill = (int)((elapsed * (unsigned long)(BAR_W - 2)) / total);
  if (fill < 0) fill = 0;
  if (fill > BAR_W - 2) fill = BAR_W - 2;

  if (fill == lastBarFill) return;

  // Only draw delta (new pixels)
  int delta = fill - lastBarFill;
  if (lastBarFill < 0) {
    delta = fill;       // first draw
    lastBarFill = 0;
  }

  if (delta > 0) {
    int innerH = (BAR_H - 4) - 2;
    tft.fillRect(BAR_X + 1 + lastBarFill, BAR_Y + 3, delta, innerH, ST77XX_WHITE);
    lastBarFill = fill;
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
    if (vegs[i].stage < 3) scheduleVegIfNeeded(i, now);

    if (vegs[i].stage < 3 && vegs[i].nextStepMs > 0 && now >= vegs[i].nextStepMs) {
      vegs[i].stage++;
      if (vegs[i].stage < 3) vegs[i].nextStepMs = now + (unsigned long)random(VEG_GROW_MIN_MS, VEG_GROW_MAX_MS);
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

  // reset bar cache
  lastBarFill = -1;
  lastBarDrawMs = 0;

  clearProgressBand();
  tft.drawRect(BAR_X, BAR_Y + 2, BAR_W, BAR_H - 4, COL_WALL);

  // first draw (0%)
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

  // Clear the band and reset cache
  clearProgressBand();
  lastBarFill = -1;
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
    if (vi >= 0 && vegs[vi].stage == 3) {
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
