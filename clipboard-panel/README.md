# Portapapeles

Panel de historial para KDE Plasma 6 (Wayland), con tarjetas de texto e imágenes.
**Elegir una tarjeta solo la copia y cierra el panel. Después tú presionas Ctrl+V.**
No pega automáticamente ni simula teclas.

Meta+V abre el panel junto al puntero. La copia más reciente aparece arriba.
Elegir una tarjeta, fijarla o reiniciar el servicio nunca cambia su posición.
El panel guarda la fecha original y un número de secuencia inmutables en su propio
historial; no usa el orden ni la fecha de último uso de Klipper. Los empates se
resuelven por el identificador estable. Una copia idéntica mantiene su posición
original, incluso si Klipper la había expulsado. Si un archivo de Klipper todavía
no terminó de guardarse, se vuelve a intentar sin cambiar la fecha original.

- La búsqueda «Buscar…» siempre está visible y mantiene el foco. Escribir filtra al instante.
- ↑ / ↓: recorrer tarjetas; RePág / AvPág: avanzar por páginas. Enter o clic: elegir y cerrar.
- Inicio / Fin: primera / última tarjeta si la búsqueda está vacía; con texto, mover el cursor.
- Supr: eliminar la tarjeta seleccionada si la búsqueda está vacía; con texto, borrar hacia delante.
  Ctrl+P: fijar o desfijar, también al filtrar. Tab y Mayús+Tab mantienen el foco en la búsqueda.
- Esc borra el filtro; otro Esc cierra. Durante la autorización de polkit, Esc la cancela.
- Arrastrar la fila del título: mover la ventana durante esa apertura. La posición no se guarda;
  cada Meta+V vuelve a abrir junto al puntero.
- Arrastrar cualquier borde o esquina: cambiar el tamaño; se recuerda al reabrir y reiniciar.
- Borrar todo: solicita tu contraseña de Linux mediante el agente de polkit de Plasma.
  Cancelar o fallar la autorización no borra nada; los fijados siempre se conservan.
- Al pasar el puntero: botones Eliminar y Fijar.
- «Último elegido» marca una tarjeta sin moverla.

**Eliminar y Borrar todo afectan solo a este panel.** No borran el historial ni
el portapapeles de Klipper. Borrar todo conserva los fijados. Las tarjetas
eliminadas no reaparecen por una nueva lectura o una copia idéntica: se conserva
su identificador como registro de eliminación. El límite es de 100 tarjetas no
fijadas, más todos los fijados. Se preservan los formatos MIME disponibles en
Klipper (incluidos HTML, enlaces y PNG), y se ofrece también la imagen decodificada
a las aplicaciones Qt. Los formatos que Klipper no guardó no pueden recuperarse.

El panel consulta Klipper cada segundo, siempre con una conexión SQLite de solo
lectura, sin modificar su base ni sus archivos. Klipper debe estar activo y
configurado para conservar los tipos deseados. El historial propio está en
`$XDG_DATA_HOME/clipboard-panel/` (por defecto `~/.local/share/clipboard-panel/`),
con directorio 0700 y base 0600. Puede contener información sensible: no se
registra su contenido en los logs. Las cargas de más de 128 MiB por tarjeta se
omiten. Borrar elimina las cargas del historial propio, sin prometer borrado
forense de los bloques de SQLite o del disco.

El aspecto usa los colores de la paleta del sistema. Es una ventana de herramienta
normal, sin marco y redimensionable. Empieza con 560 px de ancho y se ajusta al
contenido hasta 760 px de alto (mínimo 360 × 240, salvo pantallas más pequeñas).
Al redimensionarla, conserva ancho y alto en `~/.config/clipboard-panelrc`
(o `$XDG_CONFIG_HOME/clipboard-panelrc`); el historial pasa a desplazarse dentro
de ese tamaño. Al abrir, se limita al área disponible del monitor del puntero.
Las tarjetas, el texto y las miniaturas se adaptan al ancho; las miniaturas no
superan 200 px de alto. La barra de desplazamiento tiene un área de agarre de 14 px;
el indicador se ensancha al pasar el puntero y se resalta al arrastrar. Los colores
se actualizan al cambiar la paleta del sistema. No utiliza layer-shell.
Un script transitorio de KWin recuerda la ventana activa, coloca únicamente el
panel en el monitor del puntero y lo excluye de la barra de tareas y Alt+Tab.
Al elegir o cerrar con Esc, intenta devolver el foco a la ventana anterior.
Al hacer clic fuera, respeta la ventana que acabas de elegir. Si KWin no puede
preparar la ventana, el panel no intenta cambiar la configuración de pantallas.

## Compilar y probar sin tocar el escritorio

Dependencias en Ubuntu/KDE: compilador C++20, CMake, Qt6 Core/Gui/Widgets/DBus/Sql/Test,
KF6GuiAddons, KF6GlobalAccel, Python3, SQLite de Qt, systemd/busctl, pkcheck,
un agente de polkit activo y sudo sin contraseña para instalar la política.

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-wayland libqt6sql6-sqlite libkf6guiaddons-dev libkf6globalaccel-dev python3 systemd
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
heavy cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen CLIPBOARD_PANEL_RENDER_DIR="$HOME/.cache/agent-scratch/clipboard-panel/build-check" heavy ctest --test-dir build --output-on-failure
./install.sh --dry-run
```

Si `heavy` no está instalado, omite esa palabra. Las pruebas usan bases y archivos
sintéticos temporales. Las imágenes se renderizan con Qt offscreen: no abren una
ventana en Wayland, no registran atajos y no acceden al portapapeles. La prueba
visual lee únicamente los colores de `kdeglobals`, porque Qt offscreen no carga
el tema de Plasma. Los PNG tienen escala 2×. No ejecutar `--daemon` como parte de
una comprobación mientras el usuario trabaja en el escritorio.

## Instalar, solo en una prueba acordada

```bash
./install.sh --build-dir build
```

Sin `--build-dir` el instalador compila en una carpeta temporal privada.
Primero comprueba quién tiene Meta+V y aborta si pertenece a otra acción distinta
a Klipper o a este panel. Antes de cambiar nada guarda los atajos completos de
ambas acciones, los archivos anteriores y el estado del servicio en
`~/.local/state/clipboard-panel/`. Conserva otros atajos y secuencias de varias
teclas. Sube `[General] MaxClipItems` a 100 en `klipperrc` si era menor o no existía
(nunca lo baja) y pide a Klipper que relea su configuración mediante `reloadConfig`.
Conserva el valor anterior y lo restaura al desinstalar o revertir un fallo, salvo
que el límite se haya cambiado después (por ejemplo desde Mattux), sin sobrescribir
otros ajustes posteriores.
El panel guarda copias propias de los MIME: una expulsión de Klipper no elimina
los datos ya importados. Se conservan las últimas 100 tarjetas no fijadas; los
fijados no cuentan y nunca se expulsan. Al desfijar se vuelve a aplicar el límite.
No se pueden recuperar las cargas que Klipper eliminó antes de importarlas.

Instala la política `org.local.clipboard-panel.clear-history.policy` en
`/usr/share/polkit-1/actions/` con `sudo -n`: si sudo necesita contraseña, falla
con un mensaje claro. Solo una sesión activa puede autorizar con `auth_self`,
sin conservar la autorización para el próximo borrado. La aplicación nunca
recibe la contraseña. La desinstalación elimina solo la política que instaló;
si ya existía una política idéntica, la conserva. No se solicitan privilegios
para borrar una tarjeta individual.

Instala el ejecutable en `~/.local/libexec/clipboard-panel`, el servicio
de usuario y una entrada oculta del menú. Inicia el servicio, comprueba su estado
y transfiere Meta+V desde `plasmashell/show-on-mouse-pos` al panel.
No abre el panel al instalar ni modifica el contenido del portapapeles.

La instalación vuelve atrás si falla: restaura atajos, archivos y estado previo
del servicio. Las reinstalaciones conservan la copia original de seguridad.
El instalador se bloquea frente a otro instalador y rechaza archivos instalados
que hayan sido modificados fuera de él. No modifica `screenshot/`.

```bash
./install.sh --uninstall
```

Desinstalar devuelve los atajos y archivos previos, y conserva el historial
privado y los respaldos. Para cerrar solo el panel/servicio durante una prueba:
`systemctl --user stop clipboard-panel.service`. Esto no devuelve Meta+V; para
recuperar Klipper hay que desinstalar. No cambiar atajos manualmente durante
la prueba: al desinstalar se recupera la instantánea previa.

## Prueba interactiva pendiente

Se verificaron la instalación, la apertura por D-Bus y el aspecto en una captura
de solo lectura del escritorio real (26-11-2026), sin introducir teclas ni clics.
El atajo Meta+V, el foco, los arrastres Wayland y pegar texto/imágenes deben
verificarse con el usuario presente, después de la revisión del orquestador:

1. Acordar una pausa. Mantener dos ventanas de destino abiertas (editor y editor
   de imágenes), con el escritorio y el monitor secundario visibles. Instalar con
   el comando anterior; no debería aparecer ninguna ventana.
2. Copiar tres textos inocuos distintos y dos capturas de prueba, esperando al
   menos un segundo entre copias. En el editor, poner el puntero donde se desea
   el panel y presionar Meta+V. Verificar las tarjetas mixtas, el orden de copia,
   que el panel aparece cerca del puntero y que ambos monitores siguen normales.
3. Elegir una tarjeta antigua con clic. Verificar que el panel cierra y el editor
   recupera el foco. Presionar Ctrl+V manualmente. Reabrir Meta+V: la tarjeta
   elegida debe seguir exactamente en su lugar. Repetir con ↑/↓ y Enter.
4. Repetir con una imagen y pegar manualmente en el editor de imágenes. Probar
   nuevamente en el monitor secundario y cerca de los bordes. Comprobar que el
   panel no figura en la barra de tareas ni en Alt+Tab.
5. Escribir un filtro; comprobar «Sin resultados», Esc para limpiar y otro Esc
   para cerrar. Probar fijar, borrar una tarjeta y Borrar todo: el fijado permanece
   en su posición. Reiniciar el servicio y confirmar que se conserva el historial.
   Comprobar también RePág / AvPág y la edición del filtro con Inicio, Fin y Supr.
   El borrado afecta solo al panel; Klipper debe seguir disponible tras rollback.
6. Cambiar el tamaño desde bordes y esquinas, cerrar y reabrir Meta+V y reiniciar
   el servicio: el tamaño elegido se conserva. Probar los límites en ambos
   monitores y las tarjetas con un panel angosto y ancho. Arrastrar la fila del título,
   reabrir y confirmar que vuelve al puntero. Arrastrar la barra desde ambos extremos
   de su ancho y soltar cerca del borde: debe dejar de arrastrar normalmente.
7. Pulsar Borrar todo: verificar que el panel permanece abierto al aparecer el
   agente de polkit, cancelar y comprobar que no se borró nada. Repetir y autorizar:
   solo permanecen los fijados. Volver a pulsar: debe pedir contraseña otra vez.
8. Desinstalar con `./install.sh --uninstall` y verificar que Meta+V vuelve a
   Klipper. Si cualquier monitor parpadea o cambia, detener inmediatamente la
   prueba y ejecutar el mismo rollback; no repetir sondas visuales.

Las pruebas automatizadas verifican el modelo, los MIME y la lógica de
instalación con dobles de D-Bus/systemd. No demuestran el comportamiento real
de KWin, el arrastre Wayland, el agente de polkit ni el pegado entre aplicaciones; esas comprobaciones quedan pendientes.
