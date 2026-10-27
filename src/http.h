// Pedidos HTTP al panel en una tarea aparte: la conexión segura tarda 1 a 2 s y no
// puede trancar la pantalla. panel.cpp deja un pedido y lo recoge listo más tarde.
// En la placa lo hace http.cpp; en el PC, sim/sim_http.cpp.
#pragma once
#include <Arduino.h>

struct PedidoHttp {
  uint8_t tipo = 0;    // qué era (lo usa panel.cpp)
  uint32_t ref = 0;    // ídem
  String metodo;       // GET, POST, DELETE
  String url;          // completa
  String llave;        // va como "Authorization: Bearer ..."
  String cuerpo;       // JSON o vacío
  String archivo;      // si no está vacío, la respuesta va a ese archivo (fotos) y no a "respuesta"
  int codigo = 0;      // respuesta: 200, 404... o negativo si no hubo conexión
  String respuesta;
};

void http_iniciar();
bool http_enviar(PedidoHttp* p);  // la tarea se queda con él; false si no lo pudo tomar
PedidoHttp* http_recoger();       // el pedido terminado (hay que borrarlo) o nullptr
