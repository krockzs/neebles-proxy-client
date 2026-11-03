# N.E.E.B.L.E.S. Proxy Client 1.0.1 — candidato fuente F19

## Entrega del código

Módulo de usuario para configurar un nodo de salida Tailscale en su propia bóveda NEEBLES Secrets.
La pantalla `--config` solicita la IP del servidor dos veces. El Bridge de usuario
`--serve-config` atiende solicitudes autenticadas del proveedor OS por un socket privado
sin persistir la IP fuera de Secrets.

Las acciones Activate/Deactivate son acciones de **Boss Lifecycle**;
no se ejecuta `sudo`, setuid ni modificación directa de rutas desde la interfaz.
El ejecutable privilegiado `runtime/neebles-proxy-client-platform` está destinado
exclusivamente al proveedor OS independiente, mediante política root fija.
La animación de tres segundos se activa al observar un cambio confirmado del
Exit Node propio en Tailscale desde el Bridge de usuario. Requiere una sesión
gráfica disponible en el servicio de usuario y aceptación real en VivoProBook.

## Autoridades y límites

- `manifest.json`, `lifecycle.json`, `surfaces.json` y `settings/default.json`: contratos del módulo.
- `integration/systemd-user/`: unidad declarada para `boss.user_service`.
- `src/config_secrets.cpp`: única autoridad persistente para la IP, mediante Secrets.
- `runtime/`: ELF Qt 6.8.2 compilados y auditados en Esbirro (no Qt del host).
- `assets/`: icono canónico utilizado en la animación.
- `tooling/custom-v2-candidate/`: borradores **no productivos**, no son autoridad CUSTOM V2.

**Publicar Client no instala el módulo.** Falta completar el material propio y
Construction en CUSTOM V2, fijar los commits de Client y CUSTOM en Boss Registry,
materializar el proveedor root con política fija en VivoProBook y aceptar
la integración Secrets/Bridge/ON/OFF/4G. El Preflight HTTP incluido exige IP y tailnet como parámetros y es una
herramienta de comprobación no privilegiada; el Lifecycle de Activate valida
la aprobación y conectividad Tailscale en el proveedor, no ejecuta ese Preflight.

## F19 — cierre de fuente, pendiente de certificación y aceptación instalada

- F18 mantiene un solo `toggle-button` declarado en Launcher y Tray. El QML genérico
  de Boss que lo consume debe certificarse por separado; **no se cambia su estado por
  inferir éxito de una pulsación**. La sincronización con el Exit Node real y los
  reinicios de Boss siguen siendo gates de aceptación instalada.
- El proveedor OS comunica al Bridge `state.confirmed` solo después de confirmar
  el resultado de Tailscale. El Bridge acepta esa señal exclusivamente desde UID 0,
  muestra el splash de tres segundos y conserva el sondeo de estado como respaldo.
  Una notificación visual fallida nunca modifica rutas ni revierte la operación.
- `--config` usa la paleta negra, violeta y cian del Launcher/Tray de Boss y almacena
  la dirección únicamente por Secrets, con doble confirmación.
- Todos los cambios de esta fase son **fuente**. No se recompilaron ni certificaron
  aquí los ELF de runtime; no se actualizó CUSTOM V2, Registry, Boss ni el proveedor OS.

### Gates faltantes antes de release

1. Compilar en el Esbirro Qt 6.8.2/GCC14 controlado, ejecutar CTest y auditar ELF
   desde el DESTDIR y el material CUSTOM V2 autenticado.
2. Verificar el contrato QML de F18 en Boss compilado, con resultado real de Lifecycle
   (éxito/fallo) y reconciliación de estado al reiniciar.
3. Instalar con autoridad del OS y validar Secrets, Bridge, Session DISPLAY/XAUTHORITY,
   Tailscale, splash ON/OFF, Config, cierres y errores inducidos.
4. Seleccionar SHA, versionar material/Construction y publicar únicamente tras gates.


## F30 — Registro de Config en Module IPC

La ventana `--config` actúa como el runtime gobernado por `governor.open`:
se conecta a `/run/neebles/modules.sock` mediante tramas JSON de longitud
declarada, anuncia `register` con protocolo 1 y sin endpoints de Commands,
y conserva esa conexión durante la vida de la ventana. Responde `ping`
y `shutdown`; al cerrar envía `unregister`. Si Boss rechaza o pierde
la sesión, Config falla de forma cerrada. `--serve-config` continúa siendo
un servicio independiente y no ocupa RuntimeRegistry.

El transporte no concede privilegios, no altera el Exit Node ni gestiona
Secretos fuera del contrato existente. `NEEBLES_MODULES_SOCKET` permite
solo redirigir el endpoint en pruebas controladas. La aceptación real
requiere instalar mediante Boss, confirmar registro SO_PEERCRED y comprobar
el ciclo Governor Open/Close con RuntimeLease y Secrets.
