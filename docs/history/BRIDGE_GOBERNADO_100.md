# NEEBLES Proxy Client 1.0.0 — Fase Bridge administrado

Estado de fuente: candidato, pendiente publicación y certificación.

## Propiedad y orden

- `install` conserva el paso `ensure-client-vault` (Construction `install-vault`) y luego instala `config-bridge-daemon` con la capacidad existente `boss.user_service`.
- El recurso está declarado en `manifest.json` bajo `uninstall.extras`, como servicio `systemd-user` administrado por Boss. No se instala un servicio del sistema ni se usa `sudo` desde Client.
- El ELF de la unidad es el **mismo ejecutable canónico** `runtime/neebles-proxy-client --serve-config`, necesario para la identidad de Secrets y la comprobación del proveedor privilegiado.
- El servicio se reinicia tras fallos y en sesiones siguientes; el socket `/run/user/<UID>/neebles/proxy-client/bridge.sock` conserva acceso 0600. Si el socket pertenece a otra identidad o está activo, la instancia nueva falla cerrada; solo se elimina un socket local abandonado.
- Uninstall/Update conservan las validaciones de propietario y los límites transaccionales existentes de Boss para `systemd-user`.

## No certificado todavía

- El servicio de usuario ejecuta un ELF Qt: hay que comprobar que el entorno productivo de VivoProBook dispone de su entorno **certificado**. No se infiere que las bibliotecas Qt del host satisfagan el contrato de Esbirro.
- El puente no establece rutas ni cambia Tailscale.
- Provider root, `__DESKTOP_UID__` y `__TAILNET__` continúan pendientes de materialización OS controlada en VivoProBook; no sustituir por argumentos configurables desde UI.
- `--show-active`/`--show-inactive` todavía no están enlazados a la confirmación real de la operación de Boss. No considerar la animación como indicador de estado hasta corregir la integración.
- Falta añadir grant de Secrets para `neebles-proxy-client` en el mecanismo de autorización correspondiente, junto con Construction, RootFS, certificación de ELF y Registry.

## Prueba de aceptación (fuera del módulo)

Después de instalar en VivoProBook: abrir Config, guardar IP dos veces, cerrar/abrir Config, comprobar que la IP persiste en Secrets; activar/desactivar a través de Boss; comparar rutas reales desde terminal con hotspot 4G. El diagnóstico de tráfico es una herramienta externa, no código de Client.
