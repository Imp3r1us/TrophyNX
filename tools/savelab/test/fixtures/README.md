# Fixtures des lecteurs gvas / nrbf

- `gvas/` : sauvegardes Unreal Engine réelles (UE 4.23 à 5.3) reprises des tests de la bibliothèque Rust
  [localcc/gvas](https://github.com/localcc/gvas) (licence MIT, voir `gvas/LICENSE`).
- `nrbf/` : flux .NET BinaryFormatter produits par `nrbf/Generate.cs` (.NET 8,
  `EnableUnsafeBinaryFormatterSerialization`) : `save0.dat` = début de partie, `save1.dat` = partie avancée,
  plus une chaîne seule (`string.dat`) et une `List<string>` à la racine (`list.dat`).

Utilisés par `tools/savelab/test/test_formats.js` et `host/test/test_formats.cpp`, qui vérifient que le Save Lab
et l'app donnent exactement le même arbre.

- `es3/` : sauvegarde Easy Save 3 chiffrée **synthétique** (`SaveFile.es3`, JSON de deux valeurs) avec un mot de passe
  factice (`password.txt`), pour `host/test/test_es3_key.cpp` (recherche du mot de passe par morceaux).
