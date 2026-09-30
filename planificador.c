#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

#define MAX_LINE 1024
#define MAX_FIELDS 16


typedef struct {
    char id[64];
    char nombre[128];
    int tiempo;

    char **dependencias;
    int num_dependencias;

    int *dependencias_idx;
    int num_dependencias_idx;

    int *dependientes;
    int num_dependientes;

    int deps_pendientes;

    int estado;
    pid_t pid;
    int pipe_lectura_padre[2];
    int pipe_lectura_hijo[2];  
} Tarea;

typedef struct {
    Tarea *tareas;
    int num_tareas;
    int capacidad;
} Plan;

char *recortar(char *texto) {
    while (isspace((unsigned char)*texto)) texto++;
    if (*texto == '\0') return texto;
    char *finalizado = texto + strlen(texto) - 1;
    while (finalizado > texto && isspace((unsigned char)*finalizado)) finalizado--;
    finalizado[1] = '\0';
    return texto;
}

int dividir_por_dos_ptos(char *texto, char **campos, int max) {
    int n = 0;
    char *inicio = texto;
    for (char *puntero = texto; ; puntero++) {
        if (*puntero == ':' || *puntero == '\0') {
            int fin = (*puntero == '\0');
            *puntero = '\0';
            if (n < max) campos[n] = inicio;
            n++;
            if (fin) break;
            inicio = puntero + 1;
        }
    }
    return n;
}

void leer_actividad(char *linea, Plan *plan) {
    char *p = recortar(linea);
    if (*p == '\0') return;

    char *campos[MAX_FIELDS];
    int n = dividir_por_dos_ptos(p, campos, MAX_FIELDS);
    if (n < 2) return;

    Tarea t;
    memset(&t, 0, sizeof(Tarea));

    strncpy(t.id, recortar(campos[0]), sizeof(t.id) - 1);
    strncpy(t.nombre, recortar(campos[1]), sizeof(t.nombre) - 1);

    if (n >= 3) {
        char *ts = recortar(campos[2]);
        if (*ts != '\0') t.tiempo = atoi(ts);
        else t.tiempo = 100 + rand() % 4901;
    } else {
        t.tiempo = 100 + rand() % 4901;
    }

    t.num_dependencias = 0;
    t.dependencias = NULL;
    if (n >= 4) {
        char *ds = recortar(campos[3]);
        if (*ds != '\0') {
            int cuantas = 1;
            for (char *c = ds; *c; c++) if (*c == ',') cuantas++;
            t.dependencias = malloc(cuantas * sizeof(char *));
            t.num_dependencias = 0;
            char *tok = strtok(ds, ",");
            while (tok) {
                t.dependencias[t.num_dependencias++] = strdup(recortar(tok));
                tok = strtok(NULL, ",");
            }
        }
    }

    if (plan->num_tareas >= plan->capacidad) {
        plan->capacidad = plan->capacidad ? plan->capacidad * 2 : 16;
        plan->tareas = realloc(plan->tareas, plan->capacidad * sizeof(Tarea));
    }
    plan->tareas[plan->num_tareas++] = t;
}

void leer_plan(const char *archivo, Plan *plan) {
    FILE *f = fopen(archivo, "r");
    if (!f) { perror("fopen"); exit(1); }
    char linea[MAX_LINE];
    while (fgets(linea, sizeof(linea), f)) leer_actividad(linea, plan);
    fclose(f);
}

int buscar_indice(Plan *plan, const char *id_buscado) {
    for (int i = 0; i < plan->num_tareas; i++)
        if (strcmp(plan->tareas[i].id, id_buscado) == 0) return i;
    return -1;
}

void mapear_dependencias(Plan *plan) {
    for (int i = 0; i < plan->num_tareas; i++) {
        Tarea *t = &plan->tareas[i];
        t->num_dependencias_idx = 0;
        t->dependencias_idx = NULL;
        if (t->num_dependencias == 0) continue;
        t->dependencias_idx = malloc(t->num_dependencias * sizeof(int));
        for (int j = 0; j < t->num_dependencias; j++) {
            int idx = buscar_indice(plan, t->dependencias[j]);
            if (idx == -1) {
                fprintf(stderr, "Error: la actividad '%s' depende de '%s', que no existe\n",
                        t->id, t->dependencias[j]);
                exit(1);
            }
            t->dependencias_idx[t->num_dependencias_idx++] = idx;
        }
    }
}

void construir_dependientes(Plan *plan) {
    for (int i = 0; i < plan->num_tareas; i++) {
        plan->tareas[i].num_dependientes = 0;
        plan->tareas[i].dependientes = NULL;
    }
    for (int i = 0; i < plan->num_tareas; i++)
        for (int j = 0; j < plan->tareas[i].num_dependencias_idx; j++)
            plan->tareas[plan->tareas[i].dependencias_idx[j]].num_dependientes++;
    for (int i = 0; i < plan->num_tareas; i++) {
        Tarea *t = &plan->tareas[i];
        if (t->num_dependientes > 0)
            t->dependientes = malloc(t->num_dependientes * sizeof(int));
        t->num_dependientes = 0;
    }
    for (int i = 0; i < plan->num_tareas; i++)
        for (int j = 0; j < plan->tareas[i].num_dependencias_idx; j++) {
            Tarea *dep = &plan->tareas[plan->tareas[i].dependencias_idx[j]];
            dep->dependientes[dep->num_dependientes++] = i;
        }
}

void inicializar_deps_pendientes(Plan *plan) {
    for (int i = 0; i < plan->num_tareas; i++)
        plan->tareas[i].deps_pendientes = plan->tareas[i].num_dependencias_idx;
}

int hay_ciclo(Plan *plan) {
    int *pendientes = malloc(plan->num_tareas * sizeof(int));
    for (int i = 0; i < plan->num_tareas; i++)
        pendientes[i] = plan->tareas[i].num_dependencias_idx;

    int *cola = malloc(plan->num_tareas * sizeof(int));
    int ini = 0, fin = 0;
    for (int i = 0; i < plan->num_tareas; i++)
        if (pendientes[i] == 0) cola[fin++] = i;

    int procesadas = 0;
    while (ini < fin) {
        int actual = cola[ini++];
        procesadas++;
        Tarea *t = &plan->tareas[actual];
        for (int j = 0; j < t->num_dependientes; j++) {
            int dep_idx = t->dependientes[j];
            if (--pendientes[dep_idx] == 0) cola[fin++] = dep_idx;
        }
    }
    free(pendientes);
    free(cola);
    return procesadas != plan->num_tareas;
}

static volatile sig_atomic_t interrumpido = 0;
void handler_sigint(int sig) { (void)sig; interrumpido = 1; }

void lanzar_tarea(Plan *plan, int idx, int *activos) {
    Tarea *t = &plan->tareas[idx];

    if (pipe(t->pipe_lectura_padre) == -1) { perror("pipe"); exit(1); }
    if (pipe(t->pipe_lectura_hijo) == -1)  { perror("pipe"); exit(1); }

    pid_t pid = fork();
    if (pid == -1) { perror("fork"); exit(1); }

    if (pid == 0) {
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) max_fd = 1024;

    for (int fd = 3; fd < max_fd; fd++) {
        if (fd == t->pipe_lectura_padre[1]) continue;
        if (fd == t->pipe_lectura_hijo[0]) continue;
        close(fd);
    }

        char insumo[512];
        ssize_t n = read(t->pipe_lectura_hijo[0], insumo, sizeof(insumo) - 1);
        if (n <= 0) { close(t->pipe_lectura_padre[1]); exit(3); }
        insumo[n] = '\0';
        printf("[hijo %s] recibido: %s", t->id, insumo);

        if (strstr(t->nombre, "fallar") != NULL) {
        fprintf(stderr, "[hijo %s] FALLANDO A PROPOSITO\n", t->id);
        close(t->pipe_lectura_padre[1]);
        close(t->pipe_lectura_hijo[0]);
        exit(1);
    }

        struct timespec ts;
        ts.tv_sec = t->tiempo / 1000;
        ts.tv_nsec = (t->tiempo % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        char aviso[256];
        int len = snprintf(aviso, sizeof(aviso), "OK %s %d\n", t->id, t->tiempo);
        write(t->pipe_lectura_padre[1], aviso, len);

        close(t->pipe_lectura_padre[1]);
        close(t->pipe_lectura_hijo[0]);
        exit(0);
    }

    close(t->pipe_lectura_padre[1]);   
    close(t->pipe_lectura_hijo[0]);

    t->pid = pid;
    t->estado = 1;
    (*activos)++;

    char insumo[512];
    int len = snprintf(insumo, sizeof(insumo), "INSUMO %s:", t->id);
    for (int i = 0; i < t->num_dependencias_idx; i++) {
        len += snprintf(insumo + len, sizeof(insumo) - len, " %s",
                        plan->tareas[t->dependencias_idx[i]].id);
    }
    len += snprintf(insumo + len, sizeof(insumo) - len, "\n");
    write(t->pipe_lectura_hijo[1], insumo, len);
    close(t->pipe_lectura_hijo[1]);
}

void coordinar(Plan *plan, int K) {
    int total = plan->num_tareas;
    int terminadas = 0;
    int activos = 0;

    struct pollfd *fds = malloc(K * sizeof(struct pollfd));
    int *mapa = malloc(K * sizeof(int));
    if (!fds || !mapa) {
        perror("malloc");
        exit(1);
    }

    while (terminadas < total && !interrumpido) {

        for (int i = 0; i < total && activos < K; i++) {
            Tarea *t = &plan->tareas[i];
            if (t->estado == 0 && t->deps_pendientes == 0)
                lanzar_tarea(plan, i, &activos);
        }

        if (activos == 0) break;

        int nfds = 0;
        for (int i = 0; i < total && nfds < K; i++) {
            if (plan->tareas[i].estado == 1) {
                fds[nfds].fd = plan->tareas[i].pipe_lectura_padre[0];
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                mapa[nfds] = i;
                nfds++;
            }
        }

        int listos = poll(fds, nfds, 100);
        if (listos == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        for (int k = 0; k < nfds; k++) {
            if (!(fds[k].revents & (POLLIN | POLLHUP))) continue;
            int idx = mapa[k];
            Tarea *t = &plan->tareas[idx];

            char buffer[512];
            ssize_t n = read(t->pipe_lectura_padre[0], buffer, sizeof(buffer) - 1);
            int ok = 0;
            if (n > 0) {
                buffer[n] = '\0';
                if (strncmp(buffer, "OK ", 3) == 0) ok = 1;
                printf("[padre] aviso de %s: %s", t->id, buffer);
            }
            close(t->pipe_lectura_padre[0]);

            int status;
            waitpid(t->pid, &status, 0);
            activos--;

            if (ok && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                t->estado = 2;
                terminadas++;
                for (int j = 0; j < t->num_dependientes; j++)
                    plan->tareas[t->dependientes[j]].deps_pendientes--;
            } else {
                fprintf(stderr, "[padre] tarea %s FALLÓ\n", t->id);
                t->estado = 3;
                terminadas++;

                int *cola = malloc(total * sizeof(int));
                int ini = 0, fin = 0;
                for (int j = 0; j < t->num_dependientes; j++)
                    cola[fin++] = t->dependientes[j];
                while (ini < fin) {
                    int actual = cola[ini++];
                    Tarea *d = &plan->tareas[actual];
                    if (d->estado == 0) {
                        d->estado = 4;
                        terminadas++;
                        for (int j = 0; j < d->num_dependientes; j++)
                            cola[fin++] = d->dependientes[j];
                    }
                }
                free(cola);
            }
        }
    }

    if (interrumpido) {
        printf("\n[padre] SIGINT recibido, abortando todas las actividades...\n");

        for (int i = 0; i < total; i++)
            if (plan->tareas[i].estado == 1)
                kill(plan->tareas[i].pid, SIGTERM);

        for (int i = 0; i < total; i++)
            if (plan->tareas[i].estado == 1) {
                waitpid(plan->tareas[i].pid, NULL, 0);
                plan->tareas[i].estado = 4;
            }

        for (int i = 0; i < total; i++)
            if (plan->tareas[i].estado == 0)
                plan->tareas[i].estado = 4;
    }

    free(fds);
    free(mapa);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return 1;
    }
    srand(time(NULL));

    Plan plan = {0};
    leer_plan(argv[1], &plan);

    int K = atoi(argv[2]);
    if (K <= 0) {
    fprintf(stderr, "Error: K debe ser mayor que 0\n");
    return 1;
    }
    printf("K = %d\n", K);
    fflush(stdout);

    mapear_dependencias(&plan);
    construir_dependientes(&plan);
    inicializar_deps_pendientes(&plan);

    if (hay_ciclo(&plan)) {
        fprintf(stderr, "Error: el plan tiene un ciclo, no se puede ejecutar\n");
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handler_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    coordinar(&plan, K);

    printf("\n=== Resumen ===\n");
    int ok = 0, err = 0, abort = 0, pend = 0;
    for (int i = 0; i < plan.num_tareas; i++) {
        if      (plan.tareas[i].estado == 2) ok++;
        else if (plan.tareas[i].estado == 3) err++;
        else if (plan.tareas[i].estado == 4) abort++;
        else pend++;
    }
    printf("OK: %d | Error: %d | Abortadas: %d | Pendientes: %d\n",
           ok, err, abort, pend);

    return 0;
}