# N.E.E.B.L.E.S. Proxy Client 1.0.0

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
