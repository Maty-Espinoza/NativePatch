# Documentación para la IA (y para quien quiera trabajar igual)

Esta carpeta es el contexto de trabajo de Claude en NativePatch: cómo se trabaja, qué herramientas hay, cómo se compila,
cómo se prueba en la Vita y qué reglas no se negocian. La raíz del repo tiene un `CLAUDE.md` que obliga a leer esto al
empezar cada conversación. La documentación para humanos está en `../../dev/`.

## Qué leer y en qué orden
1. **`good-practices.md`** — siempre, antes de tocar nada: comentarios, código muerto, archivos viejos, versiones,
   documentación. Es corto.
2. **`workflow.md`** — cómo empieza y termina una conversación, dónde está el repo, pull/push, cómo pedir pruebas al
   usuario, cómo guardar resultados y las reglas del usuario.
3. El último resumen de conversación del proyecto de claude.ai (`claude/resumen_conversacion_*.md`) y `../../../TODO.md`:
   dicen dónde quedó todo y qué sigue.
4. Según la tarea: `environment.md` (compilar: contenedor, VM del Mac, WSL, CI), `relay.md` (mandar archivos a la Vita y
   traer logs), `stage3-plan.md` (plan y resultados de la etapa en curso), y de `../../dev/es/`:
   `how-it-works.md`, `logs-and-switches.md`, `dumps.md`, `findings.md`, `versions.md`.

## Archivos
| Archivo | Qué |
|---|---|
| `good-practices.md` | Reglas para que el código, los comentarios, los archivos y la documentación no se desordenen |
| `workflow.md` | Flujo de trabajo completo con el usuario y con el repo |
| `environment.md` | Ambientes de compilación y herramientas (`tools/`), prueba del capturador con qemu |
| `relay.md` | El relevo con la Vita (`tools/vitabridge.py`): protocolo de archivos, qué hacer y qué no |
| `stage3-plan.md` | Etapa 3: plan, pasos, lo aprendido en cada versión de prueba y estado |

## Idiomas
Los documentos de `docs/dev/` y `docs/agent/` existen en inglés (`en/`) y español (`es/`) con los mismos nombres de
archivo. El español es el idioma de trabajo con el usuario; el inglés es el de referencia para terceros. Al cambiar uno
se cambia el otro en la misma tarea (`good-practices.md`). `TODO.md`, `CHANGELOG.md` y `README.md` de la raíz están solo
en inglés (`README.es.md` es la versión en español del README).
