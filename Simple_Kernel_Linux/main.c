#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>


/*
Creacion de un modulo simple de Kernel linux con carga, descarga y mensajes de aviso
*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roberto Hernandez");
MODULE_DESCRIPTION("Modelo simple de Kernel linux");
MODULE_VERSION("0.1");



/*
printk: Similar a printf, pero para el kernel. Los mensajes van al log del sistema.
KERN_INFO: Nivel de log (INFO, WARNING, ERR)
module_init() y module_exit(): Macros que registran las funciones de carga/descarga del módulo.
__init y __exit: Optimizaciones que permiten liberar memoria después de usar estas funciones

*/

/* Funcion inicial que se ejecuta cuando se carga el modulo */


static int __init simple_module_init(void) {
    printk(KERN_INFO "Modulo cargado exitosamente\n");
    return 0; // Indica que la carga fue exitosa
}


/*Funcion que se ejecuta cuando se descarga el modulo*/

static void __exit simple_module_exit(void) {
    printk(KERN_INFO "Modulo descargado exitosamente\n");
}


/* Macros para indicar las funciones de inicio y salida del modulo */
module_init(simple_module_init);
module_exit(simple_module_exit);



