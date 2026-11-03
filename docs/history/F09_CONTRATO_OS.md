# F09 — Contrato de plataforma Proxy Client 1.0.0

## Origen del ajuste

El proveedor de Boss 2.0.11 ejecuta el helper root dentro de un servicio
restringido a `RestrictAddressFamilies=AF_UNIX`. El helper anterior intentaba
abrir una conexión HTTP AF_INET al endpoint Health, bloqueando Activate aunque
el nodo estuviera aprobado en Tailscale.

## Separación de autoridades

- **Preflight (usuario, solo lectura):** consulta `http://<IP>:18879/health`,
  verifica identidad de Server y disponibilidad, sin modificar rutas.
- **Helper root (AF_UNIX):** obtiene la IP mediante Bridge autenticado,
  valida la tailnet y la disponibilidad y aprobación del peer mediante
  `tailscale status --json`, comprueba alcance con `tailscale ping` a través
  del daemon local y selecciona el Exit Node por `tailscale set`;
  verifica el estado resultante. No usa HTTP ni sockets AF_INET directamente.
- **Deactivate:** no depende de Secrets ni del estado del Server.

No se amplían permisos del proveedor root ni se modifican Boss, Secrets,
Server o CUSTOM. La confirmación de tráfico real se hará **después** de
instalar en VivoProBook, con comandos externos desde hotspot 4G.

## Publicación pendiente

1. Confirmar el nuevo ELF Qt 6.8.2 de F09 y su compatibilidad directa con
   los manifiestos existentes Essential + referencia rust-qt.
2. Publicar el código Client como commit inmutable y fijar sus SHA40.
3. Construir y certificar material/Construction del **propio Client** en
   CUSTOM V2; la referencia Server no autoriza reutilizar su delta.
4. Registrar Client en Boss; instalar en Vivo, verificar Vault/Secrets
   y el proveedor OS autenticado, y aceptar Config/ON/OFF + tráfico 4G.

## Limitación

Los cinco CTest existentes son pruebas estructurales, no demuestran
funcionamiento Live ni cierre ELF transitivo. El runner F09 produce un
candidato y evidencia de material de referencia; no una release publicada.
