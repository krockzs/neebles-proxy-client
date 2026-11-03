# NEEBLES Proxy Client 1.0.0 — F06 Cierre funcional de fuente

## Invariantes implementados

- La IP configurable permanece en `vault-neebles-proxy-client.kdbx` por el IPC de Secrets. El helper root no lee datos persistidos, argumentos arbitrarios ni configuraciones JSON de usuario.
- `activate` obtiene la IP de un Bridge local perteneciente al usuario, autenticado por `SO_PEERCRED` y por el inodo del ejecutable canónico, y valida la aprobación del Exit Node mediante el estado de Tailscale.
- Antes de seleccionar el Exit Node, consulta el endpoint `http://<IP_CONFIGURADA>:18879/health` y verifica identidad `neebles-proxy-server`, IPv4 y estado `reachable`. Health demuestra disponibilidad, **nunca concede autorización**.
- `deactivate` despeja cualquier Exit Node seleccionado dentro de la tailnet autorizada, incluso si Secrets está bloqueado, la IP fue cambiada o el Server está fuera de línea. Esta operación requiere el mismo proveedor root autorizado y verifica el resultado.
- El puente no cambia la red; el helper root no recibe IP por argv; no se añaden diagnósticos de tráfico al Client.
- Los argumentos `--user-uid` y `--tailnet` deben proceder de una política OS publicada por root y verificada por el proveedor. El renderizador `tooling/os-provider/render_policy.py` **solo prepara** un JSON candidato en el VivoProBook. No eleva privilegios ni instala nada.

## Estado y límites de esta fase

F06 cierra las divergencias identificadas en el **código candidato**, no la instalación. La compilación previa en el Ryzen contra Qt 6.8.2 de host no es certificación. El empaquetado final sigue la autoridad de Esbirro: sysroot controlado, `DESTDIR`, auditoría ELF/dependencias y selección de material en CUSTOM V2. La política root y el descriptor de autoridad de Boss se deben publicar de forma explícita en el VivoProBook después de validar la identidad de la máquina.

Los botones `activate`/`deactivate` de Boss operan sobre Lifecycle. Las animaciones `--show-active`/`--show-inactive` aún **no están conectadas** a una respuesta real del gobernador. No deben usarse como prueba de activación. El estado efectivo se verifica mediante Tailscale en la prueba de aceptación.

## Secuencia para publicar e instalar (sin atajos)

1. QA estructural y compilación de Client en el Ryzen (esta fase).
2. Compilar todos los ELF Qt del Client en el sysroot **Esbirro 6.8.2**, generar `DESTDIR` y certificar árbol ELF y `RUNPATH`. No publicar ELF del build host.
3. Verificar y publicar el código Client 1.0.0 con SHA40 inmutable.
4. Materializar en CUSTOM V2 el Construction pinneado a ese SHA y sus paquetes Essential/delta, world y manifiestos de integridad; certificar el material.
5. Publicar la selección en el Registry de Boss sin cambiar su lógica; verificar el contrato IPC privado de Secrets para `neebles-proxy-client` antes de declarar aceptación.
6. Instalar por Boss en el **VivoProBook**, comprobar `systemd --user` del Bridge y su pertenencia a Secrets. No instalar en Ryzen.
7. En VivoProBook, como fuente para la autoridad OS, renderizar la política con el UID real y la tailnet personal comprobada. Si Tailscale está en otra tailnet, el renderizador falla cerrado. Una autoridad root autentica el archivo candidato, instala el ELF privilegiado certificado, la política y el descriptor de Boss. **No ejecutar la plantilla con marcadores**.
8. En VivoProBook, desbloquear Secrets, abrir Config y guardar IP dos veces; cerrar/abrir y comprobar persistencia, Activate/Deactivate por Boss y estado de Tailscale. Finalmente cambiar a hotspot 4G y usar **un comando externo en terminal**, no incluido en Client, para demostrar la ruta e IP de salida ON/OFF.

## Reglas de aceptación

- La configuración no se almacena fuera de la bóveda.
- ON falla cerrado ante IP inválida, tailnet equivocada, peer no aprobado, Health no disponible, Bridge no autenticado o proveedor root ausente.
- OFF funciona con Secrets bloqueado y Server desconectado, siempre que Tailscale local responda y el proveedor OS esté disponible.
- Si se cambia la IP de Server mientras otra selección está activa, ON no reemplaza silenciosamente el nodo. OFF limpia la selección actual y permite volver a activar.
- Todos los componentes de usuario y sus dependencias proceden de material certificado; los auxiliares root provienen del despliegue OS autenticado.
