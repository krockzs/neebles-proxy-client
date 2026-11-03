# Proxy Client 1.0.0 — preparación Construction (candidato no publicable)

## Autoridad

- Ryzen: desarrollo, Esbirro Qt 6.8.2 y certificación ELF. VivoProBook: instalación real por Boss.
- CUSTOM V2 conserva la verdad de Construction y el material de RootFS `rust-qt`.
- El documento `tooling/custom-v2-candidate/neebles-proxy-client.construction.DRAFT.json` solo sirve como plantilla de trabajo.
- `__PIN_CLIENT_COMMIT_SHA40__` **no es un commit**. Antes de su publicación deben existir fuente publicada, revisión exacta y material certificado. No copiar este draft a CUSTOM V2 tal cual.

## Contratos ya identificados

1. `install-vault` y `open-config` deben ejecutarse como el ELF canónico instalado `runtime/neebles-proxy-client`, con socket de sesión de Secrets en `session_readonly`.
2. `boss.user_service` **sí existe** en Boss. La unidad `integration/systemd-user/neebles-neebles-proxy-client-config-bridge.service` debe quedar como material instalado y bajo `uninstall.extras`.
3. El proveedor OS de Boss **NO expande** `__DESKTOP_UID__` ni `__TAILNET__`. La política final de `/etc/neebles/platform-operations/neebles-proxy-client.json` deberá generarse y verificarse con valores concretos del VivoProBook durante el despliegue controlado, por root. La plantilla `docs/OS_PROVIDER_POLICY_TEMPLATE.json` no es instalable como está.
4. Para operaciones directas del socket de Secrets, la identidad la determina su broker por el ELF canónico y el manifiesto instalado. `ipc_invoke_allow` de Boss corresponde a endpoints de Boss y no debe confundirse con este IPC privado.
5. No se deben presentar animaciones ON/OFF como resultado confirmado mientras la operación privilegiada de Boss y su orden con `show-active/show-inactive` no hayan sido verificados.
6. El bridge se ejecuta como `systemd-user` en el VivoProBook. Su ELF y las bibliotecas Qt del entorno real precisan inspección física adicional; una compilación del host Ryzen no certifica esa ejecución.

## Cierre pendiente

- Resolver orden de transiciones y proveedor root físico sin ampliar autoridad de Boss.
- Incorporar ELF/RootFS y hashes certificados en CUSTOM V2.
- Publicar el Client, seleccionar su SHA en Boss Registry, realizar instalación gobernada en VivoProBook.
- Probar Config persistente, ON/OFF y tráfico por hotspot 4G con comandos externos, sin agregar tests de tráfico al Client.

No se modifica ni se reconfigura Server 1.0.6, Boss, Secrets o Tailscale durante esta fase.
