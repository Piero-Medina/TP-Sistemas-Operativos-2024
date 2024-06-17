#include "procesar_conexion.h"

void procesar_conexion_io(void *args){
    t_procesar_conexion_args *casted_args = (t_procesar_conexion_args *)args;
    t_log* logger_k = casted_args->log; 
    int socket = casted_args->fd_cliente;
    char* nombre_servidor = casted_args->server_name;
    free(casted_args);

    char* nombre_interfaz = NULL;

    while (procesar_conexion_en_ejecucion) {
        int cod_op = recibir_operacion(socket);
        log_info(logger_k, "Se recibió el cod operacion (%d) en %s", cod_op, nombre_servidor);

        if(sistema_detenido){
            stop_io = true;
            log_info(logger, "STOP IO");
            sem_wait(&sem_stop_io);
            log_info(logger, "RESUME IO");
        }

        switch (cod_op) {
            case HANDSHAKE:
                char* modulo = recibir_handshake(socket);
                log_info(logger_k, "solicitud de Handshake del modulo: %s", modulo);
                log_info(logger_k, "respondiendo Handshake al modulo: %s \n", modulo);
                responder_handshake(socket); 
                free(modulo);
                break;
            case REGISTRO_INTERFAZ:
            {
                uint32_t tipo_interfaz;
                recibir_generico_entero_string(socket, &tipo_interfaz, &nombre_interfaz);
                log_info(logger_k, "Recibiendo nombre y tipo de Interfaz: (%s) \n", nombre_interfaz);
            
                sem_wait(&mutex_diccionario_interfaces);
                    agregar_interfaz(interfaces, nombre_interfaz, socket, (int)tipo_interfaz);
                sem_post(&mutex_diccionario_interfaces);
                
                envio_generico_op_code(socket, KERNEL_OK);
                
                sem_post(&sem_interfaz_io_libre);
                //free(nombre_interfaz);
                break;
            }
            case SOLICITUD_IO_GEN_SLEEP_FINALIZADA:
            {
                uint32_t pid = recibo_generico_entero(socket);
                
                log_info(logger, "PID: <%u> - Solicitud de IO_GEN_SLEEP Finalizada", pid);

                sem_wait(&mutex_cola_blocked);
                        t_PCB* pcb = buscar_pcb_por_pid_y_obtener((int)pid, cola_blocked->elements);
                sem_post(&mutex_cola_blocked);

                // ANTE FINALIZACION CUANDO IO ESTA EJECUTANDO - REPLICAR EN TODAS LA IO
                if(pid_pendiente_finalizacion(pid, victimas_pendientes_io)){
                    //mandar de bloque
                    sem_wait(&mutex_cola_blocked);
                        t_PCB* pcb_a_finalizar = buscar_pcb_por_pid_y_remover((int)pid, cola_blocked->elements);
                    sem_post(&mutex_cola_blocked);

                    mandar_a_exit(pcb_a_finalizar, "FINALIZADO POR CONSOLA INTERACTIVA");
                    sem_post(&sem_grado_multiprogramacion);
                }
                else{
                    // movemos el proceso a la lista de ready (segun algoritmo)
                    if((algoritmo_elegido == VRR) && (pcb->quantum != 0)){
                        mover_blocked_a_ready_aux((int)pid);
                    }
                    else{
                        mover_blocked_a_ready((int)pid);
                    } 
                }
                //

                // cambiar el estado (sacando del diccionario) (tenemos el key nombre_interfaz)
                sem_wait(&mutex_diccionario_interfaces);
                    t_interfaz* interfaz_sleep = dictionary_get(interfaces, nombre_interfaz);
                    interfaz_sleep->ocupado = false;
                    // interfaz_sleep->cola | el t_io_pendiente* asociado ya fue liberado y removido antes de realizar la llamada
                sem_post(&mutex_diccionario_interfaces);

                // post semaforo avisando que hay una interfaz de io disponible
                sem_post(&sem_interfaz_io_libre);
                break;
            }
            case -1:
                log_error(logger_k, "Cliente (%s) desconectado de %s \n", nombre_interfaz, nombre_servidor);

                sem_wait(&mutex_diccionario_interfaces);
                    quitar_interfaz(interfaces, nombre_interfaz);
                sem_post(&mutex_diccionario_interfaces);
                
                free(nombre_interfaz);
                return; // terminamos la funcion si el cliente se desconecta
            default:
                log_error(logger_k, "El codigo de operacion %d es incorrecto - %s", cod_op, nombre_servidor);
                break;
        }
    }

    // libera la conexion si el cliente no la libero al terminar la funcion
    if(!procesar_conexion_en_ejecucion) liberar_conexion(socket);
}


void procesar_conexion_cpu_dispatch(void *args){
    char* nombre_modulo_server = "CPU_DISPATCH";

    while (procesar_conexion_en_ejecucion) {
        int cod_op = recibir_operacion(conexion_cpu_dispatch); // bloqueante
        log_info(logger, "Se recibió el cod operacion (%d) de el server %s", cod_op, nombre_modulo_server);
        
        if(sistema_detenido){
            stop_cpu_dispatch = true;
            log_info(logger, "STOP CPU DISPATCH");
            sem_wait(&sem_stop_cpu_dispatch);
            log_info(logger, "RESUME CPU DISPATCH");
        }
        
        sem_wait(&mutex_conexion_cpu_dispatch);
        switch (cod_op) {
            case DESALOJO:
            {
                // TODO
                sem_wait(&mutex_proceso_en_ejecucion);
                    proceso_en_ejecucion = false;
                sem_post(&mutex_proceso_en_ejecucion);

                t_PCB* pcb = recibir_pcb(conexion_cpu_dispatch);
                sem_post(&mutex_conexion_cpu_dispatch);

                // si entra, se mando a finalizar al proceso antes de que llegue de CPU
                ////////////////////////////////////////////////////////////////////// 
                sem_wait(&mutex_cola_execute);
                if(finalizacion_execute_afuera_kernel){
                    finalizacion_execute_afuera_kernel = false;

                    t_PCB* tmp = buscar_pcb_por_pid_y_remover((int)pcb->pid, cola_execute->elements);
                    // liberamos el pcb viejo
                    liberar_PCB(tmp);
                    // mandamos el pcb recien llegado a exit (estara actualizado)
                    mandar_a_exit(pcb, "FINALIZADO POR CONSOLA INTERACTIVA");
                    sem_post(&sem_grado_multiprogramacion); 
                    sem_post(&sem_cpu_disponible);

                    sem_post(&mutex_cola_execute);
                    break; // salimos del case.
                }
                sem_post(&mutex_cola_execute);
                ////////////////////////////////////////////////////////////////////// 
                
                // IIIIII -> luego revisar si puede haber otros motivos de desalojo
                log_info(logger, "PID: <%u> - Desalojado por fin de Quantum", pcb->pid);

                if((algoritmo_elegido == VRR) && (pcb->quantum != 0)){
                    if(mover_execute_a_ready_aux(pcb) == false){
                        log_info(logger, "PID: <%u> - interceptado antes de pasar a READY AUX", pcb->pid);
                    }
                }
                else{
                    if(mover_execute_a_ready(pcb) == false){
                        log_info(logger, "PID: <%u> - interceptado antes de pasar a READY", pcb->pid); 
                    }
                }

                sem_post(&sem_cpu_disponible); 
                break;
            }
            case WAIT_KERNEL:
            {   
                sem_wait(&mutex_proceso_en_ejecucion);
                    proceso_en_ejecucion = false;
                sem_post(&mutex_proceso_en_ejecucion);

                t_PCB* pcb = recibir_pcb(conexion_cpu_dispatch);
                ignorar_op_code(conexion_cpu_dispatch);
                char* nombre_recurso = recibir_generico_string(conexion_cpu_dispatch);
                sem_post(&mutex_conexion_cpu_dispatch);

                // si entra, se mando a finalizar al proceso antes de que llegue de CPU
                ////////////////////////////////////////////////////////////////////// 
                sem_wait(&mutex_cola_execute);
                if(finalizacion_execute_afuera_kernel){
                    finalizacion_execute_afuera_kernel = false;

                    t_PCB* tmp = buscar_pcb_por_pid_y_remover((int)pcb->pid, cola_execute->elements);
                    // liberamos el pcb viejo
                    liberar_PCB(tmp);
                    // mandamos el pcb recien llegado a exit (estara actualizado)
                    mandar_a_exit(pcb, "FINALIZADO POR CONSOLA INTERACTIVA");
                    sem_post(&sem_grado_multiprogramacion); 
                    sem_post(&sem_cpu_disponible);

                    sem_post(&mutex_cola_execute);
                    break; // salimos del case.
                }
                sem_post(&mutex_cola_execute);
                ////////////////////////////////////////////////////////////////////// 

                log_info(logger, "PID: <%u> - WAIT_KERNEL", pcb->pid);

                sem_wait(&mutex_diccionario_recursos);
				if( dictionary_has_key(recursos, nombre_recurso) ){
                    // en caso que el Recurso exista 
					t_recurso* recurso = (t_recurso*)dictionary_get(recursos, nombre_recurso);

					if(recurso->instancias <= 0){
                        // mandar a la cola de bloqueados del mismo recurso

                        // sacamos la pcb vieja de execute y la liberamos, dejando cola execute vacia
                        sem_wait(&mutex_cola_execute);
                            t_PCB* pcb_vieja = (t_PCB*) queue_pop(cola_execute);
                            liberar_PCB(pcb_vieja);
                        sem_post(&mutex_cola_execute);

						// metemos la pcb actualizada, de modo que no haga falta actualizarla cuando se desbloquee
                        log_info(logger, "PID: <%u> - WAIT: (%s) - Instancias: (%d) - Se Bloquea", pcb->pid, nombre_recurso, recurso->instancias);
                        log_info(logger, "PID: <%u> - Bloqueado por: <RECURSO (%s)>", pcb->pid, nombre_recurso);
                        
                        queue_push(recurso->cola_recurso, (void*) pcb);
            
						sem_post(&sem_cpu_disponible);
					}
					else{
						recurso->instancias -= 1;
                        // devolvemos el proceso para que siga ejecutando (respetando el algoritmo)
                        // por ese motivo no dejamos la cpu libre
                        agregar_registro_recurso(pcb->pid, nombre_recurso);
                        log_info(logger, "PID: <%u> - WAIT: (%s) - Instancias: (%d)", pcb->pid, nombre_recurso, recurso->instancias);
						devolver_a_execute(pcb); 
					}
				}
				else{
                    // en caso que el recurso no exista el Proceso se manda Exit
                    char* motivo = obtener_motivo_salida(SALIDA_INVALID_RESOURCE, nombre_recurso);
                    //char* motivo = malloc(sizeof(char) * 50);
                    //snprintf(motivo, 50, "INVALID_RESOURCE (%s)", nombre_recurso); // agregar al final '\0'
                    
                    if(mover_execute_a_exit(pcb, motivo) == false){
                        log_info(logger, "PID: <%u> - interceptado antes de pasar a exit con el motivo <%s>", pcb->pid, motivo);
                    }

                    free(motivo);

					sem_post(&sem_grado_multiprogramacion);
					sem_post(&sem_cpu_disponible);
				}
				sem_post(&mutex_diccionario_recursos);

                free(nombre_recurso);

                sem_post(&mutex_conexion_cpu_dispatch);
                break;
            }
            case SIGNAL_KERNEL:
            {   
                sem_wait(&mutex_proceso_en_ejecucion);
                    proceso_en_ejecucion = false;
                sem_post(&mutex_proceso_en_ejecucion);

                t_PCB* pcb = recibir_pcb(conexion_cpu_dispatch);
                ignorar_op_code(conexion_cpu_dispatch);
                char* nombre_recurso = recibir_generico_string(conexion_cpu_dispatch);
                sem_post(&mutex_conexion_cpu_dispatch);
                
                // si entra, se mando a finalizar al proceso antes de que llegue de CPU
                ////////////////////////////////////////////////////////////////////// 
                sem_wait(&mutex_cola_execute);
                if(finalizacion_execute_afuera_kernel){
                    finalizacion_execute_afuera_kernel = false;

                    t_PCB* tmp = buscar_pcb_por_pid_y_remover((int)pcb->pid, cola_execute->elements);
                    // liberamos el pcb viejo
                    liberar_PCB(tmp);
                    // mandamos el pcb recien llegado a exit (estara actualizado)
                    mandar_a_exit(pcb, "FINALIZADO POR CONSOLA INTERACTIVA");
                    sem_post(&sem_grado_multiprogramacion); 
                    sem_post(&sem_cpu_disponible);

                    sem_post(&mutex_cola_execute);
                    break; // salimos del case.
                }
                sem_post(&mutex_cola_execute);
                ////////////////////////////////////////////////////////////////////// 
                
                log_info(logger, "PID: <%u> - SIGNAL_KERNEL", pcb->pid);

                sem_wait(&mutex_diccionario_recursos);
				if( dictionary_has_key(recursos, nombre_recurso) ){ 
                    // en caso que el Recurso exista 
					t_recurso* recurso = (t_recurso*)dictionary_get(recursos, nombre_recurso);

					recurso->instancias += 1;
					if(!queue_is_empty(recurso->cola_recurso)){
						t_PCB* pcb_desbloqueada = (t_PCB*)queue_pop(recurso->cola_recurso);

                        // mandar el proceso recien desbloqueado a Ready o Ready plus (segun el algortimo)
                        log_info(logger, "PID: <%u> - SIGNAL: (%s) - Instancias: (%d) - Desbloquea a PID <%u>", pcb->pid, nombre_recurso, recurso->instancias, pcb_desbloqueada->pid);
                        mover_a_ready_o_ready_aux(pcb_desbloqueada);
					}
					// devolvemos el proceso para que siga ejecutando (respetando el algoritmo)
                    // por ese motivo no dejamos la cpu libre
                    eliminar_registro_recurso(pcb->pid, nombre_recurso);
                    log_info(logger, "PID: <%u> - SIGNAL: (%s) - Instancias: (%d)", pcb->pid, nombre_recurso, recurso->instancias);
                    devolver_a_execute(pcb); 
				}
				else{
                    // en caso que el recurso no exista el Proceso se manda Exit
                    char* motivo = obtener_motivo_salida(SALIDA_INVALID_RESOURCE, nombre_recurso);
					//char* motivo = malloc(sizeof(char) * 50);
                    //snprintf(motivo, 50, "INVALID_RESOURCE (%s)", nombre_recurso); // agregar al final '\0'

                    if(mover_execute_a_exit(pcb, motivo) == false){
                        log_info(logger, "PID: <%u> - interceptado antes de pasar a exit con el motivo <%s>", pcb->pid, motivo);
                    }

                    free(motivo);

					sem_post(&sem_grado_multiprogramacion);
					sem_post(&sem_cpu_disponible);
				}
				sem_post(&mutex_diccionario_recursos);

                free(nombre_recurso);

                //sem_post(&mutex_conexion_cpu_dispatch);
                break;
            }
            case PETICION_IO: // mandamos a blocked, o sino a exit
            {
                // TODO
                sem_wait(&mutex_proceso_en_ejecucion);
                    proceso_en_ejecucion = false;
                sem_post(&mutex_proceso_en_ejecucion);

                t_PCB* pcb = recibir_pcb(conexion_cpu_dispatch);
                log_info(logger, "PID: <%u> - PETICION_IO", pcb->pid);

                verificar_tipo_interfaz(conexion_cpu_dispatch, pcb);
                
                //sem_post(&mutex_conexion_cpu_dispatch);
                //mover_execute_a_blocked(pcb); // aca dentro se actualiza el contexto de ejecucion
                //hilo_procesar_io_fake(3000);

                sem_post(&sem_cpu_disponible); 
                break;
            }
            case PROCESO_FINALIZADO:
            {
                sem_wait(&mutex_proceso_en_ejecucion);
                    proceso_en_ejecucion = false;
                sem_post(&mutex_proceso_en_ejecucion);
                
                t_PCB* pcb = recibir_pcb(conexion_cpu_dispatch);
                int motivo_salida = recibo_generico_op_code(conexion_memoria);
                sem_post(&mutex_conexion_cpu_dispatch);

                // si entra, se mando a finalizar al proceso antes de que llegue de CPU
                ////////////////////////////////////////////////////////////////////// 
                sem_wait(&mutex_cola_execute);
                if(finalizacion_execute_afuera_kernel){
                    finalizacion_execute_afuera_kernel = false;

                    t_PCB* tmp = buscar_pcb_por_pid_y_remover((int)pcb->pid, cola_execute->elements);
                    // liberamos el pcb viejo
                    liberar_PCB(tmp);
                    // mandamos el pcb recien llegado a exit (estara actualizado)
                    mandar_a_exit(pcb, "FINALIZADO POR CONSOLA INTERACTIVA");
                    sem_post(&sem_grado_multiprogramacion); 
                    sem_post(&sem_cpu_disponible);

                    sem_post(&mutex_cola_execute);
                    break; // salimos del case.
                }
                sem_post(&mutex_cola_execute);
                ////////////////////////////////////////////////////////////////////// 

                // posibles: success|out_of_memory|segmentation_fault
                char* motivo = obtener_motivo_salida(motivo_salida, NULL);
                if(mover_execute_a_exit(pcb, motivo) == false){
                    log_info(logger, "PID: <%u> - interceptado antes de pasar a exit con el motivo <%s>", pcb->pid, motivo);
                }
                free(motivo);

                sem_post(&sem_cpu_disponible);

                sem_post(&sem_grado_multiprogramacion);
                break;
            }
            case -1:
                log_error(logger, "el server %s cerro la conexion", nombre_modulo_server);
                sem_post(&mutex_conexion_cpu_dispatch);
                return; // finalizando hilo
            default:
                log_error(logger, "El codigo de operacion %d es incorrecto - %s", cod_op, nombre_modulo_server);
                sem_post(&mutex_conexion_cpu_dispatch);
                return; // finalizando hilo
        }
    }
}


void hilo_procesar_io_fake(int milisegundos){
    pthread_t hilo_durmicion;
    int* milisegundos_ptr = malloc(sizeof(int));
    *milisegundos_ptr = milisegundos;
    pthread_create(&hilo_durmicion, NULL, (void*)procesar_io_fake, (void*) milisegundos_ptr);
    pthread_detach(hilo_durmicion);
}

void procesar_io_fake(void* args){
    int* milisegundos = (int*) args;
    sleep_ms(*milisegundos);

    //mover_blocked_a_ready();

    free(milisegundos);
}
