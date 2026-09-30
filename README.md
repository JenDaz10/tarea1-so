# Tarea 1 — Procesos, Tuberías y Señales

**Integrante:** Yeny Díaz ([yeny.diaz@mail.udp.cl](mailto:yeny.diaz@mail.udp.cl))

## Descripción

El programa corresponde a un simulador y planificador de actividades representadas mediante un Grafo Acíclico Dirigido (DAG).

El programa lee un archivo `plan.txt`, donde se indican las actividades, su duración y las dependencias entre ellas. Cada actividad se ejecuta en un proceso hijo y debe respetar el orden dado por sus dependencias.

También se utiliza un límite `K` para controlar la cantidad máxima de procesos que pueden estar ejecutándose al mismo tiempo.

## Uso

El programa se ejecuta indicando el archivo del plan y el límite de concurrencia:

```bash
./planificador plan.txt K
```

Por ejemplo:

```bash
./planificador plan.txt 3
```

En este caso, el programa utiliza `plan.txt` como archivo de entrada y permite hasta 3 procesos ejecutándose de forma concurrente.

El valor de `K` debe ser mayor que 0. Si se entrega un valor inválido, el programa muestra un mensaje de error y termina.

## Formato del archivo de entrada

Cada línea del archivo representa una actividad y tiene el siguiente formato:

```text
ID_Actividad : Nombre_Actividad : tiempo_ms : [Dependencia1, Dependencia2, ...]
```

Si el campo del tiempo está vacío, se genera un valor aleatorio entre 100 y 5000 milisegundos.

Si no se indican dependencias, la actividad puede comenzar directamente.

Por ejemplo:

```text
1 : prender_carbon : 500 :
2 : comprar_carne : 1200 :
3 : comprar_pan : 300 :
4 : asar_longaniza : 800 : 1, 2
5 : armar_choripan : 250 : 3, 4
6 : servir_mesa : 100 : 5
```

En este caso, la actividad `4` no puede comenzar hasta que terminen las actividades `1` y `2`.

Los identificadores se manejan como texto, ya que pueden ser alfanuméricos.

## Compilación

El programa se compila utilizando:

```bash
gcc -Wall -Wextra -std=c17 -o planificador planificador.c
```

## Decisiones de diseño

### Parseo del archivo

Para leer cada línea se implementó una función propia que separa los campos utilizando `:`.

Se decidió no utilizar `strtok`, porque el formato permite que algunos campos estén vacíos. En ese caso, se necesita conservar los separadores para no perder la posición de los campos.

Los IDs se guardan inicialmente como texto. Después, cuando se construye el grafo, se buscan las actividades correspondientes y se utilizan sus índices internos para trabajar con los arreglos.

### Construcción del DAG

Para cada actividad se guardan sus dependencias y también las actividades que dependen de ella.

Además, se mantiene el grado de entrada de cada actividad, que corresponde a la cantidad de dependencias que todavía no han terminado.

Para revisar si existen ciclos se utiliza el algoritmo de Kahn. Si después de recorrer el grafo quedan actividades sin procesar, significa que existe un ciclo y el programa termina mostrando un mensaje de error.

### Creación de procesos y control de concurrencia

El proceso principal funciona como coordinador y crea un proceso hijo para cada actividad que debe ejecutarse.

Antes de crear un nuevo hijo, el padre revisa la cantidad de procesos que se encuentran activos. Si ya se alcanzó el límite `K`, espera a que alguno termine antes de continuar.

El estado del DAG es manejado por el proceso padre. Los hijos solamente realizan la simulación de su actividad y se comunican con el padre mediante pipes. De esta forma, los procesos hijos no modifican directamente las estructuras del DAG.

Para esperar los mensajes de los procesos se utiliza `poll()`, evitando que el padre tenga que estar revisando constantemente si algún hijo terminó.

### Comunicación mediante pipes

Cada actividad tiene dos pipes:

* Un pipe desde el hijo hacia el padre, utilizado para informar que la actividad terminó.
* Un pipe desde el padre hacia el hijo, utilizado para enviar la información correspondiente a sus dependencias.

Cuando una actividad termina correctamente, el hijo envía un mensaje con información de la actividad, por ejemplo:

```text
OK <id> <tiempo>
```

Al crear un hijo, se cierran los descriptores de los pipes que no necesita. Esto permite que cada proceso mantenga abiertos solamente los descriptores que utiliza.

### Aislamiento de errores

Si una actividad falla, el proceso padre marca esa actividad como fallida y busca las actividades que dependen de ella.

Estas actividades también se marcan como abortadas, ya que no pueden ejecutarse correctamente si una de sus dependencias falló.

La búsqueda se realiza recorriendo las actividades dependientes. Las ramas del DAG que no dependen de la actividad que falló pueden continuar normalmente.

De esta manera, un error no provoca que se detenga todo el plan.

### Manejo de SIGINT

Para manejar `Ctrl+C` se utiliza `sigaction`.

El manejador de la señal solamente cambia una bandera. Luego, cuando el proceso principal vuelve a ejecutar su flujo normal, revisa esa bandera.

Si se recibió `SIGINT`, el padre envía `SIGTERM` a los procesos hijos activos, espera que terminen con `waitpid`, y marca como abortadas tanto esas actividades como las que aún no habían comenzado. De esta forma, al recibir la señal, ninguna actividad queda en estado pendiente.

Finalmente, el programa imprime el resumen con el estado de todas las actividades.

### Simulación de duración

Para simular el tiempo de ejecución de cada actividad se utiliza `nanosleep()`.

El tiempo indicado en milisegundos se transforma a segundos y nanosegundos para poder utilizarlo con esta función.

## Pruebas realizadas

Se realizaron las siguientes pruebas:

* **Plan básico:** se ejecutó un plan de 7 actividades y se verificó que las dependencias se respetaran.
* **Plan con ciclo:** el programa detectó el ciclo y no comenzó la ejecución.
* **Dependencia inexistente:** el programa detectó la referencia inválida y terminó mostrando el error.
* **Actividad fallida:** se comprobó que la rama que dependía de la actividad fallida fuera abortada, mientras las actividades independientes continuaron.
* **Ctrl+C:** se ejecutó un plan largo y se presionó `Ctrl+C`. Los procesos activos fueron terminados y el programa mostró el resumen final.
* **Prueba de estrés:** se ejecutó un plan de 10000 actividades con `K = 50`. La ejecución demoró aproximadamente 13,1 segundos y se completaron las 10000 actividades sin errores.

## Restricciones

El programa no utiliza hilos ni mecanismos de sincronización de hilos.

La concurrencia se implementa utilizando procesos, principalmente mediante `fork()`, `pipe()` y `poll()`.

La compilación se realizó utilizando:

```bash
gcc -Wall -Wextra -std=c17
```

y no se obtuvieron advertencias.

## Uso de inteligencia artificial

Se utilizó asistencia de IA como apoyo durante el desarrollo para discutir algunas decisiones de diseño y resolver dudas puntuales sobre la implementación.

El código fue revisado, ejecutado y adaptado por la integrante del grupo.

## Estructura del repositorio

* `planificador.c`: código fuente del programa.
* `plan.txt`: archivo de ejemplo utilizado para ejecutar el programa.
* `plan_*.txt`: archivos utilizados para realizar distintas pruebas.
* `README.md`: documentación del proyecto.
* `.gitignore`: archivos y carpetas que no se incluyen en el repositorio.