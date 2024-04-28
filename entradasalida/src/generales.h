#ifndef GENERALES_H_
#define GENERALES_H_

#include <commons/log.h>
#include "entradaSalida_config.h"
#include <stdbool.h>

#include <enum/enum.h>

// declarado en main.c
extern t_log* logger;
extern t_entradaSalida_config* config;

extern tipo_interfaz tipo_de_interfaz;

extern int conexion_kernel;
extern int conexion_memoria;

#endif 