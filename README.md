# N.E.E.B.L.E.S. Proxy Client

Módulo cliente de NEEBLES Proxy para seleccionar y controlar
un Exit Node de Tailscale mediante la infraestructura
gobernada de NEEBLES OS.

La versión declarada se encuentra en `manifest.json`.
La versión publicada debe confirmarse mediante el Registry
y la release correspondiente.

## 1. Arquitectura

Proxy Client utiliza:

- Tailscale: conexión de red y Exit Node.
- NEEBLES Secrets: almacenamiento de configuración sensible.
- NEEBLES Boss: instalación y operaciones Lifecycle.
- Proveedor privilegiado del sistema: operaciones autorizadas
  sobre Tailscale.
- Bridge de usuario: integración con la configuración.

La interfaz gráfica no ejecuta `sudo` ni modifica
directamente las rutas del sistema.

La preparación inicial de Tailscale es responsabilidad del
usuario y debe efectuarse antes de instalar Proxy Client.

## 2. Requisitos previos

- Equipo Linux compatible con Tailscale.
- Conectividad a Internet.
- Acceso a la tailnet correspondiente.
- Un Proxy Server configurado como Exit Node.
- Exit Node autorizado desde la administración de Tailscale.
- NEEBLES OS, Boss y Secrets compatibles con el módulo.

La IP que debe utilizar el cliente es la dirección
Tailscale del servidor, no su IP pública de Internet.

## 3. Instalar Tailscale

Comprobar si ya está instalado:

```bash
command -v tailscale
```

Si no está instalado, utilizar la instalación oficial:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
```

O utilizar los paquetes oficiales de Debian:

https://pkgs.tailscale.com/stable/

Habilitar el servicio:

```bash
sudo systemctl enable --now tailscaled
```

Si el equipo todavía no está autenticado:

```bash
sudo tailscale up
```

Completar la autenticación en la tailnet donde se encuentra
Proxy Server.

Verificar:

```bash
tailscale version
tailscale status
tailscale ip -4
```

## 4. Identificar el Exit Node

Desde el cliente:

```bash
tailscale status
```

Identificar el servidor autorizado como Exit Node.

También es posible consultar su dirección IP desde el
servidor utilizando:

```bash
tailscale ip -4
```

No utilizar direcciones de ejemplo como configuración real.

## 5. Probar manualmente el Exit Node

Antes de instalar Proxy Client es recomendable verificar
que el túnel y el Exit Node funcionan.

Definir la IP Tailscale real del servidor:

```bash
EXIT_NODE_IP="100.x.y.z"
```

Reemplazar `100.x.y.z` por la dirección correcta.

Seleccionar temporalmente el Exit Node:

```bash
sudo tailscale set --exit-node="$EXIT_NODE_IP"
```

Comprobar:

```bash
tailscale status
```

Opcionalmente, consultar la IP pública de salida:

```bash
curl -4 https://api.ipify.org
echo
```

La IP pública observada debería corresponder a la salida
a Internet del servidor, considerando la red del servidor.

Si el cliente necesita conservar acceso directo a su
red local, evaluar expresamente:

```bash
sudo tailscale set \
    --exit-node-allow-lan-access=true
```

Esto modifica el comportamiento de acceso a la LAN y no
debe habilitarse sin considerar sus implicaciones.

## 6. Devolver el control al módulo

Terminada la comprobación manual, desactivar la selección
temporal del Exit Node:

```bash
sudo tailscale set --exit-node=
```

Este comando no desconecta el equipo de la tailnet;
solamente elimina la selección del Exit Node.

Verificar:

```bash
tailscale status
```

El cliente queda conectado a Tailscale y preparado para que
NEEBLES Proxy Client administre posteriormente la selección
del Exit Node mediante sus operaciones autorizadas.

No es necesario ejecutar manualmente el comando de
selección durante el uso cotidiano del módulo.

## 7. Instalación de NEEBLES Proxy Client

Con Tailscale instalado y el Exit Node previamente probado:

1. Abrir NEEBLES Boss.
2. Instalar Proxy Client mediante Boss.
3. Abrir Config del módulo.
4. Introducir la IP Tailscale correspondiente al servidor.
5. Confirmar la dirección solicitada por Config.
6. Guardar mediante la integración con NEEBLES Secrets.
7. Comprobar que la configuración se recupera correctamente.

La dirección del servidor se administra mediante Secrets,
no mediante un archivo de texto con credenciales.

No almacenar claves privadas en el repositorio.

## 8. Funcionamiento

La activación y desactivación de Proxy Client son
operaciones gobernadas por Boss Lifecycle.

El proveedor privilegiado debe confirmar el estado real
de Tailscale.

La interfaz no debe inferir que la operación tuvo éxito
únicamente porque el usuario pulsó un botón.

El splash visual de activación o desactivación depende
del estado confirmado del Exit Node y de una sesión
gráfica disponible.

## 9. Comprobaciones

Después de instalar:

1. Abrir Config y verificar la recuperación desde Secrets.
2. Activar Proxy Client.
3. Confirmar el Exit Node real mediante `tailscale status`.
4. Verificar la salida a Internet.
5. Desactivar Proxy Client.
6. Confirmar que la selección del Exit Node desaparece.
7. Verificar la recuperación después de reiniciar el equipo.

Las pruebas deben realizarse sobre la instalación real,
no únicamente sobre simulaciones de Bridge o Lifecycle.

## 10. Referencias técnicas

- `manifest.json`: identidad y versión declarada.
- `lifecycle.json`: operaciones gobernadas.
- `surfaces.json`: interfaz declarada.
- `src/config_secrets.cpp`: integración de configuración.
- `runtime/`: ejecutables del módulo.
- `integration/systemd-user/`: servicio de usuario.
- `contracts/platform-operations.json`: operaciones
  declaradas hacia el proveedor del sistema.

La autoridad privilegiada permanece en el sistema
operativo, no en la interfaz del módulo.

Documentación oficial:

- https://tailscale.com/docs/install/linux
- https://tailscale.com/docs/features/exit-nodes
