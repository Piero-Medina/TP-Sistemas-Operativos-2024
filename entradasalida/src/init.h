#ifndef INIT_H_
#define INIT_H_

#include <stdlib.h>
#include <string.h>
#include <commons/log.h>
#include <commons/config.h>
#include <pthread.h>
#include <signal.h>

#include "entradaSalida_config.h"
#include "generales.h"
#include "procesar_conexion.h"

#include <enum/enum.h>

void init_entrada_salida(void);
void liberar_entrada_salida(void);
void sigint_handler(int signum);

// setea el tipo de interfaz elegido
void tipo_de_interfaz_elegido(char* path);

// configura al modulo segun el tipo de interfaz
void configurar_segun_tipo_de_interfaz(void);

#endif 