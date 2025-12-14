=========================================================================
GUÍA DE ARQUITECTURA Y DESARROLLO - COMPONENTE EEZ_AIoT
=========================================================================

1. ESTRUCTURA DE CARPETAS
-------------------------------------------------------------------------
Este componente se divide en dos secciones críticas:

A. CARPETA /ui/  (ZONA GENERADA - NO TOCAR)
   - Contiene: screens.c, screens.h, images/, styles.c, etc.
   - Origen: Generado automáticamente por EEZ Studio.
   - REGLA: NUNCA editar estos archivos manualmente. 
     Cualquier cambio se perderá la próxima vez que presiones "Generate" 
     en EEZ Studio.

B. CARPETA /src/ (ZONA DE LÓGICA - TU CÓDIGO)
   - Contiene: actions.cpp, vars.cpp y lógica de negocio.
   - Origen: Creado y mantenido por el desarrollador.
   - REGLA: Aquí es donde implementas las funciones que EEZ Studio solicita.

2. FLUJO DE TRABAJO (WORKFLOW)
-------------------------------------------------------------------------
1. En EEZ Studio: Creas la interfaz, agregas botones y Defines VARIABLES.
2. En EEZ Studio: Botón "Generate".
3. En el Código (/src/):
   - Si creaste una "Acción" nueva: Implementa la función en actions.cpp
   - Si creaste una "Variable" nueva: Implementa los get/set en vars.cpp

3. USO DE VARIABLES (DATA BINDING)
-------------------------------------------------------------------------
En lugar de leer widgets directamente (ej. lv_textarea_get_text), usamos
el sistema de variables de EEZ:

- vars.cpp: Mantiene el estado de la variable (usando std::string o bool).
- EEZ Studio: Vincula el widget (Data) a la variable global.
- Logic: Solo llama a get_var_nombre() para obtener el valor limpio.

=========================================================================