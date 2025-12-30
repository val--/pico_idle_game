# La Maison de Michka

Un idle game tout simple pour Raspberry Pi Pico avec écran OLED.

## Matériel requis

- Raspberry Pi Pico
- Écran OLED SH1106 128x64 (I2C)
- 5 boutons (UP, DOWN, LEFT, RIGHT, OK)
- Résistances pull-up pour les boutons

## Connexions

- **OLED** : SDA → GPIO 0, SCL → GPIO 1
- **Boutons** : GPIO 2 (UP), GPIO 3 (DOWN), GPIO 4 (RIGHT), GPIO 5 (LEFT), GPIO 6 (OK)
- Tous les boutons en INPUT_PULLUP

## Gameplay

- **Déplacer Michka** : Utilisez les flèches pour vous déplacer dans la maison et le jardin
- **Récolter les légumes** : Attendez qu'ils poussent, puis appuyez sur OK pour les récolter
- **Acheter du fromage** : Allez au magasin ($) et achetez du fromage avec des légumes
- **Attraper les souris** : Les souris apparaissent pour manger le fromage, attrapez-les avec OK
- **Acheter l'atelier** : Au magasin, achetez l'atelier avec des souris
- **Fabriquer des croquettes** : À l'atelier, transformez souris + légumes en croquettes
- **Automatisation / idle** : A venir (embaucher du personnel pour récoler les ressources)
- **But du jeu** : Fabriquer de la pâtée pour attirer le Chat Légendaire...

## Fonctionnalités

- Sauvegarde automatique toutes les 5 secondes
- Système de ressources : Souris (S), Légumes (L), Croquettes
- Fromages avec usure limitée (3 utilisations)
- Souris avec comportement dynamique

## Bibliothèques nécessaires

- `U8g2lib` (pour l'écran OLED)
- `Wire` (I2C, inclus avec Arduino)
- `EEPROM` (sauvegarde, inclus avec Arduino)
