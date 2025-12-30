#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>

// ---------- OLED ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------- Buttons ----------
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_LEFT   5
#define BTN_RIGHT  4
#define BTN_OK     6

static const int TILE = 8;
static const int GRID_W = 16;
static const int GRID_H = 6;
static const int PLAY_Y0 = 8;
static const int HOUSE_X0 = 0;
static const int HOUSE_X1 = 7;
static const int GARDEN_X0 = 8;
static const int GARDEN_X1 = 15;

static const int DOOR_Y = 3;

static const int VEG_GROW_MIN_MS = 6500;
static const int VEG_GROW_MAX_MS = 10000;

static const int shopX = 15;
static const int shopY = 5;
static const int workshopX = 6;
static const int workshopY = 5;

static const int CHEESE_COST_L = 5;
static const int WORKSHOP_COST_M = 10;
static const int FAST_HARVEST_COST_K = 10;
static const uint8_t CHEESE_MAX_USES = 3;
static const unsigned long CHEESE_BLINK_PERIOD_MS = 180;
static const unsigned long CHEESE_CHECK_EVERY_MS = 900;
static const int CHEESE_SPAWN_CHANCE_PERCENT = 12;
static const int MOUSE_SPEED_SLOW_MS = 420;
static const int MOUSE_SPEED_FAST_MS = 180;
static const unsigned long MOUSE_EAT_PAUSE_MS = 250;
static const unsigned long CHEESE_EAT_FLICKER_MS = 90;

struct Player { int x, y; } michka{3, 2};

int resM = 0; // mice
int resV = 0; // vegetables
int resK = 0; // croquettes

bool hasWorkshop = false;

// Upgrades
bool hasFastHarvest = false;

struct Veg { int x, y; int stage; unsigned long nextStepMs; };
Veg vegs[] = {
  {10, 1, 0, 0},
  {12, 3, 0, 0},
  {14, 2, 0, 0},
  { 9, 4, 0, 0}
};
static const int VEG_COUNT = sizeof(vegs)/sizeof(vegs[0]);

struct CheeseLine {
  bool active;
  uint8_t usesLeft;
  uint8_t usesMax;
};
CheeseLine cheeses[GRID_H];

enum MouseState { HIDDEN, RUNNING, EATING, RETURNING };
struct Mouse {
  MouseState st;
  int x, y;
  int dx;
  unsigned long nextMoveMs;
  unsigned long returnAtMs;
  int moveDelayMs;

  unsigned long eatUntilMs;
} mouseE{HIDDEN, HOUSE_X0, 0, +1, 0, 0, 300, 0};

unsigned long lastInputMs = 0;

static const unsigned long HARVEST_DURATION_MS = 5000;
static const unsigned long HARVEST_DURATION_FAST_MS = 4000;
static const unsigned long RESOURCE_MESSAGE_DURATION_MS = 1500;
static const unsigned long HARVEST_VEG_BLINK_MS = 150;
struct HarvestState {
  bool active;
  int vegIndex;
  unsigned long startTime;
  unsigned long endTime;
  bool showMessage;
  unsigned long messageEndTime;
} harvestState = {false, -1, 0, 0, false, 0};

struct ResourceMessage {
  bool active;
  const char* text;
  unsigned long endTime;
} resourceMessage = {false, nullptr, 0};

static const unsigned long RESOURCE_BLINK_DURATION_MS = 800;
static const unsigned long RESOURCE_BLINK_PERIOD_MS = 100;
struct ResourceBlink {
  bool active;
  unsigned long endTime;
} mouseBlink = {false, 0}, vegBlink = {false, 0}, croqBlink = {false, 0};

enum ShopMenuState { MENU_CLOSED, MENU_OPEN };
ShopMenuState shopMenuState = MENU_CLOSED;
int shopMenuSelection = 0;

static const unsigned long ARROW_DISPLAY_DURATION_MS = 1500;
static const unsigned long ARROW_BLINK_PERIOD_MS = 200;
struct ArrowIndicator {
  bool active;
  int cheeseY;
  unsigned long endTime;
} arrowIndicator = {false, -1, 0};

struct WorkshopArrowIndicator {
  bool active;
  unsigned long endTime;
} workshopArrowIndicator = {false, 0};

enum WorkshopMenuState { WORKSHOP_MENU_CLOSED, WORKSHOP_MENU_OPEN };
WorkshopMenuState workshopMenuState = WORKSHOP_MENU_CLOSED;

enum TitleState { TITLE_SHOWING, TITLE_PLAYING };
TitleState titleState = TITLE_SHOWING;
int titleMenuSelection = 0;

#define SAVE_MAGIC 0x4D494348
#define SAVE_VERSION 1

struct SaveData {
  uint32_t magic;
  uint8_t version;
  // Player
  int8_t michkaX;
  int8_t michkaY;
  // Resources
  int16_t resM;
  int16_t resV;
  int16_t resK;
  // Workshop
  bool hasWorkshop;
  // Upgrades
  bool hasFastHarvest;
  // Veggies
  struct VegSave {
    int8_t x, y;
    int8_t stage;
    uint32_t nextStepMs;
  } vegs[VEG_COUNT];
  // Cheeses
  struct CheeseSave {
    bool active;
    uint8_t usesLeft;
    uint8_t usesMax;
  } cheeses[GRID_H];
  uint32_t checksum;
};

static const int SAVE_INTERVAL_MS = 5000;
unsigned long lastSaveMs = 0;

bool pressed(int pin) { return digitalRead(pin) == LOW; }

bool edgePress(bool current, bool &prev) {
  bool fired = (current && !prev);
  prev = current;
  return fired;
}

int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void seedRandom() { randomSeed(micros()); }

uint32_t calculateChecksum(SaveData* data) {
  uint32_t sum = 0;
  uint8_t* bytes = (uint8_t*)data;
  int size = sizeof(SaveData) - sizeof(data->checksum);
  for (int i = 0; i < size; i++) {
    sum += bytes[i];
  }
  return sum;
}

void saveGame() {
  SaveData save;
  unsigned long now = millis();
  
  save.magic = SAVE_MAGIC;
  save.version = SAVE_VERSION;
  save.michkaX = michka.x;
  save.michkaY = michka.y;
  save.resM = resM;
  save.resV = resV;
  save.resK = resK;
  save.hasWorkshop = hasWorkshop;
  save.hasFastHarvest = hasFastHarvest;
  
  for (int i = 0; i < VEG_COUNT; i++) {
    save.vegs[i].x = vegs[i].x;
    save.vegs[i].y = vegs[i].y;
    save.vegs[i].stage = vegs[i].stage;
    if (vegs[i].nextStepMs > 0 && vegs[i].nextStepMs > now) {
      save.vegs[i].nextStepMs = vegs[i].nextStepMs - now;
    } else {
      save.vegs[i].nextStepMs = 0;
    }
  }
  
  for (int y = 0; y < GRID_H; y++) {
    save.cheeses[y].active = cheeses[y].active;
    save.cheeses[y].usesLeft = cheeses[y].usesLeft;
    save.cheeses[y].usesMax = cheeses[y].usesMax;
  }
  
  save.checksum = calculateChecksum(&save);
  EEPROM.put(0, save);
  EEPROM.commit();
}

bool loadGame() {
  SaveData save;
  EEPROM.get(0, save);
  
  if (save.magic != SAVE_MAGIC || save.version != SAVE_VERSION) {
    return false;
  }
  
  if (save.checksum != calculateChecksum(&save)) {
    return false;
  }
  
  michka.x = save.michkaX;
  michka.y = save.michkaY;
  resM = save.resM;
  resV = save.resV;
  resK = save.resK;
  hasWorkshop = save.hasWorkshop;
  hasFastHarvest = save.hasFastHarvest;
  
  unsigned long now = millis();
  for (int i = 0; i < VEG_COUNT; i++) {
    vegs[i].x = save.vegs[i].x;
    vegs[i].y = save.vegs[i].y;
    vegs[i].stage = save.vegs[i].stage;
    vegs[i].nextStepMs = (save.vegs[i].nextStepMs > 0) ? now + save.vegs[i].nextStepMs : 0;
  }
  
  for (int y = 0; y < GRID_H; y++) {
    cheeses[y].active = save.cheeses[y].active;
    cheeses[y].usesLeft = save.cheeses[y].usesLeft;
    cheeses[y].usesMax = save.cheeses[y].usesMax;
  }
  
  mouseE.st = HIDDEN;
  mouseE.x = HOUSE_X0;
  mouseE.y = 0;
  mouseE.dx = +1;
  mouseE.nextMoveMs = 0;
  mouseE.returnAtMs = 0;
  mouseE.moveDelayMs = 300;
  mouseE.eatUntilMs = 0;
  
  return true;
}

bool hasSaveGame() {
  SaveData save;
  EEPROM.get(0, save);
  
  if (save.magic != SAVE_MAGIC) return false;
  if (save.version != SAVE_VERSION) return false;
  
  uint32_t calculatedChecksum = calculateChecksum(&save);
  return (save.checksum == calculatedChecksum);
}

void resetGame() {
  michka.x = 3;
  michka.y = 2;
  resM = resV = resK = 0;
  hasWorkshop = hasFastHarvest = false;
  
  vegs[0] = {10, 1, 0, 0};
  vegs[1] = {12, 3, 0, 0};
  vegs[2] = {14, 2, 0, 0};
  vegs[3] = { 9, 4, 0, 0};
  
  for (int y = 0; y < GRID_H; y++) {
    cheeses[y] = {false, 0, CHEESE_MAX_USES};
  }
  
  mouseE = {HIDDEN, HOUSE_X0, 0, +1, 0, 0, 300, 0};
  
  shopMenuState = MENU_CLOSED;
  workshopMenuState = WORKSHOP_MENU_CLOSED;
  arrowIndicator = {false, -1, 0};
  workshopArrowIndicator = {false, 0};
  harvestState = {false, -1, 0, 0, false, 0};
  resourceMessage = {false, nullptr, 0};
  mouseBlink = vegBlink = croqBlink = {false, 0};
  
  lastSaveMs = millis();
}

// ---------- Drawing ----------
void drawMichka(int gx, int gy) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  u8g2.drawBox(x + 2, y + 3, 4, 4);      // body
  u8g2.drawPixel(x + 2, y + 2);          // ears
  u8g2.drawPixel(x + 5, y + 2);
}

void drawMouse(int gx, int gy, bool blink) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  if (!blink) return;
  u8g2.drawPixel(x + 3, y + 4);
  u8g2.drawPixel(x + 4, y + 4);
  u8g2.drawPixel(x + 5, y + 5);
}

void drawVeg(int gx, int gy, int stage) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  if (stage == 0) {
    u8g2.drawPixel(x + 4, y + 5);
  } else if (stage == 1) {
    u8g2.drawVLine(x + 4, y + 3, 3);
    u8g2.drawPixel(x + 3, y + 4);
    u8g2.drawPixel(x + 5, y + 4);
  } else {
    u8g2.drawDisc(x + 4, y + 4, 2);
  }
}

// Divider with door opening
void drawDividerWithDoor() {
  int sepX = 8 * TILE;

  // Start from ty=1 to avoid drawing the top line that conflicts with HUD
  for (int ty = 1; ty < GRID_H; ty++) {
    if (ty == DOOR_Y) continue;
    u8g2.drawVLine(sepX, PLAY_Y0 + ty * TILE, TILE);
  }

  int py = PLAY_Y0 + DOOR_Y * TILE;
  u8g2.drawFrame(sepX - 2, py + 2, 4, 4);
}

void drawShop(int gx, int gy) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  // Symbole $ avec police plus grande et lisible
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(x, y + 7, "$");
}

// Cheese icon (wedge + holes)
void drawCheese(int gx, int gy) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  u8g2.drawTriangle(x + 1, y + 6, x + 6, y + 6, x + 6, y + 2);
  u8g2.drawPixel(x + 4, y + 5);
  u8g2.drawPixel(x + 5, y + 4);
}

void drawWorkshop(int gx, int gy) {
  int x = gx * TILE, y = PLAY_Y0 + gy * TILE;
  u8g2.drawBox(x + 1, y + 5, 6, 2);
  u8g2.drawVLine(x + 2, y + 3, 2);
  u8g2.drawVLine(x + 5, y + 3, 2);
  u8g2.drawBox(x + 3, y + 3, 2, 2);
}

void drawArrow(int x, int y, unsigned long now, bool active, unsigned long endTime) {
  if (!active || now >= endTime) return;
  if (((now / ARROW_BLINK_PERIOD_MS) % 2) != 0) return;
  
  u8g2.drawHLine(x + 2, y + 4, 3);
  u8g2.drawPixel(x + 5, y + 3);
  u8g2.drawPixel(x + 6, y + 4);
  u8g2.drawPixel(x + 5, y + 5);
}

void drawArrowIndicator(int cheeseY, unsigned long now) {
  if (arrowIndicator.active && arrowIndicator.cheeseY == cheeseY) {
    drawArrow((HOUSE_X1 - 1) * TILE, PLAY_Y0 + cheeseY * TILE, now, 
              arrowIndicator.active, arrowIndicator.endTime);
    if (now >= arrowIndicator.endTime) arrowIndicator.active = false;
  }
}

void drawWorkshopArrowIndicator(unsigned long now) {
  if (workshopArrowIndicator.active) {
    drawArrow((workshopX - 1) * TILE, PLAY_Y0 + workshopY * TILE, now,
              workshopArrowIndicator.active, workshopArrowIndicator.endTime);
    if (now >= workshopArrowIndicator.endTime) workshopArrowIndicator.active = false;
  }
}

bool isAdjacentTo(int x, int y) {
  int dx = abs(michka.x - x);
  int dy = abs(michka.y - y);
  return (dx <= 1 && dy <= 1 && !(dx == 0 && dy == 0));
}

bool isOnShop() {
  return isAdjacentTo(shopX, shopY);
}

int vegIndexAtPlayer() {
  for (int i = 0; i < VEG_COUNT; i++) {
    if (vegs[i].x == michka.x && vegs[i].y == michka.y) return i;
  }
  return -1;
}

bool isNearMouse() {
  if (mouseE.st == HIDDEN) return false;
  int dx = abs(michka.x - mouseE.x);
  int dy = abs(michka.y - mouseE.y);
  return (dx <= 1 && dy <= 1);
}

int cheeseIndexAtPlayer() {
  if (michka.x != HOUSE_X1) return -1;
  int y = michka.y;
  if (y == DOOR_Y) return -1;
  if (cheeses[y].active) return y;
  return -1;
}

const char* cheeseStatusText(int y) {
  if (!cheeses[y].active) return "";
  if (cheeses[y].usesLeft == cheeses[y].usesMax) return "Fromage: entier";
  if (cheeses[y].usesLeft == 1) return "Fromage: presque fini";
  static char buf[20];
  snprintf(buf, sizeof(buf), "Fromage: %d/%d", cheeses[y].usesLeft, cheeses[y].usesMax);
  return buf;
}

void updateVeggies(unsigned long now) {
  for (int i = 0; i < VEG_COUNT; i++) {
    if (vegs[i].nextStepMs == 0) {
      vegs[i].nextStepMs = now + (unsigned long)random(VEG_GROW_MIN_MS, VEG_GROW_MAX_MS);
    }
    if (now >= vegs[i].nextStepMs) {
      if (vegs[i].stage < 2) vegs[i].stage++;
      if (vegs[i].stage < 2) {
        vegs[i].nextStepMs = now + (unsigned long)random(VEG_GROW_MIN_MS, VEG_GROW_MAX_MS);
      } else {
        vegs[i].nextStepMs = 0;
      }
    }
  }
}

int pickMouseSpeedMs() {
  int r = random(0, 100);
  if (r < 15) return random(MOUSE_SPEED_FAST_MS, MOUSE_SPEED_FAST_MS + 40);
  if (r < 60) return random(260, 380);
  return random(MOUSE_SPEED_SLOW_MS, MOUSE_SPEED_SLOW_MS + 140);
}

bool hasAnyCheese() {
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y) continue;
    if (cheeses[y].active) return true;
  }
  return false;
}

bool hasAvailableCheeseSlots() {
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y || y == workshopY) continue;
    if (!cheeses[y].active) return true;
  }
  return false;
}

void spawnMouseFromCheese(unsigned long now) {
  if (mouseE.st != HIDDEN) return;
  if (!hasAnyCheese()) return;

  static unsigned long nextCheck = 0;
  if (now < nextCheck) return;
  nextCheck = now + CHEESE_CHECK_EVERY_MS;

  int r = random(0, 100);
  if (r >= CHEESE_SPAWN_CHANCE_PERCENT) return;

  int rows[GRID_H];
  int count = 0;
  for (int y = 0; y < GRID_H; y++) {
    if (y != DOOR_Y && cheeses[y].active) rows[count++] = y;
  }
  if (count == 0) return;

  int pick = rows[random(0, count)];

  mouseE.st = RUNNING;
  mouseE.x = HOUSE_X0;
  mouseE.y = pick;
  mouseE.dx = +1;

  mouseE.moveDelayMs = pickMouseSpeedMs();
  mouseE.nextMoveMs = now + (unsigned long)mouseE.moveDelayMs;

  mouseE.returnAtMs = now + (unsigned long)random(6000, 11000);
  mouseE.eatUntilMs = 0;
}

int buyCheese(unsigned long now) {
  (void)now;
  if (resV < CHEESE_COST_L) return -1;

  int availableRows[GRID_H];
  int count = 0;
  for (int y = 0; y < GRID_H; y++) {
    if (y != DOOR_Y && y != workshopY && !cheeses[y].active) {
      availableRows[count++] = y;
    }
  }
  
  if (count == 0) return -1;
  
  int chosenY = availableRows[random(0, count)];
  resV -= CHEESE_COST_L;
  cheeses[chosenY] = {true, CHEESE_MAX_USES, CHEESE_MAX_USES};
  return chosenY;
}

bool buyWorkshop() {
  if (hasWorkshop || resM < WORKSHOP_COST_M) return false;
  resM -= WORKSHOP_COST_M;
  hasWorkshop = true;
  return true;
}

bool buyFastHarvest() {
  if (hasFastHarvest || resK < FAST_HARVEST_COST_K) return false;
  resK -= FAST_HARVEST_COST_K;
  hasFastHarvest = true;
  return true;
}

static const int CROQUETTE_COST_M = 4;
static const int CROQUETTE_COST_L = 2;
bool craftCroquettes(unsigned long now) {
  if (!hasWorkshop || resM < CROQUETTE_COST_M || resV < CROQUETTE_COST_L) return false;
  resM -= CROQUETTE_COST_M;
  resV -= CROQUETTE_COST_L;
  resK += 1;
  resourceMessage = {true, "+1 paqu. de croquettes", now + RESOURCE_MESSAGE_DURATION_MS};
  croqBlink = {true, now + RESOURCE_BLINK_DURATION_MS};
  return true;
}

bool isOnWorkshop() {
  return hasWorkshop && isAdjacentTo(workshopX, workshopY);
}

void consumeCheeseAtRow(int y) {
  if (y < 0 || y >= GRID_H) return;
  if (y == DOOR_Y) return;
  if (!cheeses[y].active) return;

  if (cheeses[y].usesLeft > 0) cheeses[y].usesLeft--;
  if (cheeses[y].usesLeft == 0) cheeses[y].active = false;
}

void updateHarvest(unsigned long now) {
  if (harvestState.active && now >= harvestState.endTime) {
    if (harvestState.vegIndex >= 0 && harvestState.vegIndex < VEG_COUNT) {
      resV += 1;
      vegs[harvestState.vegIndex].stage = 0;
      vegs[harvestState.vegIndex].nextStepMs = 0;
    }
    harvestState.showMessage = true;
    harvestState.messageEndTime = now + RESOURCE_MESSAGE_DURATION_MS;
    resourceMessage = {true, "+ 1 legume !", now + RESOURCE_MESSAGE_DURATION_MS};
    vegBlink = {true, now + RESOURCE_BLINK_DURATION_MS};
    harvestState.active = false;
    harvestState.vegIndex = -1;
  }
  
  if (harvestState.showMessage && now >= harvestState.messageEndTime) {
    harvestState.showMessage = false;
  }
  if (resourceMessage.active && now >= resourceMessage.endTime) {
    resourceMessage = {false, nullptr, 0};
  }
  if (mouseBlink.active && now >= mouseBlink.endTime) mouseBlink.active = false;
  if (vegBlink.active && now >= vegBlink.endTime) vegBlink.active = false;
  if (croqBlink.active && now >= croqBlink.endTime) croqBlink.active = false;
}

void updateMouse(unsigned long now) {
  spawnMouseFromCheese(now);
  if (mouseE.st == HIDDEN) return;

  // If mouse is eating, wait, then consume and flee
  if (mouseE.st == EATING) {
    if (now >= mouseE.eatUntilMs) {
      consumeCheeseAtRow(mouseE.y);
      mouseE.st = RETURNING;
      mouseE.dx = -1;
      mouseE.nextMoveMs = now + (unsigned long)mouseE.moveDelayMs; // resume movement
    }
    return; // no movement while eating
  }

  if (mouseE.st == RUNNING && now >= mouseE.returnAtMs) {
    mouseE.st = RETURNING;
  }

  if (now < mouseE.nextMoveMs) return;
  mouseE.nextMoveMs = now + (unsigned long)mouseE.moveDelayMs;

  if (mouseE.st == RETURNING) mouseE.dx = -1;

  int oldX = mouseE.x;
  int nx = mouseE.x + mouseE.dx;

  // boundaries in house: x in [0..7]
  if (nx < HOUSE_X0) {
    mouseE.st = HIDDEN;
    mouseE.x = HOUSE_X0;
    return;
  } else if (nx > HOUSE_X1) {
    nx = HOUSE_X1;
    mouseE.dx = -1;
    mouseE.st = RETURNING;
  }

  mouseE.x = nx;

  // If it just reached the cheese tile while RUNNING, start eating pause
  if (mouseE.st == RUNNING && oldX != HOUSE_X1 && mouseE.x == HOUSE_X1) {
    int y = mouseE.y;
    if (y >= 0 && y < GRID_H && y != DOOR_Y && cheeses[y].active) {
      mouseE.st = EATING;
      mouseE.eatUntilMs = now + MOUSE_EAT_PAUSE_MS;
      // Stay on the cheese tile during pause
    } else {
      mouseE.st = RETURNING;
      mouseE.dx = -1;
    }
  }
}

void handleOkAction(unsigned long now) {
  if (isNearMouse()) {
    resM += 1;
    mouseE.st = HIDDEN;
    resourceMessage = {true, "+ 1 souris", now + RESOURCE_MESSAGE_DURATION_MS};
    mouseBlink = {true, now + RESOURCE_BLINK_DURATION_MS};
    return;
  }

  int vi = vegIndexAtPlayer();
  if (vi >= 0 && vegs[vi].stage == 2 && !harvestState.active) {
    unsigned long duration = hasFastHarvest ? HARVEST_DURATION_FAST_MS : HARVEST_DURATION_MS;
    harvestState = {true, vi, now, now + duration, false, 0};
    return;
  }

  if (isOnShop() && shopMenuState == MENU_CLOSED) {
    shopMenuState = MENU_OPEN;
    shopMenuSelection = 0;
    return;
  }

  if (isOnWorkshop() && workshopMenuState == WORKSHOP_MENU_CLOSED) {
    workshopMenuState = WORKSHOP_MENU_OPEN;
    return;
  }
}

const char* contextMessage() {
  if (isNearMouse()) return "OK: chasser";

  int ci = cheeseIndexAtPlayer();
  if (ci >= 0) return cheeseStatusText(ci);

  int vi = vegIndexAtPlayer();
  if (vi >= 0 && vegs[vi].stage == 2) return "OK: recolter";

  if (isOnShop() && shopMenuState == MENU_CLOSED) return "OK: ouvrir magasin";

  if (isOnWorkshop() && workshopMenuState == WORKSHOP_MENU_CLOSED) return "OK: ouvrir atelier";

  static char tutorialMsg[32];
  if (resK > 0) {
    return "Continue comme ca";
  }
  if (hasAnyCheese() && resM == 0) {
    return "Attrappe une souris";
  }
  if (resM > 0) {
    return "Fabrique des croket'";
  }
  if (resV < CHEESE_COST_L) {
    snprintf(tutorialMsg, sizeof(tutorialMsg), "Recolte %d legumes", CHEESE_COST_L);
    return tutorialMsg;
  }
  return "Achete un fromage";
}

void drawTitleScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(15, 18, "La Maison");
  u8g2.drawStr(20, 32, "de Michka");
  
  int titleX = 100, titleY = 8;
  u8g2.drawBox(titleX + 2, titleY + 3, 4, 4);
  u8g2.drawPixel(titleX + 2, titleY + 2);
  u8g2.drawPixel(titleX + 5, titleY + 2);
  
  u8g2.setFont(u8g2_font_6x10_tf);
  if (titleMenuSelection == 0) {
    u8g2.drawStr(8, 48, "> Nouveau");
  } else {
    u8g2.drawStr(18, 48, "Nouveau");
  }
  
  if (hasSaveGame()) {
    if (titleMenuSelection == 1) {
      u8g2.drawStr(8, 58, "> Continuer");
    } else {
      u8g2.drawStr(18, 58, "Continuer");
    }
  }
  
  u8g2.sendBuffer();
}

void drawHUD(unsigned long now) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);
  char hud[32];
  
  bool blinkS = mouseBlink.active && ((now / RESOURCE_BLINK_PERIOD_MS) % 2) == 1;
  bool blinkL = vegBlink.active && ((now / RESOURCE_BLINK_PERIOD_MS) % 2) == 1;
  
  snprintf(hud, sizeof(hud), "S:%3d L:%3d", resM, resV);
  u8g2.drawStr(0, 10, hud);
  
  if (blinkS) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(12, 1, 18, 9);
    u8g2.setDrawColor(1);
  }
  if (blinkL) {
    u8g2.setDrawColor(0);
    u8g2.drawBox(48, 1, 18, 9);
    u8g2.setDrawColor(1);
  }
  
  if (hasWorkshop) {
    bool blinkCroq = croqBlink.active && ((now / RESOURCE_BLINK_PERIOD_MS) % 2) == 1;
    snprintf(hud, sizeof(hud), "C:%3d", resK);
    u8g2.drawStr(80, 10, hud);
    if (blinkCroq) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(92, 1, 18, 9);
      u8g2.setDrawColor(1);
    }
  }
}

void drawShopMenu(unsigned long now) {
  drawHUD(now);
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 12, 128, 52);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(0);
  u8g2.drawStr(45, 24, "MAGASIN");
  
  char buf0[20];
  if (hasAvailableCheeseSlots()) {
    snprintf(buf0, sizeof(buf0), "Fromage %dL", CHEESE_COST_L);
  } else {
    snprintf(buf0, sizeof(buf0), "Fromage MAX");
  }
  
  char buf1[20];
  if (hasWorkshop) {
    snprintf(buf1, sizeof(buf1), "Atelier OK");
  } else {
    snprintf(buf1, sizeof(buf1), "Atelier %dS", WORKSHOP_COST_M);
  }
  
  char buf2[25];
  if (hasFastHarvest) {
    snprintf(buf2, sizeof(buf2), "Recolte rapide OK");
  } else {
    snprintf(buf2, sizeof(buf2), "Recolte rapide %dC", FAST_HARVEST_COST_K);
  }
  
  if (shopMenuSelection == 0) {
    u8g2.drawStr(5, 40, ">");
    u8g2.drawStr(15, 40, buf0);
    u8g2.drawStr(15, 54, buf1);
  } else if (shopMenuSelection == 1) {
    u8g2.drawStr(15, 40, buf0);
    u8g2.drawStr(5, 54, ">");
    u8g2.drawStr(15, 54, buf1);
  } else {
    u8g2.drawStr(15, 40, buf1);
    u8g2.drawStr(5, 54, ">");
    char buf2short[20];
    snprintf(buf2short, sizeof(buf2short), hasFastHarvest ? "Recolte rapide OK" : "Recolte rap. %dC", FAST_HARVEST_COST_K);
    u8g2.drawStr(15, 54, buf2short);
  }
  
  if (shopMenuSelection < 2) {
    u8g2.drawStr(120, 64, "v");
  }
  
  u8g2.setDrawColor(1);
}

void drawWorkshopMenu(unsigned long now) {
  drawHUD(now);
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 12, 128, 52);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(0);
  u8g2.drawStr(45, 24, "ATELIER");
  
  char buf[20];
  snprintf(buf, sizeof(buf), "Croquette %dS %dL", CROQUETTE_COST_M, CROQUETTE_COST_L);
  u8g2.drawStr(5, 40, ">");
  u8g2.drawStr(15, 40, buf);
  u8g2.setDrawColor(1);
}

void render(unsigned long now) {
  if (titleState == TITLE_SHOWING) {
    drawTitleScreen();
    return;
  }
  
  u8g2.clearBuffer();

  drawHUD(now);
  drawDividerWithDoor();

  // shop
  drawShop(shopX, shopY);

  // veggies
  for (int i = 0; i < VEG_COUNT; i++) {
    // Blink veg if being harvested
    bool shouldDraw = true;
    if (harvestState.active && harvestState.vegIndex == i && vegs[i].stage == 2) {
      shouldDraw = ((now / HARVEST_VEG_BLINK_MS) % 2) == 0;
    }
    if (shouldDraw) {
      drawVeg(vegs[i].x, vegs[i].y, vegs[i].stage);
    }
  }

  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y || !cheeses[y].active) continue;

    bool drawIt = true;
    if (mouseE.st == EATING && mouseE.y == y) {
      drawIt = ((now / CHEESE_EAT_FLICKER_MS) % 2) == 0;
    } else if (cheeses[y].usesLeft == 1) {
      drawIt = ((now / CHEESE_BLINK_PERIOD_MS) % 2) == 0;
    }

    if (drawIt) drawCheese(HOUSE_X1, y);
    drawArrowIndicator(y, now);
  }

  if (hasWorkshop) {
    drawWorkshop(workshopX, workshopY);
    drawWorkshopArrowIndicator(now);
  }

  if (mouseE.st != HIDDEN) {
    bool mouseVisible = (mouseE.st == EATING) || ((now / (unsigned long)clampi(mouseE.moveDelayMs, 160, 500)) % 2) == 0;
    drawMouse(mouseE.x, mouseE.y, mouseVisible);
  }

  drawMichka(michka.x, michka.y);

  if (harvestState.active) {
    unsigned long elapsed = now - harvestState.startTime;
    unsigned long total = harvestState.endTime - harvestState.startTime;
    int progress = clampi((int)((elapsed * 100) / total), 0, 100);
    
    int barX = 4, barY = 60, barW = 120, barH = 4;
    u8g2.setDrawColor(1);
    u8g2.drawFrame(barX, barY, barW, barH);
    int fillW = (progress * (barW - 2)) / 100;
    if (fillW > 0) {
      u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);
    }
  }
  
  if (resourceMessage.active && resourceMessage.text != nullptr) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setDrawColor(1);
    u8g2.drawStr(0, 64, resourceMessage.text);
  }
  
  if (shopMenuState == MENU_OPEN) {
    drawShopMenu(now);
  } else if (workshopMenuState == WORKSHOP_MENU_OPEN) {
    drawWorkshopMenu(now);
  } else if (!harvestState.active && !harvestState.showMessage && !resourceMessage.active) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 64, contextMessage());
  }

  u8g2.sendBuffer();
}

// ---------- Input ----------
struct BtnState { bool prev = false; };
BtnState bUp, bDown, bLeft, bRight, bOk;

void updateInput(unsigned long now) {
  const unsigned long MOVE_COOLDOWN = 120;
  bool canMove = (now - lastInputMs) >= MOVE_COOLDOWN;
  
  // Prevent movement during harvest
  if (harvestState.active) {
    canMove = false;
  }

  bool up = pressed(BTN_UP);
  bool down = pressed(BTN_DOWN);
  bool left = pressed(BTN_LEFT);
  bool right = pressed(BTN_RIGHT);
  bool ok = pressed(BTN_OK);

  bool upEdge = edgePress(up, bUp.prev);
  bool downEdge = edgePress(down, bDown.prev);
  bool leftEdge = edgePress(left, bLeft.prev);
  bool rightEdge = edgePress(right, bRight.prev);
  bool okEdge = edgePress(ok, bOk.prev);

  if (titleState == TITLE_SHOWING) {
    bool saveExists = hasSaveGame();
    if (!saveExists && titleMenuSelection > 0) {
      titleMenuSelection = 0;
    }
    
    if (canMove && (upEdge || downEdge)) {
      int maxOptions = saveExists ? 2 : 1;
      titleMenuSelection = (titleMenuSelection + 1) % maxOptions;
      lastInputMs = now;
    }
    if (okEdge) {
      lastInputMs = now;
      if (titleMenuSelection == 0) {
        resetGame();
        titleState = TITLE_PLAYING;
      } else if (titleMenuSelection == 1 && saveExists && loadGame()) {
        titleState = TITLE_PLAYING;
      }
    }
    return;
  }

  if (shopMenuState == MENU_OPEN) {
    if (canMove) {
      if (upEdge) {
        shopMenuSelection = (shopMenuSelection - 1 + 3) % 3;
        lastInputMs = now;
      }
      if (downEdge) {
        shopMenuSelection = (shopMenuSelection + 1) % 3;
        lastInputMs = now;
      }
      if (leftEdge) {
        shopMenuState = MENU_CLOSED;
        lastInputMs = now;
        return;
      }
    }
    if (okEdge) {
      lastInputMs = now;
      if (shopMenuSelection == 0) {
        int cheeseY = buyCheese(now);
        if (cheeseY >= 0) {
          shopMenuState = MENU_CLOSED;
          arrowIndicator = {true, cheeseY, now + ARROW_DISPLAY_DURATION_MS};
        }
      } else if (shopMenuSelection == 1) {
        if (buyWorkshop()) {
          shopMenuState = MENU_CLOSED;
          workshopArrowIndicator = {true, now + ARROW_DISPLAY_DURATION_MS};
        }
      } else if (shopMenuSelection == 2) {
        if (buyFastHarvest()) {
          shopMenuState = MENU_CLOSED;
        }
      }
    }
    return;
  }

  if (workshopMenuState == WORKSHOP_MENU_OPEN) {
    if (leftEdge) {
      workshopMenuState = WORKSHOP_MENU_CLOSED;
      lastInputMs = now;
      return;
    }
    if (okEdge) {
      lastInputMs = now;
      if (craftCroquettes(now)) {
        workshopMenuState = WORKSHOP_MENU_CLOSED;
      }
    }
    return;
  }

  if (canMove) {
    if (upEdge) {
      int ny = michka.y - 1;
      if (!(ny == shopY && michka.x == shopX) && !(hasWorkshop && ny == workshopY && michka.x == workshopX)) {
        michka.y = clampi(ny, 0, GRID_H - 1);
        lastInputMs = now;
      }
    }
    if (downEdge) {
      int ny = michka.y + 1;
      if (!(ny == shopY && michka.x == shopX) && !(hasWorkshop && ny == workshopY && michka.x == workshopX)) {
        michka.y = clampi(ny, 0, GRID_H - 1);
        lastInputMs = now;
      }
    }

    if (leftEdge) {
      int nx = michka.x - 1;
      if (!(michka.x == 8 && nx == 7 && michka.y != DOOR_Y) && 
          !(nx == shopX && michka.y == shopY) && 
          !(hasWorkshop && nx == workshopX && michka.y == workshopY)) {
        michka.x = clampi(nx, 0, GRID_W - 1);
        lastInputMs = now;
      }
    }

    if (rightEdge) {
      int nx = michka.x + 1;
      if (!(michka.x == 7 && nx == 8 && michka.y != DOOR_Y) && 
          !(nx == shopX && michka.y == shopY) && 
          !(hasWorkshop && nx == workshopX && michka.y == workshopY)) {
        michka.x = clampi(nx, 0, GRID_W - 1);
        lastInputMs = now;
      }
    }
  }

  if (okEdge) {
    lastInputMs = now;
    handleOkAction(now);
  }
}

void setup() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  Wire.setSDA(0);
  Wire.setSCL(1);
  Wire.begin();
  Wire.setClock(100000);

  EEPROM.begin(512);
  seedRandom();
  u8g2.begin();

  for (int y = 0; y < GRID_H; y++) {
    cheeses[y] = {false, 0, CHEESE_MAX_USES};
  }

  titleState = TITLE_SHOWING;
  titleMenuSelection = 0;
  lastSaveMs = millis();
}

void loop() {
  unsigned long now = millis();
  updateInput(now);
  
  if (titleState == TITLE_PLAYING) {
    updateHarvest(now);
    updateMouse(now);
    updateVeggies(now);
    if (now - lastSaveMs >= SAVE_INTERVAL_MS) {
      saveGame();
      lastSaveMs = now;
    }
  }

  render(now);
  delay(10);
}
