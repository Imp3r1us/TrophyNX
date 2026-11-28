# Fixtures des lecteurs gvas / nrbf

- `gvas/` : sauvegardes Unreal Engine réelles (UE 4.23 à 5.3) reprises des tests de la bibliothèque Rust
  [localcc/gvas](https://github.com/localcc/gvas) (licence MIT, voir `gvas/LICENSE`).
  `gvas/ue54_checkpoint.sav` : UE 5.4.1 (nouveaux en-têtes de propriété), `Chap2Checkpoint` de Poppy Playtime Chapter 2
  en fin de chapitre (issue #54).
- `nrbf/` : flux .NET BinaryFormatter produits par `nrbf/Generate.cs` (.NET 8,
  `EnableUnsafeBinaryFormatterSerialization`) : `save0.dat` = début de partie, `save1.dat` = partie avancée,
  plus une chaîne seule (`string.dat`) et une `List<string>` à la racine (`list.dat`).

Utilisés par `tools/savelab/test/test_formats.js` et `host/test/test_formats.cpp`, qui vérifient que le Save Lab
et l'app donnent exactement le même arbre.
