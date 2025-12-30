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

// ---------- Grid ----------
static const int TILE = 8;
static const int GRID_W = 16;
static const int GRID_H = 6;      // 6 tiles high (48 px)
static const int PLAY_Y0 = 8;     // start y for play area
static const int HOUSE_X0 = 0;
static const int HOUSE_X1 = 7;
static const int GARDEN_X0 = 8;
static const int GARDEN_X1 = 15;

// Door in the wall between house and garden
static const int DOOR_Y = 3;      // 0..GRID_H-1

static const int VEG_GROW_MIN_MS = 6500;
static const int VEG_GROW_MAX_MS = 10000;

// Shop
static const int shopX = 15;
static const int shopY = 5;

// Workshop position in house (bas à droite)
static const int workshopX = 6;
static const int workshopY = 5;

// Cheese (uses-based)
static const int CHEESE_COST_L = 5;             // cost in vegetables
static const int WORKSHOP_COST_M = 10;          // cost in mice
static const uint8_t CHEESE_MAX_USES = 3;       // 3 "morsures"
static const unsigned long CHEESE_BLINK_PERIOD_MS = 180;

// Mouse spawn driven by cheese
static const unsigned long CHEESE_CHECK_EVERY_MS = 900; // check ~1/sec
static const int CHEESE_SPAWN_CHANCE_PERCENT = 12;      // "parfois"
static const int MOUSE_SPEED_SLOW_MS = 420;
static const int MOUSE_SPEED_FAST_MS = 180;

// Mouse eating pause
static const unsigned long MOUSE_EAT_PAUSE_MS = 250;
// Extra "miam" blink while eating (cheese flickers)
static const unsigned long CHEESE_EAT_FLICKER_MS = 90;

// ---------- Game state ----------
struct Player { int x, y; } michka{3, 2};

int resM = 0; // souris
int resV = 0; // legumes
int resK = 0; // croquettes

// Workshop (atelier)
bool hasWorkshop = false;

// Veggies (garden plots)
struct Veg { int x, y; int stage; unsigned long nextStepMs; };
Veg vegs[] = {
  {10, 1, 0, 0},
  {12, 3, 0, 0},
  {14, 2, 0, 0},
  { 9, 4, 0, 0}
};
static const int VEG_COUNT = sizeof(vegs)/sizeof(vegs[0]);

// Cheese placed in the house (one per row max, except DOOR_Y)
struct CheeseLine {
  bool active;
  uint8_t usesLeft;
  uint8_t usesMax;
};
CheeseLine cheeses[GRID_H];

// Mouse entity
enum MouseState { HIDDEN, RUNNING, EATING, RETURNING };
struct Mouse {
  MouseState st;
  int x, y;
  int dx; // +1 or -1
  unsigned long nextMoveMs;
  unsigned long returnAtMs;
  int moveDelayMs;

  // For EATING state
  unsigned long eatUntilMs;
} mouseE{HIDDEN, HOUSE_X0, 0, +1, 0, 0, 300, 0};

// UI
unsigned long lastInputMs = 0;

// Shop menu
enum ShopMenuState { MENU_CLOSED, MENU_OPEN };
ShopMenuState shopMenuState = MENU_CLOSED;
int shopMenuSelection = 0; // 0 = Fromage, 1 = Atelier

// Arrow indicator for newly placed cheese
static const unsigned long ARROW_DISPLAY_DURATION_MS = 1500; // 1.5 seconds
static const unsigned long ARROW_BLINK_PERIOD_MS = 200; // blink every 200ms
struct ArrowIndicator {
  bool active;
  int cheeseY; // row where cheese was placed
  unsigned long endTime;
} arrowIndicator = {false, -1, 0};

// Arrow indicator for newly placed workshop
struct WorkshopArrowIndicator {
  bool active;
  unsigned long endTime;
} workshopArrowIndicator = {false, 0};

// Workshop menu
enum WorkshopMenuState { WORKSHOP_MENU_CLOSED, WORKSHOP_MENU_OPEN };
WorkshopMenuState workshopMenuState = WORKSHOP_MENU_CLOSED;

// Title screen
enum TitleState { TITLE_SHOWING, TITLE_PLAYING };
TitleState titleState = TITLE_SHOWING;
int titleMenuSelection = 0; // 0 = Nouveau, 1 = Continuer

// ---------- Save system ----------
// Magic number to verify save validity
#define SAVE_MAGIC 0x4D494348  // "MICH" en hex
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
  // Checksum (simple sum)
  uint32_t checksum;
};

static const int SAVE_INTERVAL_MS = 5000; // Sauvegarder toutes les 5 secondes
unsigned long lastSaveMs = 0;

// ---------- Helpers ----------
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

// ---------- Save/Load ----------
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
  unsigned long now = millis(); // Capturer le temps une seule fois
  
  save.magic = SAVE_MAGIC;
  save.version = SAVE_VERSION;
  
  // Player
  save.michkaX = michka.x;
  save.michkaY = michka.y;
  
  // Resources
  save.resM = resM;
  save.resV = resV;
  save.resK = resK;
  
  // Workshop
  save.hasWorkshop = hasWorkshop;
  
  // Veggies
  for (int i = 0; i < VEG_COUNT; i++) {
    save.vegs[i].x = vegs[i].x;
    save.vegs[i].y = vegs[i].y;
    save.vegs[i].stage = vegs[i].stage;
    // Sauvegarder le temps relatif au lieu du temps absolu
    if (vegs[i].nextStepMs > 0 && vegs[i].nextStepMs > now) {
      save.vegs[i].nextStepMs = vegs[i].nextStepMs - now;
    } else {
      save.vegs[i].nextStepMs = 0;
    }
  }
  
  // Cheeses
  for (int y = 0; y < GRID_H; y++) {
    save.cheeses[y].active = cheeses[y].active;
    save.cheeses[y].usesLeft = cheeses[y].usesLeft;
    save.cheeses[y].usesMax = cheeses[y].usesMax;
  }
  
  // Calculate checksum
  save.checksum = calculateChecksum(&save);
  
  // Write to EEPROM
  EEPROM.put(0, save);
  EEPROM.commit();
}

bool loadGame() {
  SaveData save;
  EEPROM.get(0, save);
  
  // Verify magic number
  if (save.magic != SAVE_MAGIC) {
    return false;
  }
  
  // Verify version
  if (save.version != SAVE_VERSION) {
    return false;
  }
  
  // Verify checksum
  uint32_t calculatedChecksum = calculateChecksum(&save);
  if (save.checksum != calculatedChecksum) {
    return false;
  }
  
  // Load player
  michka.x = save.michkaX;
  michka.y = save.michkaY;
  
  // Load resources
  resM = save.resM;
  resV = save.resV;
  resK = save.resK;
  
  // Load workshop
  hasWorkshop = save.hasWorkshop;
  
  // Load veggies
  unsigned long now = millis();
  for (int i = 0; i < VEG_COUNT; i++) {
    vegs[i].x = save.vegs[i].x;
    vegs[i].y = save.vegs[i].y;
    vegs[i].stage = save.vegs[i].stage;
    if (save.vegs[i].nextStepMs > 0) {
      vegs[i].nextStepMs = now + save.vegs[i].nextStepMs;
    } else {
      vegs[i].nextStepMs = 0;
    }
  }
  
  // Load cheeses
  for (int y = 0; y < GRID_H; y++) {
    cheeses[y].active = save.cheeses[y].active;
    cheeses[y].usesLeft = save.cheeses[y].usesLeft;
    cheeses[y].usesMax = save.cheeses[y].usesMax;
  }
  
  // Reset mouse (on ne sauvegarde pas l'état de la souris)
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
  // Réinitialiser toutes les valeurs par défaut
  michka.x = 3;
  michka.y = 2;
  resM = 0;
  resV = 0;
  resK = 0;
  hasWorkshop = false;
  
  // Réinitialiser les légumes
  vegs[0].x = 10; vegs[0].y = 1; vegs[0].stage = 0; vegs[0].nextStepMs = 0;
  vegs[1].x = 12; vegs[1].y = 3; vegs[1].stage = 0; vegs[1].nextStepMs = 0;
  vegs[2].x = 14; vegs[2].y = 2; vegs[2].stage = 0; vegs[2].nextStepMs = 0;
  vegs[3].x =  9; vegs[3].y = 4; vegs[3].stage = 0; vegs[3].nextStepMs = 0;
  
  // Réinitialiser les fromages
  for (int y = 0; y < GRID_H; y++) {
    cheeses[y].active = false;
    cheeses[y].usesMax = CHEESE_MAX_USES;
    cheeses[y].usesLeft = 0;
  }
  
  // Réinitialiser la souris
  mouseE.st = HIDDEN;
  mouseE.x = HOUSE_X0;
  mouseE.y = 0;
  mouseE.dx = +1;
  mouseE.nextMoveMs = 0;
  mouseE.returnAtMs = 0;
  mouseE.moveDelayMs = 300;
  mouseE.eatUntilMs = 0;
  
  // Réinitialiser les menus
  shopMenuState = MENU_CLOSED;
  workshopMenuState = WORKSHOP_MENU_CLOSED;
  
  // Réinitialiser les indicateurs de flèche
  arrowIndicator.active = false;
  arrowIndicator.cheeseY = -1;
  arrowIndicator.endTime = 0;
  workshopArrowIndicator.active = false;
  workshopArrowIndicator.endTime = 0;
  
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

  for (int ty = 0; ty < GRID_H; ty++) {
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

// Workshop icon (table simple et lisible)
void drawWorkshop(int gx, int gy) {
  int x = gx * TILE;
  int y = PLAY_Y0 + gy * TILE;
  // Plateau de la table (horizontal)
  u8g2.drawBox(x + 1, y + 5, 6, 2);
  // Pieds de la table (2 supports verticaux)
  u8g2.drawVLine(x + 2, y + 3, 2);
  u8g2.drawVLine(x + 5, y + 3, 2);
  // Objet sur la table (petit rectangle)
  u8g2.drawBox(x + 3, y + 3, 2, 2);
}

// Draw arrow indicator pointing to newly placed cheese
void drawArrowIndicator(int cheeseY, unsigned long now) {
  if (!arrowIndicator.active || arrowIndicator.cheeseY != cheeseY) return;
  if (now >= arrowIndicator.endTime) {
    arrowIndicator.active = false;
    return;
  }
  
  // Blink the arrow
  bool visible = ((now / ARROW_BLINK_PERIOD_MS) % 2) == 0;
  if (!visible) return;
  
  int x = (HOUSE_X1 - 1) * TILE; // Position à gauche du fromage (x=6)
  int y = PLAY_Y0 + cheeseY * TILE;
  
  // Draw arrow "->" simple et lisible
  // Ligne horizontale
  u8g2.drawHLine(x + 2, y + 4, 3);
  // Pointe de flèche (triangle simple)
  u8g2.drawPixel(x + 5, y + 3);
  u8g2.drawPixel(x + 6, y + 4);
  u8g2.drawPixel(x + 5, y + 5);
}

// Draw arrow indicator pointing to newly placed workshop
void drawWorkshopArrowIndicator(unsigned long now) {
  if (!workshopArrowIndicator.active) return;
  if (now >= workshopArrowIndicator.endTime) {
    workshopArrowIndicator.active = false;
    return;
  }
  
  // Blink the arrow
  bool visible = ((now / ARROW_BLINK_PERIOD_MS) % 2) == 0;
  if (!visible) return;
  
  int x = (workshopX - 1) * TILE; // Position à gauche de l'atelier (x=5)
  int y = PLAY_Y0 + workshopY * TILE;
  
  // Draw arrow "->" simple et lisible
  // Ligne horizontale
  u8g2.drawHLine(x + 2, y + 4, 3);
  // Pointe de flèche (triangle simple)
  u8g2.drawPixel(x + 5, y + 3);
  u8g2.drawPixel(x + 6, y + 4);
  u8g2.drawPixel(x + 5, y + 5);
}

// ---------- Logic ----------
bool isOnShop() { 
  // Vérifier si on est adjacent au magasin (pas dessus)
  int dx = abs(michka.x - shopX);
  int dy = abs(michka.y - shopY);
  return (dx <= 1 && dy <= 1 && !(dx == 0 && dy == 0));
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

// Check if there are available slots for placing cheese
bool hasAvailableCheeseSlots() {
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y) continue;           // Pas devant la porte
    if (y == workshopY) continue;        // Pas sur la ligne de l'atelier
    if (!cheeses[y].active) {            // Seulement les lignes libres
      return true;
    }
  }
  return false;
}

// Spawn a mouse ONLY if at least one cheese is active
void spawnMouseFromCheese(unsigned long now) {
  if (mouseE.st != HIDDEN) return;
  if (!hasAnyCheese()) return;

  static unsigned long nextCheck = 0;
  if (now < nextCheck) return;
  nextCheck = now + CHEESE_CHECK_EVERY_MS;

  int r = random(0, 100);
  if (r >= CHEESE_SPAWN_CHANCE_PERCENT) return;

  // gather active rows
  int rows[GRID_H];
  int count = 0;
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y) continue;
    if (cheeses[y].active) rows[count++] = y;
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

// Buy cheese: place it randomly on an available house row (x=7), excluding DOOR_Y and workshop row
// Returns the Y position of the placed cheese, or -1 if purchase failed
int buyCheese(unsigned long now) {
  (void)now;
  if (resV < CHEESE_COST_L) return -1;

  // Rassembler toutes les lignes disponibles (pas la porte, pas l'atelier)
  int availableRows[GRID_H];
  int count = 0;
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y) continue;           // Pas devant la porte
    if (y == workshopY) continue;        // Pas sur la ligne de l'atelier
    if (!cheeses[y].active) {            // Seulement les lignes libres
      availableRows[count++] = y;
    }
  }
  
  if (count == 0) return -1; // Aucune ligne disponible
  
  // Choisir aléatoirement parmi les lignes disponibles
  int chosenY = availableRows[random(0, count)];
  
  resV -= CHEESE_COST_L;
  cheeses[chosenY].active = true;
  cheeses[chosenY].usesMax = CHEESE_MAX_USES;
  cheeses[chosenY].usesLeft = CHEESE_MAX_USES;
  return chosenY;
}

// Buy workshop
bool buyWorkshop() {
  if (hasWorkshop) return false; // Déjà possédé
  if (resM < WORKSHOP_COST_M) return false;
  
  resM -= WORKSHOP_COST_M;
  hasWorkshop = true;
  return true;
}

// Craft croquettes (nécessite l'atelier)
// Coût: 4 souris et 2 légumes pour 1 croquette
static const int CROQUETTE_COST_M = 4;
static const int CROQUETTE_COST_L = 2;
bool craftCroquettes() {
  if (!hasWorkshop) return false;
  if (resM < CROQUETTE_COST_M) return false;
  if (resV < CROQUETTE_COST_L) return false;
  resM -= CROQUETTE_COST_M;
  resV -= CROQUETTE_COST_L;
  resK += 1;
  return true;
}

bool isOnWorkshop() {
  if (!hasWorkshop) return false;
  // Vérifier si on est adjacent à l'atelier (pas dessus)
  int dx = abs(michka.x - workshopX);
  int dy = abs(michka.y - workshopY);
  return (dx <= 1 && dy <= 1 && !(dx == 0 && dy == 0));
}

void consumeCheeseAtRow(int y) {
  if (y < 0 || y >= GRID_H) return;
  if (y == DOOR_Y) return;
  if (!cheeses[y].active) return;

  if (cheeses[y].usesLeft > 0) cheeses[y].usesLeft--;
  if (cheeses[y].usesLeft == 0) cheeses[y].active = false;
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
  // capture mouse
  if (isNearMouse()) {
    resM += 1;
    mouseE.st = HIDDEN;
    return;
  }

  // harvest veg
  int vi = vegIndexAtPlayer();
  if (vi >= 0 && vegs[vi].stage == 2) {
    resV += 1;
    vegs[vi].stage = 0;
    vegs[vi].nextStepMs = 0;
    return;
  }

  // open shop menu
  if (isOnShop()) {
    if (shopMenuState == MENU_CLOSED) {
      shopMenuState = MENU_OPEN;
      shopMenuSelection = 0;
    }
    return;
  }

  // open workshop menu
  if (isOnWorkshop()) {
    if (workshopMenuState == WORKSHOP_MENU_CLOSED) {
      workshopMenuState = WORKSHOP_MENU_OPEN;
    }
    return;
  }

  // OK on cheese: no action (status shown via contextMessage)
}

const char* contextMessage() {
  if (isNearMouse()) return "OK: chasser";

  int ci = cheeseIndexAtPlayer();
  if (ci >= 0) return cheeseStatusText(ci);

  int vi = vegIndexAtPlayer();
  if (vi >= 0 && vegs[vi].stage == 2) return "OK: recolter";

  if (isOnShop() && shopMenuState == MENU_CLOSED) return "OK: ouvrir magasin";

  if (isOnWorkshop() && workshopMenuState == WORKSHOP_MENU_CLOSED) return "OK: ouvrir atelier";

  return "Deplace Michka";
}

// ---------- Render ----------
void drawTitleScreen() {
  u8g2.clearBuffer();
  
  // Titre principal avec une police plus grande
  u8g2.setFont(u8g2_font_8x13B_tf);
  u8g2.drawStr(15, 18, "La Maison");
  u8g2.drawStr(20, 32, "de Michka");
  
  // Petit dessin de chat à côté du titre
  int titleX = 100;
  int titleY = 8;
  u8g2.drawBox(titleX + 2, titleY + 3, 4, 4);      // body
  u8g2.drawPixel(titleX + 2, titleY + 2);          // ears
  u8g2.drawPixel(titleX + 5, titleY + 2);
  
  // Menu
  u8g2.setFont(u8g2_font_6x10_tf);
  
  // Option "Nouveau"
  if (titleMenuSelection == 0) {
    u8g2.drawStr(8, 48, "> Nouveau");
  } else {
    u8g2.drawStr(18, 48, "Nouveau");
  }
  
  // Option "Continuer" (seulement si sauvegarde existe)
  if (hasSaveGame()) {
    if (titleMenuSelection == 1) {
      u8g2.drawStr(8, 58, "> Continuer");
    } else {
      u8g2.drawStr(18, 58, "Continuer");
    }
  }
  
  u8g2.sendBuffer();
}

void drawHUD() {
  u8g2.setFont(u8g2_font_6x10_tf);
  char hud[32];
  snprintf(hud, sizeof(hud), "S:%d L:%d", resM, resV);
  u8g2.drawStr(0, 10, hud);
  if (hasWorkshop) {
    snprintf(hud, sizeof(hud), "Croq:%d", resK);
    u8g2.drawStr(80, 10, hud);
  }
}

void drawShopMenu() {
  // Afficher le HUD en haut
  drawHUD();
  
  // Menu - fond noir en dessous du HUD
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 12, 128, 52);
  
  // Police et couleur pour texte blanc
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(0);
  
  // Titre
  u8g2.drawStr(45, 24, "MAGASIN");
  
  // Item 0
  char buf0[20];
  if (hasAvailableCheeseSlots()) {
    snprintf(buf0, sizeof(buf0), "Fromage %dL", CHEESE_COST_L);
  } else {
    snprintf(buf0, sizeof(buf0), "Fromage MAX");
  }
  if (shopMenuSelection == 0) {
    u8g2.drawStr(20, 40, ">");
  }
  u8g2.drawStr(30, 40, buf0);
  
  // Item 1
  char buf1[20];
  if (hasWorkshop) {
    snprintf(buf1, sizeof(buf1), "Atelier OK");
  } else {
    snprintf(buf1, sizeof(buf1), "Atelier %dS", WORKSHOP_COST_M);
  }
  if (shopMenuSelection == 1) {
    u8g2.drawStr(20, 54, ">");
  }
  u8g2.drawStr(30, 54, buf1);
  
  u8g2.setDrawColor(1);
}

void drawWorkshopMenu() {
  // Afficher le HUD en haut
  drawHUD();
  
  // Menu - fond noir en dessous du HUD
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 12, 128, 52);
  
  // Police et couleur pour texte blanc
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(0);
  
  // Titre
  u8g2.drawStr(45, 24, "ATELIER");
  
  // Item
  char buf[20];
  snprintf(buf, sizeof(buf), "Croquette %dS %dL", CROQUETTE_COST_M, CROQUETTE_COST_L);
  u8g2.drawStr(20, 40, ">");
  u8g2.drawStr(30, 40, buf);
  
  u8g2.setDrawColor(1);
}

void render(unsigned long now) {
  if (titleState == TITLE_SHOWING) {
    drawTitleScreen();
    return;
  }
  
  u8g2.clearBuffer();

  drawHUD();
  drawDividerWithDoor();

  // shop
  drawShop(shopX, shopY);

  // veggies
  for (int i = 0; i < VEG_COUNT; i++) {
    drawVeg(vegs[i].x, vegs[i].y, vegs[i].stage);
  }

  // cheeses:
  // - blink when usesLeft == 1 (about to disappear)
  // - ALSO flicker quickly while the mouse is EATING on that row (miam effect)
  for (int y = 0; y < GRID_H; y++) {
    if (y == DOOR_Y) continue;
    if (!cheeses[y].active) continue;

    bool drawIt = true;

    // Extra "miam" flicker while eating THIS cheese
    if (mouseE.st == EATING && mouseE.y == y) {
      drawIt = ((now / CHEESE_EAT_FLICKER_MS) % 2) == 0;
    } else if (cheeses[y].usesLeft == 1) {
      drawIt = ((now / CHEESE_BLINK_PERIOD_MS) % 2) == 0;
    }

    if (drawIt) drawCheese(HOUSE_X1, y);
    
    // Draw arrow indicator for newly placed cheese
    drawArrowIndicator(y, now);
  }

  // workshop (si possédé)
  if (hasWorkshop) {
    drawWorkshop(workshopX, workshopY);
    // Draw arrow indicator for newly placed workshop
    drawWorkshopArrowIndicator(now);
  }

  // mouse blink tied to speed (when EATING -> steady ON for readability)
  if (mouseE.st != HIDDEN) {
    bool mouseVisible = true;
    if (mouseE.st != EATING) {
      int blinkPeriod = clampi(mouseE.moveDelayMs, 160, 500);
      mouseVisible = ((now / (unsigned long)blinkPeriod) % 2) == 0;
    }
    drawMouse(mouseE.x, mouseE.y, mouseVisible);
  }

  // player
  drawMichka(michka.x, michka.y);

  // Menus (par-dessus tout)
  if (shopMenuState == MENU_OPEN) {
    drawShopMenu();
  } else if (workshopMenuState == WORKSHOP_MENU_OPEN) {
    drawWorkshopMenu();
  } else {
    // bottom message (seulement si menus fermés)
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

  // Gestion du menu titre
  if (titleState == TITLE_SHOWING) {
    // S'assurer que la sélection est valide
    bool saveExists = hasSaveGame();
    if (!saveExists && titleMenuSelection > 0) {
      titleMenuSelection = 0;
    }
    
    if (canMove) {
      if (upEdge || downEdge) {
        int maxOptions = saveExists ? 2 : 1;
        titleMenuSelection = (titleMenuSelection + 1) % maxOptions;
        lastInputMs = now;
      }
    }
    if (okEdge) {
      lastInputMs = now;
      if (titleMenuSelection == 0) {
        // Nouveau jeu
        resetGame();
        titleState = TITLE_PLAYING;
      } else if (titleMenuSelection == 1 && saveExists) {
        // Continuer
        if (loadGame()) {
          titleState = TITLE_PLAYING;
        }
      }
    }
    return; // Ne pas gérer les autres inputs sur l'écran titre
  }

  // Gestion du menu du magasin
  if (shopMenuState == MENU_OPEN) {
    if (canMove) {
      if (upEdge) {
        shopMenuSelection = (shopMenuSelection - 1 + 2) % 2;
        lastInputMs = now;
      }
      if (downEdge) {
        shopMenuSelection = (shopMenuSelection + 1) % 2;
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
        // Acheter fromage
        int cheeseY = buyCheese(now);
        if (cheeseY >= 0) {
          // Achat réussi : fermer le menu et afficher la flèche
          shopMenuState = MENU_CLOSED;
          arrowIndicator.active = true;
          arrowIndicator.cheeseY = cheeseY;
          arrowIndicator.endTime = now + ARROW_DISPLAY_DURATION_MS;
        }
      } else if (shopMenuSelection == 1) {
        // Acheter atelier
        if (buyWorkshop()) {
          // Achat réussi : fermer le menu et afficher la flèche
          shopMenuState = MENU_CLOSED;
          workshopArrowIndicator.active = true;
          workshopArrowIndicator.endTime = now + ARROW_DISPLAY_DURATION_MS;
        }
      }
    }
    return; // Ne pas gérer le mouvement du joueur quand le menu est ouvert
  }

  // Gestion du menu de l'atelier
  if (workshopMenuState == WORKSHOP_MENU_OPEN) {
    if (leftEdge) {
      workshopMenuState = WORKSHOP_MENU_CLOSED;
      lastInputMs = now;
      return;
    }
    if (okEdge) {
      lastInputMs = now;
      craftCroquettes();
      // Le menu reste ouvert après craft
    }
    return; // Ne pas gérer le mouvement du joueur quand le menu est ouvert
  }

  if (canMove) {
    if (upEdge) {
      int ny = michka.y - 1;
      // Empêcher d'entrer sur le magasin
      if (!(ny == shopY && michka.x == shopX)) {
        // Empêcher d'entrer sur l'atelier
        if (!(hasWorkshop && ny == workshopY && michka.x == workshopX)) {
          michka.y = clampi(ny, 0, GRID_H - 1);
          lastInputMs = now;
        }
      }
    }
    if (downEdge) {
      int ny = michka.y + 1;
      // Empêcher d'entrer sur le magasin
      if (!(ny == shopY && michka.x == shopX)) {
        // Empêcher d'entrer sur l'atelier
        if (!(hasWorkshop && ny == workshopY && michka.x == workshopX)) {
          michka.y = clampi(ny, 0, GRID_H - 1);
          lastInputMs = now;
        }
      }
    }

    if (leftEdge) {
      int nx = michka.x - 1;
      // Empêcher de traverser le mur sans passer par la porte
      if (!(michka.x == 8 && nx == 7 && michka.y != DOOR_Y)) {
        // Empêcher d'entrer sur le magasin
        if (!(nx == shopX && michka.y == shopY)) {
          // Empêcher d'entrer sur l'atelier
          if (!(hasWorkshop && nx == workshopX && michka.y == workshopY)) {
            michka.x = clampi(nx, 0, GRID_W - 1);
            lastInputMs = now;
          }
        }
      }
    }

    if (rightEdge) {
      int nx = michka.x + 1;
      // Empêcher de traverser le mur sans passer par la porte
      if (!(michka.x == 7 && nx == 8 && michka.y != DOOR_Y)) {
        // Empêcher d'entrer sur le magasin
        if (!(nx == shopX && michka.y == shopY)) {
          // Empêcher d'entrer sur l'atelier
          if (!(hasWorkshop && nx == workshopX && michka.y == workshopY)) {
            michka.x = clampi(nx, 0, GRID_W - 1);
            lastInputMs = now;
          }
        }
      }
    }
  }

  if (okEdge) {
    lastInputMs = now;
    handleOkAction(now);
  }
}

// ---------- Arduino ----------
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

  // Initialize EEPROM (nécessaire pour Raspberry Pi Pico)
  EEPROM.begin(512); // Allouer 512 bytes pour la sauvegarde

  seedRandom();
  u8g2.begin();

  // Initialiser les fromages par défaut
  for (int y = 0; y < GRID_H; y++) {
    cheeses[y].active = false;
    cheeses[y].usesMax = CHEESE_MAX_USES;
    cheeses[y].usesLeft = 0;
  }

  // Initialiser l'écran titre
  titleState = TITLE_SHOWING;
  titleMenuSelection = 0;
  
  lastSaveMs = millis();
}

void loop() {
  unsigned long now = millis();

  updateInput(now);
  
  // Ne mettre à jour le jeu que si on n'est pas sur l'écran titre
  if (titleState == TITLE_PLAYING) {
    updateMouse(now);
    updateVeggies(now);

    // Sauvegarder périodiquement
    if (now - lastSaveMs >= SAVE_INTERVAL_MS) {
      saveGame();
      lastSaveMs = now;
    }
  }

  render(now);

  delay(10);
}
