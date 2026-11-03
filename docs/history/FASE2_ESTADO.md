# Fase 2 — superficies y lifecycle (candidato, sin despliegue)

- Se declaran acciones **activate/deactivate** para Launcher y Tray y transiciones Boss.
- Ambas invocan `boss.platform_operation` con `require_privilege=true`; Boss deniega mientras falte descriptor de autoridad y política raíz.
- `PLATFORM_PROVIDER_FASE2_POLICY_EXAMPLE.json` es solo ejemplo; **no instalar** hasta revisar permisos, propiedad, ELF y control de salida.
- `Config` todavía NO está expuesto: el diálogo Qt sigue siendo demo y NO persiste en Secrets.
- No hay integración efectiva del preflight Health al flujo de activación. El servidor devuelve actualmente `exit_ready=false`; no equiparar ese campo a la aprobación de Tailscale.
- No desplegar en VivoProBook: faltan rootfs CUSTOM V2, política OS, Secrets, control de rollback y QA integrada.
- Este parche no modifica Boss, CUSTOM, rutas de red ni archivos del sistema.
