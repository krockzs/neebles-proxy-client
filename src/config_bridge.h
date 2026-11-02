#pragma once

// Servidor efimero de consulta de configuracion para el proveedor OS de Boss.
// Solamente responde a una solicitud de root autenticado por SO_PEERCRED.
namespace ProxyConfigBridge {
int serve(int argc, char **argv);
}
