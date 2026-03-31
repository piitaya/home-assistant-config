# Mise a jour de la table de couleurs Bambu Lab

Source: [SpoolmanDB](https://github.com/Donkie/SpoolmanDB/blob/main/filaments/bambulab.json)

## Quand ?

Quand un scan affiche le hex brut au lieu d'un nom de couleur.

## Comment ?

```bash
cd /homeassistant/esphome/components/bambu_nfc
python3 generate_colors.py
```

Puis recompiler et reflasher.
