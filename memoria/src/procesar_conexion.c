#include "procesar_conexion.h"

void procesar_conexion_general(void *args){
    t_procesar_conexion_args *casted_args = (t_procesar_conexion_args *)args;
    t_log* logger_m = casted_args->log; 
    int socket = casted_args->fd_cliente;
    char* nombre_servidor = casted_args->server_name;
    free(casted_args);

    while (procesar_conexion_en_ejecucion) {
        int cod_op = recibir_operacion(socket);
        log_info(logger_m, "Se recibió el cod operacion %d en %s", cod_op, nombre_servidor);
        switch (cod_op) {
            case HANDSHAKE:
                // Hacer algo si el código de operación es 1 (usar ENUM)
                char* modulo = recibir_handshake(socket);
                log_info(logger_m, "solicitud de Handshake del modulo: %s", modulo);
                log_info(logger_m, "respondiendo Handshake al modulo: %s \n", modulo);
                responder_handshake(socket);
                free(modulo);
                break;
            case NUEVO_PROCESO_MEMORIA: // KERNEL
                log_info(logger_m, "solicitud de Nuevo Proceso del KERNEL \n");
                kernel_creacion_nuevo_proceso(socket);
                envio_generico_op_code(socket, MEMORIA_OK);
                break;
            case -1:
                log_error(logger_m, "Cliente desconectado de %s", nombre_servidor);
                //return EXIT_FAILURE -- si queremos terminar el server apenas alguien se desconecte
                //break;
                return; // terminamos la funcion si el cliente se desconecta
            default:
                log_error(logger_m, "El codigo de operacion %d es incorrecto - %s", cod_op, nombre_servidor);
                break;
        }
    }

    // libera la conexion si el cliente no la libero al terminar la funcion
    if(!procesar_conexion_en_ejecucion) liberar_conexion(socket);
}
