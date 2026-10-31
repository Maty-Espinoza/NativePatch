# Documentación para desarrolladores (español)

Para entender NativePatch sin saber nada de antemano. Versión en inglés: `../en/` (mismos nombres de archivo).

| Orden | Archivo | Qué |
|---|---|---|
| 1 | `overview.md` | Qué es el proyecto, sus dos plugins, estado, cómo instalar y compilar, límites, origen |
| 2 | `graphics-basics.md` | Lo mínimo de gráficos (CPU, GE, listas de dibujo, texturas, framebuffers) para seguir el resto. Si ya sabes, sáltalo |
| 3 | `how-it-works.md` | Cómo funcionan NativeLink y NativeDraw: buzón, espejo, compuerta, captura, registro del frame, traductor, presentador |
| 4 | `findings.md` | Qué se midió, qué se descubrió, qué se descartó y qué límites tiene |
| 5 | `architecture-and-roadmap.md` | El análisis que decidió la arquitectura y la hoja de ruta por etapas (se conserva tal como se escribió) |
| — | `logs-and-switches.md` | Qué escribe cada log, línea por línea, e interruptores de `ux0:data/nativedraw/` |
| — | `dumps.md` | Volcados de frames: niveles, archivos, cómo leerlos con `tools/volcado.py`, reproducción en la Vita |
| — | `adrenaline.md` | Lo que hay que saber de Adrenaline: carga de plugins, cómo dibuja, mapa de memoria, canal PSP–ARM |
| — | `versions.md` | Reglas de versiones, historial de commits y equivalencia con los nombres viejos (GePatch, gepresent) |

Cómo se trabaja en el proyecto (flujo, ambientes, pruebas): `../../agent/es/`. Historia versión por versión:
`../../changelog/es/`.
