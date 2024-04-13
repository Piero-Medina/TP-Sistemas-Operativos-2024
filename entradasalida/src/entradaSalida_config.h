#ifndef ENTRADA_SALIDA_CONFIG_H
#define ENTRADA_SALIDA_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <commons/config.h>

typedef struct {
    t_config* config;
    char* tipo_interfaz;
    int tiempo_unidad_trabajo;
    char* ip_kernel;
    char* puerto_kernel;
    char* ip_memoria;
    char* puerto_memoria;
    char* path_base_dialfs;
    int block_size;
    int block_count;
} t_entradaSalida_config;

t_entradaSalida_config* init_entradaSalida_config(char* path);

void liberar_entradaSalida_config(t_entradaSalida_config* entradaSalida_config);

#endif
