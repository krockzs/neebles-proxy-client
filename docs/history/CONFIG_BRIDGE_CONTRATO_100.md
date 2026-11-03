# NEEBLES Proxy Client 1.0.0 - puente de configuración protegido

## Autoridad

- Secrets 1.0.7: única persistencia de la IP elegida en `vault-neebles-proxy-client.kdbx`.
- Runtime Client instalado: único proceso que solicita `structure.get` a Secrets. Arranca con `--serve-config` como ejecutable canónico.
- Socket local: `/run/user/<desktop_uid>/neebles/proxy-client/bridge.sock`; acepta únicamente a root mediante `SO_PEERCRED`. Consulta Secrets **en cada petición**, sin cachear IP ni contraseña.
- Operador privilegiado de OS: `neebles-proxy-client-platform`, invocado únicamente por el proveedor de Boss con argumentos inmutables de política root: `--activate|--deactivate|--inspect`, `--user-uid=<uid>`, `--tailnet=<name>`.
- El ayudante root valida credenciales de pares (UID, PID e inodo ELF canónico), la IP 100.64.0.0/10, la identidad de tailnet, estado de daemon, aprobacion de ExitNodeOption y selección posterior a la operación.
- El proveedor no recibe IP como argumento ni lee archivos plaintext de configuración.

## Dependencia de despliegue aún no resuelta (FAIL-CLOSED)

Este parche de fuente NO crea un servicio de usuario ni incorpora el paso de Construction para arrancar el puente persistente. Su arranque desde `modules.runtime` y `session` requiere certificar los grants de escritura sobre socket de sesión en Esbirro/CUSTOM V2. El helper deniega la operación si no existe un puente autenticado.

El OS provider de Boss exige un ELF root-owned en una ruta controlada. Un binario recién compilado bajo el repositorio o instalado en /opt no basta. El manifiesto de ejemplo **no es desplegable** hasta que una autoridad OS lo registre correctamente en el VivoProBook.

El runtime Qt debe ser construido y certificado dentro del sysroot controlado Qt 6.8.2. No compilar ni ejecutar automáticamente en VivoProBook con bibliotecas del host.

El flujo y la UI de confirmación final (animación después de la respuesta de Boss) requieren cierre posterior. No se añade código de pruebas de ruta ni se cambia la configuración de Tailscale en el Ryzen.

## Criterios de cierre en VivoProBook

1. Instalación Boss con MaterialBinding y RuntimeLease válidos, bóveda creada y abierta.
2. Config acepta IP confirmada dos veces y recupera idéntica IP en una segunda apertura.
3. Bridge controlado por Boss permanece vivo y expone su socket solo a root.
4. Proveedor root OS instalado por autoridad y con política exacta validada.
5. Boss Surface Activate usa la IP de Secrets y selecciona Exit Node aprobado. Deactivate la libera.
6. La visualización de 3 segundos ocurre después de respuesta positiva real de Boss.
7. Prueba externa mediante terminal en 4G para comprobar rutas (sin integrar tests en el módulo).
