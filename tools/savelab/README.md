# Save Lab

Outil web (une seule page, fonctionne sur téléphone) pour **comprendre une sauvegarde Switch et en tirer des succès**
au format de l'app (`games/<TITLEID>.json`).

1. **Captures** : importer plusieurs sauvegardes du même jeu (fichiers ou .zip JKSV), dans l'ordre, nommées d'après
   ce qui a été fait en jeu. Une capture « contrôle » (rien fait depuis la précédente) élimine le bruit.
2. **Structure** : format reconnu (BotW/TotK/MK8D/Smash, arbres BYML, sauvegardes texte JSON des jeux Unity comme Hollow Knight,
   sauvegardes Unreal Engine « GVAS », données .NET BinaryFormatter « NRBF » des jeux Unity), en-tête, entropie (chiffré/compressé ?), table hash→valeur,
   dictionnaire de noms (CRC32/Murmur3/FNV-1a), textes.
3. **Différences** : chaque valeur qui change est classée (drapeau 0→1, compteur, champ de bits, flottant, bruit).
   Sélection multiple → succès « compter »/« somme » ; « Étendre la série » retrouve les drapeaux voisins d'une collection.
4. **Valeur connue** : recherche type Cheat Engine d'un chiffre vu en jeu.
   **⚡ Générer les succès** : à partir d'une partie neuve + une save avancée/100 % (ex. téléchargée sur GBAtemp),
   regroupe les drapeaux en collections (paliers premier / moitié / complète) et les compteurs en paliers.
5. **Succès** : édition, test sur chaque capture avec la même logique que la console, validation, export JSON.

Fichiers : `engine.js` (moteur, testé par `node test/test_engine.js`), `formats.js` (modules de format GVAS et NRBF,
testés par `node test/test_formats.js` sur `test/fixtures/`), `app.html` (interface),
`build.py` → `index.html` (page autonome qui embarque aussi les définitions de `romfs/games`).

### Modules de format

Un module transforme une sauvegarde binaire en arbre (objets, listes, nombres, textes) : tous les outils par chemin
(différences, ⚡ Générer, règles) marchent alors sans rien changer. Chaque module a son jumeau C++ dans l'app
(`source/save_formats.cpp`), qui doit produire **exactement** le même arbre (vérifié par `test_formats.js` avec `CPP_DUMP`).
Pour en ajouter un : une fonction `parseXxx(buf)` dans `formats.js` qui rend `null` si ce n'est pas son format,
une entrée dans `TREE_FORMATS` (`engine.js`), le lecteur C++ correspondant et une ligne dans `evaluateSave`.
