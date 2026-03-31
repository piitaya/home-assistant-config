# Home Assistant Configuration

My personal [Home Assistant](https://www.home-assistant.io/) configuration.

![HA Version](https://img.shields.io/badge/Home%20Assistant-2026.4.0b7-blue)

## Setup

After cloning, enable git hooks:
```bash
git config core.hooksPath .githooks
```

## Structure

```
.
├── configuration.yaml        # Main configuration
├── automations.yaml          # Automations
├── scripts.yaml              # Scripts
├── scenes.yaml               # Scenes
├── packages/                 # Modular configuration packages
├── blueprints/               # Automation & script blueprints
├── custom_templates/         # Custom Jinja2 templates
├── custom_zha_quirks/        # Custom ZHA device quirks
└── themes/                   # Custom UI themes
```

